#include "storage.h"
#include "driver/ch376/ch376_spi.h"
#include "esp_log.h"
#include "service/clock/clock.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "storage";
static bool s_ready     = false;   /* 磁盘是否就绪 */
static bool s_file_open = false;   /* 用户文件是否已打开 */
static bool s_init_mount_ok;
static char s_fname[STORAGE_FILE_MAX]; /* 当前操作的完整文件名 "XX.txt" */
static uint32_t s_next_session = 1;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static storage_status_t s_status = { .state = STORAGE_UNKNOWN };
/* Storage operations have one owner. Only an explicit disconnect in this
 * operation may classify its NOT_READY result as ABSENT. */
static uint64_t s_absence_observations;

static void observe(storage_state_t state)
{
    const uint64_t now = clock_ms();
    portENTER_CRITICAL(&s_status_lock);
    s_status = (storage_status_t){ .state = state, .checked = true, .checked_uptime_ms = now };
    if (state == STORAGE_ABSENT) s_absence_observations++;
    portEXIT_CRITICAL(&s_status_lock);
}

bool storage_get_status(storage_status_t *status)
{
    if (!status) return false;
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return true;
}

static err_t observe_result(err_t result, uint64_t absence_before)
{
    if (result == ERR_PARAM) return result; /* No media observation took place. */
    observe(result == ERR_OK ? STORAGE_READY :
            (result == ERR_NOT_READY && s_absence_observations != absence_before ? STORAGE_ABSENT : STORAGE_ERROR));
    return result;
}

/* 生成合法文件名 "/<8.3大写>.TXT"
 *
 * ★ 为什么必须转大写 8.3：
 *   CH376 的 SET_FILE_NAME(0x2F) 按手册只支持 8.3 短文件名、且字母必须大写
 *   （参考工程验证过的就是 "/ASD.TXT"）。传小写/空格/中文时芯片行为未定义：
 *   它自己能用同样字符串 OPEN 回来（日志看着"成功"），
 *   但写进目录项的名字是坏的 → 电脑上"打不开 / 说找不到文件"。
 *
 * 转换规则：a-z→A-Z；字母数字 _ - 保留；空格/中文/其它 → '_'；主体截断到 8 字符。
 * 注意：超过 8 字符或含中文的用户名会丢失区分度(可能撞名)，日志里有告警。 */
static err_t make_fname(const char *user)
{
    char   base[9];
    size_t n = 0;

    if (!user || !*user) return ERR_PARAM;

    for (const char *p = user; *p && n < 8; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            base[n++] = c;
        } else {
            base[n++] = '_';                 /* 空格/中文/其它非法字符 */
        }
    }
    base[n] = '\0';

    int r = snprintf(s_fname, sizeof(s_fname), "/%s.TXT", base);
    if (r <= 0 || r >= (int)sizeof(s_fname)) {
        ESP_LOGE(TAG, "文件名过长，生成失败");
        return ERR_PARAM;
    }

    ESP_LOGI(TAG, "用户名 '%s' → 文件名 '%s'", user, s_fname);

    if (strlen(user) > 8) {
        ESP_LOGW(TAG, "用户名超过 8 字符已截断，可能与其它用户名撞名: '%s'", user);
    }
    return ERR_OK;
}

/* 新建文件并写入表头(不关闭，由调用方决定) */
static err_t create_with_header(void)
{
    if (!CH376T_FILE_CREATE_NAME(s_fname)) {
        ESP_LOGE(TAG, "创建文件失败: %s", s_fname);
        return ERR_FAIL;
    }
    const char *hdr = STORAGE_HEADER;
    if (!CH376T_BYTE_WRITE_DATA((const uint8_t *)hdr, (uint16_t)strlen(hdr))) {
        ESP_LOGE(TAG, "写表头失败: %s", s_fname);
        return ERR_FAIL;
    }
    return ERR_OK;
}

/* 开机自检：已移除。
 * 原自检每次开机都重建 /TEST.TXT，导致"电脑上删掉的文件又出现"，已删除。
 * (驱动+U盘的正确性已由 2026-09-14 的日志自检验证通过) */

static err_t storage_init_impl(void)
{
    s_init_mount_ok = false;
    CH376_PORT_INIT();              /* 1) GPIO 软 SPI 引脚 */

    /* 2) 先取芯片版本：CMD_GET_IC_VER 不依赖 USB 中断，只要 SPI 通就能读 */
    uint8_t ver = CH376T_Get_IC_Ver();
    ESP_LOGI(TAG, "CH376 版本号: 0x%02X", ver);

    if (!CH376T_SetUsbMode()) {          /* 3) 进 USB 主机模式 */
        ESP_LOGE(TAG, "USB主机模式失败");
        return ERR_FAIL;
    }
    if (!CH376T_CMD_DISK_CONNECT()) {    /* 4) 检测磁盘(需插入U盘) */
        ESP_LOGE(TAG, "磁盘未连接");
        return ERR_FAIL;
    }

    /* 5) 挂载磁盘：验证版流程里没这一步，所以【失败也不当致命错误】，
          仅告警后继续(有些U盘/固件不响应 0x31) */
    s_init_mount_ok = CH376T_DISK_MOUNT();
    if (!s_init_mount_ok) {
        ESP_LOGW(TAG, "磁盘挂载(0x31)返回失败，继续尝试文件操作");
    }

    s_ready = true;
    ESP_LOGI(TAG, "storage ready");
    return ERR_OK;
}

/* 介质同步：每次"要开始新动作"前调用（此时必须没有文件打开）。
 *
 * 为什么必须有这一步：
 *   CH376 挂载后会把 FAT 表 + 根目录扇区缓存在芯片内部 RAM。
 *   如果 U 盘被拔下来(在电脑上改过)再插回板子，而板子没重新 MOUNT，
 *   芯片就会拿着【过期的目录缓存】继续读写 ——
 *   表现就是：电脑上删掉的文件"复活"、板子新写的文件在电脑上找不到。
 *
 * ★ 关键：CH376 的 CMD_DISK_CONNECT(0x30) 会让芯片重新检测/枚举 USB 设备，
 *   这会【作废已挂载的 FAT 上下文】。所以发完 0x30 之后，
 *   无论返回什么，都必须再补一次 CMD_DISK_MOUNT(0x31)，否则后续
 *   文件操作会一律返回 0x42(ERR_MISS_FILE 文件未找到)——哪怕文件就在盘上!
 *
 * 注意：0x30 会打断芯片的文件上下文，只能在"无文件打开"时调用。 */
static err_t media_sync(void)
{
    if (!s_ready) {
        ESP_LOGW(TAG, "磁盘未就绪，重试初始化...");
        return storage_init();
    }

    uint8_t st = CH376T_DiskCheckRaw();        /* 0x30 检测介质在位 */

    if (st == USB_INT_DISCONNECT) {            /* 0x16 介质已被拔出 */
        ESP_LOGW(TAG, "检测到U盘已拔出");
        s_ready = false;
        observe(STORAGE_ABSENT);
        return ERR_NOT_READY;
    }

    /* 在位(0x14) / 刚插回(0x15) / 其它状态：统一重新挂载一次，
       既让"拔插过"的情况重读 FAT，也修复 0x30 造成的上下文作废 */
    if (st != USB_INT_SUCCESS && st != USB_INT_CONNECT) {
        ESP_LOGW(TAG, "U盘状态异常: 0x%02X", st);
    }

    if (!CH376T_DISK_MOUNT()) {                /* 0x31 重新挂载(必须!) */
        ESP_LOGE(TAG, "重新挂载失败");
        s_ready = false;
        return ERR_NOT_READY;
    }
    return ERR_OK;
}

static err_t storage_create_session_impl(char *stem, size_t stem_size)
{
    if (!stem || stem_size < 9) return ERR_PARAM;
    if (media_sync() != ERR_OK) return ERR_NOT_READY;

    for (uint32_t id = s_next_session; id <= 9999999u; id++) {
        char candidate[9];
        snprintf(candidate, sizeof(candidate), "U%07lu", (unsigned long)id);
        if (make_fname(candidate) != ERR_OK) return ERR_FAIL;

        uint8_t status = CH376T_FILE_OPEN_STATUS(s_fname);
        if (status == USB_INT_SUCCESS) {
            if (!CH376T_FILE_CLOSE()) return ERR_FAIL;
            continue;
        }
        if (status != 0x42) { /* ERR_MISS_FILE: only this means a free name */
            ESP_LOGE(TAG, "检查文件名失败: %s (0x%02X)", s_fname, status);
            return ERR_FAIL;
        }

        if (create_with_header() != ERR_OK) {
            CH376T_FILE_CLOSE();
            return ERR_FAIL;
        }
        if (!CH376T_FILE_CLOSE()) return ERR_FAIL;

        /* 重新挂载并回读，确认目录项确实写入介质。 */
        if (!CH376T_DISK_MOUNT() || !CH376T_FILE_OPEN_NAME(s_fname)) {
            return ERR_FAIL;
        }
        uint32_t size = CH376T_GetFileSize();
        if (!CH376T_FILE_CLOSE() || size < strlen(STORAGE_HEADER)) {
            return ERR_FAIL;
        }
        snprintf(stem, stem_size, "%s", candidate);
        s_next_session = id + 1;
        ESP_LOGI(TAG, "新测量文件: %s", s_fname);
        return ERR_OK;
    }
    return ERR_NO_MEM;
}

/* 断盘恢复：保证本次测量文件存在，不保持打开。 */
static err_t storage_ensure_session_impl(const char *user)
{
    err_t e = media_sync();
    if (e != ERR_OK) return e;

    e = make_fname(user);
    if (e != ERR_OK) return e;

    /* 已存在 → 保留原内容，直接返回(不再写表头) */
    uint8_t open_status = CH376T_FILE_OPEN_STATUS(s_fname);
    if (open_status == USB_INT_SUCCESS) {
        uint32_t sz = CH376T_GetFileSize();
        if (!CH376T_FILE_CLOSE()) {
            ESP_LOGE(TAG, "关闭文件失败: %s", s_fname);
            return ERR_FAIL;
        }
        ESP_LOGI(TAG, "测量文件已存在(保留原内容, 当前 %lu 字节): %s",
                 (unsigned long)sz, s_fname);
        return ERR_OK;
    }
    if (open_status != 0x42) return ERR_NOT_READY;

    /* 不存在 → 新建 + 写表头 + 关闭 */
    if (create_with_header() != ERR_OK) {
        CH376T_FILE_CLOSE();
        return ERR_FAIL;
    }
    if (!CH376T_FILE_CLOSE()) {
        ESP_LOGE(TAG, "关闭文件失败 → 目录项可能没落盘: %s", s_fname);
        return ERR_FAIL;
    }
    ESP_LOGI(TAG, "已创建测量文件并写入表头: %s", s_fname);

    /* ---- 落盘验证：先重新挂载(强制芯片丢掉内部 FAT/目录缓存、重读磁盘)，
           再打开这个文件。能打开+长度对，才说明数据【真的写进U盘了】；
           如果这里失败，说明目录项只存在于芯片缓存里，拔盘/断电就没了。 ---- */
    if (!CH376T_DISK_MOUNT()) {
        ESP_LOGE(TAG, "落盘验证失败: 重新挂载失败");
        return ERR_FAIL;
    }
    if (!CH376T_FILE_OPEN_NAME(s_fname)) {
        ESP_LOGE(TAG, "落盘验证失败: 重新挂载后打不开 %s → 文件没真正写进U盘!",
                 s_fname);
        return ERR_FAIL;
    }
    uint32_t sz = CH376T_GetFileSize();
    CH376T_FILE_CLOSE();
    ESP_LOGI(TAG, "落盘验证通过: %s 确实在U盘上, 长度 %lu 字节",
             s_fname, (unsigned long)sz);
    return ERR_OK;
}

/* 每分钟打开、追加、关闭。本函数不做 0x30/0x31 介质同步；
 * 该操作只在新测量或故障恢复时、无文件打开的条件下执行。 */
static err_t storage_open_session_impl(const char *user)
{
    err_t e = make_fname(user);
    if (e != ERR_OK) return e;

    if (!s_ready) return ERR_NOT_READY;

    uint8_t open_status = CH376T_FILE_OPEN_STATUS(s_fname);
    if (open_status != USB_INT_SUCCESS) {
        if (open_status != 0x42) return ERR_NOT_READY;
        /* 文件确实不存在：现场建一个并写表头。 */
        ESP_LOGW(TAG, "文件不存在，现场创建: %s", s_fname);
        if (create_with_header() != ERR_OK) {
            CH376T_FILE_CLOSE();
            s_file_open = false;
            return ERR_FAIL;
        }
        /* 刚建的为空文件，指针已在末尾，直接保持打开即可 */
        s_file_open = true;
        ESP_LOGI(TAG, "打开用户文件(新建, 追加模式): %s", s_fname);
        return ERR_OK;
    }

    /* 已有内容 → 把写指针移到文件末尾，后续写入即为追加 */
    uint32_t len = CH376T_GetFileSize();
    if (len > 0 && !CH376T_BYTE_LOCATE(len)) {
        ESP_LOGE(TAG, "定位文件末尾失败(len=%lu): %s",
                 (unsigned long)len, s_fname);
        CH376T_FILE_CLOSE();
        s_file_open = false;
        return ERR_FAIL;
    }

    s_file_open = true;
    ESP_LOGI(TAG, "打开用户文件(追加模式, 当前 %lu 字节): %s",
             (unsigned long)len, s_fname);
    return ERR_OK;
}

static err_t storage_write_open_impl(const char *text)
{
    if (!s_ready)     return ERR_NOT_READY;
    if (!s_file_open) return ERR_NOT_READY;
    if (!text)        return ERR_PARAM;

    uint16_t len = (uint16_t)strlen(text);
    if (!CH376T_BYTE_WRITE_DATA((const uint8_t *)text, len)) {
        ESP_LOGE(TAG, "写入文件失败: %s", s_fname);
        return ERR_FAIL;
    }
    /* 文件保持打开，写指针已随写入前进，下次写接着往后写 */
    return ERR_OK;
}

static err_t storage_close_session_impl(void)
{
    if (!s_file_open) return ERR_OK;         /* 本来就没打开 */

    if (!CH376T_FILE_CLOSE()) {
        ESP_LOGE(TAG, "关闭文件失败: %s", s_fname);
        s_file_open = false;
        s_fname[0] = '\0';
        return ERR_FAIL;
    }
    s_file_open = false;
    s_fname[0] = '\0';
    return ERR_OK;
}

err_t storage_init(void)
{
    uint64_t before = s_absence_observations;
    err_t result = storage_init_impl();
    if (result == ERR_OK && !s_init_mount_ok) {
        /* Preserve the existing nonfatal startup behavior while reporting the
         * failed mount honestly; a later successful file operation clears it. */
        observe(STORAGE_ERROR);
        return result;
    }
    return observe_result(result, before);
}

err_t storage_create_session(char *stem, size_t size)
{
    uint64_t before = s_absence_observations;
    return observe_result(storage_create_session_impl(stem, size), before);
}

err_t storage_ensure_session(const char *stem)
{
    uint64_t before = s_absence_observations;
    return observe_result(storage_ensure_session_impl(stem), before);
}

err_t storage_open_session(const char *stem)
{
    uint64_t before = s_absence_observations;
    return observe_result(storage_open_session_impl(stem), before);
}

err_t storage_write_open(const char *text)
{
    uint64_t before = s_absence_observations;
    return observe_result(storage_write_open_impl(text), before);
}

err_t storage_close_session(void)
{
    bool was_open = s_file_open;
    uint64_t before = s_absence_observations;
    err_t result = storage_close_session_impl();
    return was_open ? observe_result(result, before) : result;
}
