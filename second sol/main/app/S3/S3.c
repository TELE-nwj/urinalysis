#include "S3.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "driver/drop/drop.h"
#include "driver/time_svc/time_svc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "S3";

/* CH376 驱动内部的日志 TAG —— 写盘临界区里要临时把它屏蔽掉 */
#define CH376_LOG_TAG   "CH376T"

/* ===== S3 测量中：测量期数据 =====
 * 本状态只负责"把测量结果写到串口屏" + "每分钟往 U盘写一行"。
 * 写盘相关实现见 service/storage。 */
static urine_meas_t s_meas;

typedef enum {
    ALERT_NONE = 0,
    ALERT_ANURIA,
    ALERT_OLIGURIA,
    ALERT_POLYURIA,
} urine_alert_t;

#define DAY_MS 86400000ULL
#define STORAGE_RETRY_MS 5000ULL
static urine_alert_t s_day_alert = ALERT_NONE;
static bool s_day_locked = false;

/* 会话计时：测量中累加；暂停冻结、复位归零 */
static uint64_t s_elapsed_ms = 0;
static uint64_t s_acc_tick   = 0;   /* 上一次记账时刻 */
static uint32_t s_last_min   = 0;   /* 已记录的分钟序号 */

/* 推屏节流：显示屏不需要 50Hz，200ms 一次足够
   (monitor 20ms 一轮 → 实际约 5Hz，避免把串口屏刷爆/卡顿) */
#define S3_UI_PERIOD_MS   200u
static uint64_t s_ui_tick = 0;        /* 上次推屏时刻 */
static char     s_last_status[32];    /* UTF-8 组合警报需要足够的字节空间 */

/* ===== U盘记录(每分钟一条) =====
 * 文件不常开：写盘那一刻才 open→write→close，全程在临界区内。 */
static char s_session[9] = ""; /* U0000001：本次测量的 8.3 文件名主体 */
static bool s_storage_ok = false;
static uint64_t s_next_retry_ms = 0;
static volatile bool s_ui_pause = false; /* true: 写盘临界区内, 暂停推屏 */
static float     s_last_ml   = 0.0f;  /* 上一分钟标记时的累计尿量(算增量用) */
static SemaphoreHandle_t s_spi_mtx = NULL; /* 串行化 CH376 SPI 访问 */

/* ---- 存储区进出：暂停推屏 + 屏蔽驱动日志 + 拿 SPI 锁。
 * 液滴中断继续运行，否则介质重试期间会漏计。 ---- */
static void S3_crit_enter(void)
{
    if (!s_spi_mtx) {
        s_spi_mtx = xSemaphoreCreateMutex();
    }
    if (s_spi_mtx) {
        xSemaphoreTake(s_spi_mtx, portMAX_DELAY);
    }

    esp_log_level_set(CH376_LOG_TAG, ESP_LOG_NONE);  /* 区内不许有日志 */
    s_ui_pause = true;                               /* 停串口屏推送 */
}

static void S3_crit_exit(void)
{
    s_ui_pause = false;
    esp_log_level_set(CH376_LOG_TAG, ESP_LOG_INFO);

    if (s_spi_mtx) {
        xSemaphoreGive(s_spi_mtx);
    }
}

/* 当前状态文本(报警优先级) */
static const char *cur_status(void)
{
    /* 临床/满袋提示为主；存储故障每 5 秒插播 1 秒，避免被遮挡。 */
    if (!s_storage_ok && (timesvc_now_ms() / 1000ULL) % 5ULL == 0ULL) {
        return "U盘异常";
    }
    if (s_meas.flag_bag_full && s_day_alert == ALERT_POLYURIA) return "尿袋满/多尿";
    if (s_meas.flag_bag_full) return "尿袋满";
    if (s_day_alert == ALERT_ANURIA) return "无尿";
    if (s_day_alert == ALERT_OLIGURIA) return "少尿";
    if (s_day_alert == ALERT_POLYURIA) return "多尿";
    if (!s_storage_ok) return "U盘异常";
    return "测量中";
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
 * —— 离开 S3 时冻结数据、复位时归零，都用它 */
void S3_push_values(void)
{
    char ts[16];
    screen_show_urine(s_meas.total_ml);        /* ① 累计尿量 ml   -> data.n0.txt */
    screen_show_rate(s_meas.rate_mlh);         /* ② 实时速率 ml/h -> data.n1.txt */
    screen_show_drops(s_meas.total_drops);     /* ③ 累计滴数      -> data.n2.txt */
    fmt_elapsed(ts, sizeof(ts));               /* ④ 会话计时      -> data.t1.txt */
    screen_show_time(ts);
}

/* 把当前解算结果推送到串口屏(5 个量)；状态文本变了才推，避免反复刷同一个值 */
void S3_push_ui(void)
{
    const char *st;

    if (s_ui_pause) return;            /* 写盘临界区内: 不碰串口屏 */

    S3_push_values();

    st = cur_status();                         /* ⑤ 状态/报警    -> data.t0.txt */
    if (strcmp(st, s_last_status) != 0) {
        snprintf(s_last_status, sizeof(s_last_status), "%s", st);
        screen_show_status(st);
    }
}

/* ============================================================
 * U盘记录：每分钟一条
 *   内容: 时间(hh:mm:ss)  本分钟尿量(ml)  本分钟平均流速(ml/h)
 *   本分钟尿量 = 本分钟末累计总量 - 上一分钟标记时的累计总量
 *   平均流速   = 本分钟尿量 × 60
 *
 *   触发时暂停推屏 + 屏蔽驱动日志 + 拿 SPI 锁，液滴中断继续计数
 *   临界区内【只做 SPI 通讯，不打印任何日志】
 * ============================================================ */
static void S3_record_write(uint32_t minute)
{
    char   c1[16];                                /* 时间 hh:mm:ss */
    char   c2[16];                                /* 本分钟尿量   */
    char   c3[16];                                /* 本分钟平均流速 */
    char   line[80];
    uint32_t sec = minute * 60u;
    float  delta = s_meas.total_ml - s_last_ml;   /* 本分钟尿量增量(ml) */
    float  rate  = delta * 60.0f;                 /* 本分钟平均流速(ml/h) */
    err_t  e;

    s_last_ml = s_meas.total_ml;                  /* 消费掉这一分钟的增量基准 */

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

    /* ---- 进临界区前打一条(带上本条内容，便于核对) ---- */
    ESP_LOGI(TAG, ">>>>> 写盘(停推屏): 第%lu分钟 %s",
             (unsigned long)minute, line);

    line[n++] = '\r';                             /* 行尾 */
    line[n++] = '\n';
    line[n]   = '\0';

    S3_crit_enter();
    /* ============ 临界区内: 只有 SPI 通讯 ============ */
    e = storage_open_session(s_session); /* 打开 + 定位到文件末尾(追加) */
    if (e == ERR_OK) {
        e = storage_write_open(line);    /* 写这一行 */
        err_t close_err = storage_close_session();
        if (e == ERR_OK) e = close_err;
    }
    /* ============================================== */
    S3_crit_exit();

    if (e != ERR_OK) {
        s_storage_ok = false;
        s_next_retry_ms = timesvc_now_ms() + STORAGE_RETRY_MS;
        ESP_LOGW(TAG, "<<<<< 退出临界区: 第%lu分钟写盘失败，已跳过",
                 (unsigned long)minute);
    } else {
        ESP_LOGI(TAG, "<<<<< 退出临界区: 第%lu分钟已写入U盘",
                 (unsigned long)minute);
    }
}

static void storage_retry(uint64_t now_ms)
{
    err_t e;
    if (s_storage_ok || now_ms < s_next_retry_ms) return;
    S3_crit_enter();
    e = *s_session ? storage_ensure_session(s_session)
                   : storage_create_session(s_session, sizeof(s_session));
    S3_crit_exit();
    s_storage_ok = (e == ERR_OK);
    s_next_retry_ms = timesvc_now_ms() + STORAGE_RETRY_MS;
    if (s_storage_ok) ESP_LOGI(TAG, "测量文件: /%s.TXT", s_session);
    else ESP_LOGW(TAG, "U盘不可用，继续计量，稍后重试");
}

void S3_storage_start(void)
{
    s_session[0] = '\0';
    s_storage_ok = false;
    s_next_retry_ms = 0;
    storage_retry(timesvc_now_ms());
}

void S3_storage_stop(void)
{
    s_session[0] = '\0';
    s_storage_ok = false;
    s_next_retry_ms = 0;
}

const char *S3_session_file(void)
{
    return s_session;
}

void S3_reset_data(uint64_t now_ms)
{
    urine_meas_reset(&s_meas);      /* 会话从 0 计(重记测量起点) */
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_acc_tick   = now_ms;
    s_last_ml    = 0.0f;            /* 每分钟增量基准归零 */
    s_day_alert  = ALERT_NONE;
    s_day_locked = false;
}

void S3_update(uint64_t now_ms)
{
    /* ① 推进会话计时 */
    s_elapsed_ms += now_ms - s_acc_tick;
    s_acc_tick    = now_ms;

    /* ② 解算：累计/瞬时速率/方式A缺水/尿袋满(需上层先 drop_poll) */
    urine_update(&s_meas);

    if (!s_day_locked && s_elapsed_ms >= DAY_MS) {
        s_day_locked = true;
        if (s_meas.total_ml < 100.0f) s_day_alert = ALERT_ANURIA;
        else if (s_meas.total_ml < 400.0f) s_day_alert = ALERT_OLIGURIA;
        else if (s_meas.total_ml > 2500.0f) s_day_alert = ALERT_POLYURIA;
        else s_day_alert = ALERT_NONE;
        ESP_LOGI(TAG, "首个24小时尿量=%.1fml，结论=%d",
                 (double)s_meas.total_ml, (int)s_day_alert);
    }

    storage_retry(now_ms);

    /* ③ 每到新一分钟：往U盘写一条 */
    uint32_t mins = (uint32_t)(s_elapsed_ms / 60000);
    if (mins > s_last_min) {
        s_last_min = mins;
        S3_record_write(mins);      /* U盘: 每分钟一条 */
    }
}

void S3_clear(uint64_t now_ms)
{
    urine_meas_reset(&s_meas);
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_acc_tick   = now_ms;
    s_last_ml    = 0.0f;
    s_day_alert  = ALERT_NONE;
    s_day_locked = false;
}

void S3_resume(uint64_t now_ms)
{
    /* 暂停时已结算过计时, 继续时仅重置记账基准 */
    s_acc_tick = now_ms;
    s_meas.rate_mlh = 0.0f;
    /* s_last_ml / s_last_min 保持不变，暂停期间未采集液滴。 */
}

void S3_pause(uint64_t now_ms)
{
    /* 结算计时到暂停瞬间, 并把 4 个数值冻结在屏上(状态由 S4 刷成 PAUSE) */
    s_elapsed_ms += now_ms - s_acc_tick;
    s_acc_tick    = now_ms;
    s_meas.rate_mlh = 0.0f;
    S3_push_values();
}

const urine_meas_t *S3_meas(void)
{
    return &s_meas;
}

/* ===== 状态入口 ===== */
void S3_enter(void)
{
    ESP_LOGI(TAG, "进入 S3 测量中");

    /* 进来先整体刷一次屏：清空"上次状态"缓存强制推一次状态，
       让屏幕立刻和当前数据对齐(否则从 S4 回来时状态可能停在上一个状态的文本) */
    s_last_status[0] = '\0';
    S3_push_ui();
    s_ui_tick = timesvc_now_ms();
}

/* 每周期入口(monitor 20ms 调一次)：采集 → 解算 → 推屏(200ms 节流) */
void S3_run(void)
{
    uint64_t now = timesvc_now_ms();

    drop_poll();                        /* ① 采集: 把 ISR 里的滴间隔搬进 Time[] */
    S3_update(now);                     /* ② 解算: 计时/尿量/瞬时速率/缺水/满袋/按分钟快照 */

    /* ③ 推屏: 200ms 节流(20ms 周期下约 5Hz)，避免把串口屏喂爆 */
    if (now - s_ui_tick >= S3_UI_PERIOD_MS) {
        s_ui_tick = now;
        S3_push_ui();
    }
}
