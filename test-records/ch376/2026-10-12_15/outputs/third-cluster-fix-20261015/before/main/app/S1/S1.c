#include "S1.h"
#include "service/clock/clock.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "service/urine/urine.h"
#include "service/mobile/mobile.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "S1";

/* 日志测试副本：保留 storage 与 CH376 的完整阶段日志。
 * 日志等级均先保存再恢复，不影响事务之外的配置。 */
#define CH376_LOG_TAG   "CH376T"
#define STORAGE_LOG_TAG "storage"

/* ===== S1 测量中：测量期数据 =====
 * 本模块只负责"把测量结果写到串口屏" + "每分钟往 U盘写一行"，外加会话计时与
 * 24 小时结论；尿量的解算在 service/urine（monitor 每周期调 urine_poll()）。
 * 状态栏/报警栏两个文本控件由 monitor 写，本模块只写 4 个数值控件。 */

/* ---- 首个 24 小时尿量结论的阈值（沿用原 S3.c 的临床口径）---- */
#define DAY_MS           86400000ULL
#define DAY_ANURIA_ML    100.0f
#define DAY_OLIGURIA_ML  400.0f
#define DAY_POLYURIA_ML 2500.0f

/* U 盘掉盘后的重试间隔 */
#define STORAGE_RETRY_MS 5000ULL

/* 24 小时结论（报警栏四态之一；"尿袋满"由 monitor 按尿量直接判定） */
typedef enum {
    ALERT_NONE = 0,
    ALERT_ANURIA,
    ALERT_OLIGURIA,
    ALERT_POLYURIA,
} day_alert_t;

static day_alert_t s_day_alert  = ALERT_NONE;
static bool        s_day_locked = false;   /* 满 24h 只判定一次(首次锁定) */

/* 会话计时：测量中累加；暂停由 S1_settle 结算后冻结；复位归零 */
static uint64_t s_elapsed_ms = 0;
static uint64_t s_acc_tick   = 0;   /* 上一次记账时刻(clock_ms) */
static uint32_t s_last_min   = 0;   /* 已记录的分钟序号 */

/* 推屏节流：显示屏不需要 50Hz，200ms 一次足够
   (monitor 20ms 一轮 → 实际约 5Hz，避免把串口屏刷爆/卡顿) */
#define S1_UI_PERIOD_MS   200u
static uint64_t s_ui_tick = 0;        /* 上次推屏时刻 */

/* ===== U盘记录(每分钟一条) =====
 * 文件不常开：写盘那一刻才 open→write→close，全程在临界区内。 */
static char s_session[9] = ""; /* U0000001：本次测量的 8.3 文件名主体 */
static bool s_storage_ok = false;
static uint64_t s_next_retry_ms = 0;
static volatile bool s_ui_pause = false; /* true: 写盘临界区内, 暂停推屏 */
static uint32_t s_crit_depth = 0;        /* 写盘临界区嵌套深度(只允许同一任务重入) */
static esp_log_level_t s_ch376_lvl   = ESP_LOG_INFO; /* 最外层进入前 CH376T 的等级 */
static esp_log_level_t s_storage_lvl = ESP_LOG_INFO; /* 最外层进入前 storage 的等级 */
static bool s_capture_restore; /* 最外层进入前液滴中断是否开启 */
static float     s_last_ml   = 0.0f;  /* 上一分钟标记时的累计尿量(算增量用) */
static SemaphoreHandle_t s_spi_mtx = NULL; /* 串行化 CH376 SPI 访问 */

/* ---- 存储区进出：暂停手机状态/串口屏推送，暂关液滴中断，拿 SPI 锁。
 * 这里是互斥保护的事务区，不是关闭调度的 FreeRTOS 临界区。
 * CH376 可等待/让出任务；Wi-Fi 连接、控制 ACK 和网络底层继续运行。
 *
 * 【可重入设计】递归互斥量 + s_crit_depth 深度计数，两件事各管各的：
 *   · take/give —— 每一层都配对(见下面的理由)，负责跨任务串行；
 *   · 深度计数 —— 只有最外层保存/恢复采集、推送及日志，内层退出不提前恢复。 */
static void s1_crit_enter(void)
{
    if (!s_spi_mtx) {
        s_spi_mtx = xSemaphoreCreateRecursiveMutex();
    }
    if (s_spi_mtx) {
        /* 无条件拿锁(不要挪进下面的 depth 判断里)：深度是全局的，
         * 那样写会让"别人正在区内"的任务直接闯进来，跨任务串行就没了。
         * 这一层拿的锁，由 s1_crit_exit() 里对应的那一层 give 还掉。 */
        xSemaphoreTakeRecursive(s_spi_mtx, portMAX_DELAY);
    }

    if (s_crit_depth == 0) {
        service_mobile_set_push_paused(true);
        s_capture_restore = urine_capture_suspend();
        s_ch376_lvl   = esp_log_level_get(CH376_LOG_TAG);
        s_storage_lvl = esp_log_level_get(STORAGE_LOG_TAG);
        esp_log_level_set(CH376_LOG_TAG, ESP_LOG_INFO);
        esp_log_level_set(STORAGE_LOG_TAG, ESP_LOG_INFO);
        s_ui_pause = true;                           /* 停串口屏推送 */
    }
    s_crit_depth++;
}

static void s1_crit_exit(void)
{
    if (s_crit_depth == 0) return;   /* 没进过就不退(防御) */
    s_crit_depth--;

    if (s_crit_depth == 0) {
        /* 任何存储结果都从这里恢复；不得用 arm 清掉已有滴间隔/缓冲。 */
        esp_log_level_set(CH376_LOG_TAG, s_ch376_lvl);
        esp_log_level_set(STORAGE_LOG_TAG, s_storage_lvl);
        s_ui_pause = false;
        urine_capture_restore(s_capture_restore);
        service_mobile_set_push_paused(false);
    }

    /* 【必须每一层都 give，不能"只在最外层 give"】
     * 递归互斥量按层计数：enter() 每一层都 take 了一次，若这里只有最外层 give，
     * 深度 2 就变成 take×2 / give×1，递归计数残留 1 → 锁再也放不掉(别的任务永久阻塞)。
     * 【也不能用深度决定"要不要 take"】s_crit_depth 是全局的：若 enter() 写成
     * "depth>0 就不拿锁"，别的任务看到有人正在区内就会直接闯进去，SPI 不再串行。
     * 结论：take/give 每层配对；深度只管保存/恢复等级与推屏开关。
     * (内层 give 后外层仍持有锁——递归计数还剩 1，跨任务互斥不受影响。) */
    if (s_spi_mtx) {
        xSemaphoreGiveRecursive(s_spi_mtx);
    }
}

/* 错误码 → 短文本：只为写盘告警能一眼看懂是哪类问题(取值见 bsp/err.h) */
static const char *err_text(err_t e)
{
    switch (e) {
    case ERR_FAIL:      return "一般失败";
    case ERR_TIMEOUT:   return "超时";
    case ERR_NOT_READY: return "未就绪";
    case ERR_BUSY:      return "忙";
    case ERR_PARAM:     return "参数错误";
    case ERR_NO_MEM:    return "内存不足";
    default:            return "?";
    }
}

/* 24 小时结论 → 报警栏单字母（与 monitor 约定的协议）
 *   A=正常  B=无尿  C=少尿  D=多尿    (E=尿袋满由 monitor 按尿量插播) */
static const char *day_alert_text(day_alert_t alert)
{
    switch (alert) {
    case ALERT_ANURIA:   return "B";
    case ALERT_OLIGURIA: return "C";
    case ALERT_POLYURIA: return "D";
    default:             return "A";
    }
}

/* 把会话计时格式化成 hh:mm:ss */
static void fmt_elapsed(char *buf, size_t sz)
{
    uint32_t sec = (uint32_t)(s_elapsed_ms / 1000);
    snprintf(buf, sz, "%02lu:%02lu:%02lu",
             (unsigned long)(sec / 3600),
             (unsigned long)((sec % 3600) / 60),
             (unsigned long)(sec % 60));
}

/* 只推 4 个"数值/时间"控件(不带状态)
 * —— 暂停冻结数据、复位时归零、进入测量时对齐，都用它 */
void S1_push_values(void)
{
    const urine_meas_t *m = urine_get();
    char ts[16];

    screen_show_urine(m->total_ml);        /* ① 累计尿量 ml   -> data.t0 */
    screen_show_rate(m->rate_mlh);         /* ② 实时速率 ml/h -> data.t1 */
    screen_show_drops(m->total_drops);     /* ③ 累计滴数      -> data.t2 */
    fmt_elapsed(ts, sizeof(ts));           /* ④ 会话计时      -> data.t3 */
    screen_show_elapsed(ts);
}

/* 带节流的推屏：写盘临界区内不碰串口屏。
 * (本函数与写盘都在 monitor 任务里串行执行，正常不会重入；
 *  s_ui_pause 保留原 S3.c 的防御，避免以后有人从别的任务调进来。) */
static void s1_push_ui(void)
{
    if (s_ui_pause) return;
    S1_push_values();
}

/* ============================================================
 * U盘记录：每分钟一条
 *   内容: 时间(hh:mm:ss)  本分钟尿量(ml)  本分钟平均流速(ml/h)
 *   本分钟尿量 = 本分钟末累计总量 - 上一分钟标记时的累计总量
 *   平均流速   = 本分钟尿量 × 60
 *
 *   触发时暂停手机/串口屏数据推送、关闭液滴中断、拿 SPI 锁。
 *   storage 阶段日志保留，CH376 的警告/错误保留。
 * ============================================================ */
static void s1_record_write(uint32_t minute)
{
    char   c1[16];                                /* 时间 hh:mm:ss */
    char   c2[16];                                /* 本分钟尿量   */
    char   c3[16];                                /* 本分钟平均流速 */
    char   line[80];
    uint32_t sec = minute * 60u;
    float  total = urine_get()->total_ml;
    float  delta = total - s_last_ml;             /* 本分钟尿量增量(ml) */
    float  rate  = delta * 60.0f;                 /* 本分钟平均流速(ml/h) */
    err_t  e;
    const char *step = "打开";                    /* 失败时指明卡在哪一步 */
    const uint64_t transaction_begin = clock_ms();

    s_last_ml = total;                            /* 消费掉这一分钟的增量基准 */

    /* 三列各自成串，再按 storage.h 里的列宽/间隔【右对齐】拼成一行 */
    snprintf(c1, sizeof(c1), "%02lu:%02lu:%02lu",
             (unsigned long)(sec / 3600),
             (unsigned long)((sec % 3600) / 60),
             (unsigned long)(sec % 60));
    snprintf(c2, sizeof(c2), "%.1fml", (double)delta);
    snprintf(c3, sizeof(c3), "%.1fml/h", (double)rate);

    int n = snprintf(line, sizeof(line), REC_ROW_FMT, c1, c2, c3);
    if (n < 0 || n >= (int)sizeof(line) - 2) {
        ESP_LOGW(TAG, "第%lu分钟: 记录字符串过长，跳过", (unsigned long)minute);
        return;
    }

    if (!s_storage_ok || !*s_session) {
        ESP_LOGW(TAG, "第%lu分钟未存储(U盘不可用)", (unsigned long)minute);
        return;
    }

    /* 写盘前留一行本条内容：事后能核对 U 盘里的记录与屏上读数是否对得上 */
    ESP_LOGI(TAG, ">>>>> 进入临界区(停液滴中断/停推屏/暂停手机推送): 第%lu分钟 %s", (unsigned long)minute, line);

    line[n++] = '\r';                             /* 行尾 */
    line[n++] = '\n';
    line[n]   = '\0';

    s1_crit_enter();
    /* ============ 受保护的打开/写入/关闭事务 ============ */
    e = storage_open_session(s_session); /* 打开 + 定位到文件末尾(追加) */
    ESP_LOGI(TAG, "[写盘排查] 分钟=%lu 打开返回=%d 文件=/%s.TXT", (unsigned long)minute, (int)e, s_session);
    if (e == ERR_OK) {
        step = "写入";
        e = storage_write_open(line);    /* 写这一行 */
        ESP_LOGI(TAG, "[写盘排查] 分钟=%lu 写入返回=%d 请求字节=%d", (unsigned long)minute, (int)e, n);
        if (e == ERR_OK) step = "关闭";
        err_t close_err = storage_close_session();
        ESP_LOGI(TAG, "[写盘排查] 分钟=%lu 关闭返回=%d", (unsigned long)minute, (int)close_err);
        if (e == ERR_OK) e = close_err;
    }
    /* ============================================== */
    s1_crit_exit();

    ESP_LOGI(TAG, "[写盘排查] 分钟=%lu 总返回=%d 阶段=%s 总耗时=%llu毫秒（仅命令反馈，未验证电脑可读）",
             (unsigned long)minute, (int)e, step, (unsigned long long)(clock_ms() - transaction_begin));

    if (e != ERR_OK) {
        s_storage_ok = false;
        s_next_retry_ms = clock_ms() + STORAGE_RETRY_MS;
        /* 在恢复采集/推送后，汇总具体失败阶段和错码。 */
        ESP_LOGW(TAG, "<<<<< 退出临界区: 第%lu分钟写盘失败(%s阶段 错误码=%d %s): /%s.TXT 已跳过，%u秒后重试",
                 (unsigned long)minute, step, (int)e, err_text(e), s_session,
                 (unsigned)(STORAGE_RETRY_MS / 1000ULL));
    } else {
        ESP_LOGI(TAG, "<<<<< 退出临界区: 第%lu分钟已写入U盘: /%s.TXT", (unsigned long)minute, s_session);
    }
}

/* 掉盘后每 5 秒重试一次：标识锁定后只恢复同一文件。
 * 尚未选定编号（如开始时没有U盘）才允许尝试首次分配。 */
static void storage_retry(uint64_t now_ms)
{
    err_t e;
    bool resume;      /* true=恢复已有会话文件；false=新分配一个 */

    if (s_storage_ok || now_ms < s_next_retry_ms) return;

    resume = (*s_session != '\0');
    ESP_LOGI(TAG, "[会话重试] 原因=%s 文件=%s；已分配的检测禁止换号或自动重建",
             resume ? "验证或追加失败，恢复原文件" : "尚未分配文件，首次分配",
             resume ? s_session : "未分配");
    ESP_LOGI(TAG, ">>>>> 进入临界区: %s测量文件", resume ? "恢复" : "新建");
    s1_crit_enter();
    e = resume ? storage_ensure_session(s_session)
               : storage_create_session(s_session, sizeof(s_session));
    s1_crit_exit();

    s_storage_ok = (e == ERR_OK);
    s_next_retry_ms = clock_ms() + STORAGE_RETRY_MS;
    if (s_storage_ok) {
        ESP_LOGI(TAG, "<<<<< 退出临界区: 测量文件 /%s.TXT 已就绪", s_session);
    } else {
        ESP_LOGW(TAG, "<<<<< 退出临界区: %s失败(错误码=%d %s)，继续计量，%u秒后重试",
                 resume ? "恢复会话" : "新建文件", (int)e, err_text(e),
                 (unsigned)(STORAGE_RETRY_MS / 1000ULL));
        ESP_LOGW(TAG, "[会话保护] 当前文件=%s；已锁定时仅恢复，不自动重建。若文件缺失或表头损坏，请保留日志后复位开启新检测",
                 *s_session ? s_session : "尚未分配");
    }
}

void S1_storage_start(void)
{
    ESP_LOGI(TAG, "[会话控制] 新检测分配入口，上一标识=%s", *s_session ? s_session : "无");
    s_session[0] = '\0';
    s_storage_ok = false;
    s_next_retry_ms = 0;
    storage_retry(clock_ms());
}

void S1_storage_stop(void)
{
    ESP_LOGI(TAG, "[会话控制] 复位结束当前检测，释放标识=%s", *s_session ? s_session : "无");
    s_session[0] = '\0';
    s_storage_ok = false;
    s_next_retry_ms = 0;
}

const char *S1_session_file(void)
{
    return s_session;
}

const char *S1_day_alert(void)
{
    return day_alert_text(s_day_alert);
}

uint64_t S1_elapsed_ms(void)
{
    return s_elapsed_ms;
}

void S1_reset(void)
{
    urine_reset();                  /* 会话从 0 计(重记测量起点) */
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_acc_tick   = clock_ms();
    s_last_ml    = 0.0f;            /* 每分钟增量基准归零 */
    s_day_alert  = ALERT_NONE;
    s_day_locked = false;
}

void S1_settle(void)
{
    /* 结算计时到暂停瞬间：暂停期间的时间不计入会话 */
    uint64_t now = clock_ms();
    s_elapsed_ms += now - s_acc_tick;
    s_acc_tick    = now;
    urine_rate_zero();              /* 屏上速率不留旧读数 */
}

void S1_enter(void)
{
    /* 计时基准挪到"此刻"：开始测量与暂停后继续都适用。
     * 继续时这一段"上次记账(暂停瞬间) → 现在"的时间被跳过，正是暂停不计时的效果。 */
    s_acc_tick = clock_ms();
    urine_rate_zero();
    /* 立刻推一次，让屏上的数值马上和当前状态对齐(不等到 200ms 节拍) */
    s_ui_tick = clock_ms();
    S1_push_values();
}

/* 每周期入口(monitor 20ms 调一次)：计时 → 24h 结论 → 每分钟写盘 → 推屏 */
void S1_run(void)
{
    uint64_t now = clock_ms();
    const urine_meas_t *m = urine_get();

    /* ① 推进会话计时 */
    s_elapsed_ms += now - s_acc_tick;
    s_acc_tick    = now;

    /* ② 满 24 小时首次锁定结论(阈值沿用原 S3.c) */
    if (!s_day_locked && s_elapsed_ms >= DAY_MS) {
        s_day_locked = true;
        if (m->total_ml < DAY_ANURIA_ML)        s_day_alert = ALERT_ANURIA;
        else if (m->total_ml < DAY_OLIGURIA_ML) s_day_alert = ALERT_OLIGURIA;
        else if (m->total_ml > DAY_POLYURIA_ML) s_day_alert = ALERT_POLYURIA;
        else                                    s_day_alert = ALERT_NONE;
        ESP_LOGI(TAG, "首个24小时尿量=%.1fml，结论=%s",
                 (double)m->total_ml, day_alert_text(s_day_alert));
    }

    /* ③ 掉盘重试(写盘前先看看能不能恢复) */
    storage_retry(now);

    /* ④ 每到新的一分钟：往 U盘写一条 */
    uint32_t mins = (uint32_t)(s_elapsed_ms / 60000ULL);
    if (mins > s_last_min) {
        s_last_min = mins;
        s1_record_write(mins);
    }

    /* ⑤ 推屏：200ms 节流(20ms 周期下约 5Hz)，避免把串口屏喂爆 */
    if (now - s_ui_tick >= S1_UI_PERIOD_MS) {
        s_ui_tick = now;
        s1_push_ui();
    }
}
