#include "monitor.h"
#include "service/screen/screen.h"
#include "service/record/record.h"
#include "driver/drop/drop.h"
#include "driver/time_svc/time_svc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "monitor";

/* 主循环周期(ms) */
#define LOOP_MS   20

/* ============================================================
 * 测量状态机（任务调度）
 *   IDLE    : 空闲(未开始 / 复位后)。不计数、不计时。
 *   RUNNING : 测量中。读滴/累计/计时/按分钟记录。
 *   PAUSED  : 暂停。停止累计与计时，可 继续/复位/保存。
 * 由串口屏按钮回传驱动：START(开始/继续) / PAUSE / RESET / SAVE。
 * ============================================================ */
typedef enum {
    MEAS_IDLE = 0,
    MEAS_RUNNING,
    MEAS_PAUSED,
} meas_state_t;

static meas_state_t s_state = MEAS_IDLE;

/* 本层持有的“解算+判断”结果(仅测量中由 urine_update 更新) */
static urine_meas_t s_meas;

/* 会话计时(ms)：仅测量中累加；暂停冻结、复位归零 */
static uint64_t s_elapsed_ms = 0;
static uint64_t s_acc_tick    = 0;   /* 上一次记账时刻 */
static uint32_t s_last_min    = 0;   /* 已记录的分钟序号 */

/* 当前状态文本(报警优先级) */
static const char *cur_status(void)
{
    if (s_meas.flag_bag_full)  return "BAG FULL";
    if (s_meas.flag_low_urine) return "LOW URINE";
    return "READY";
}

/* 把会话计时格式化成 mm:ss */
static void fmt_elapsed(char *buf, size_t sz)
{
    uint32_t sec = (uint32_t)(s_elapsed_ms / 1000);
    snprintf(buf, sz, "%02lu:%02lu",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

/* 把当前解算结果推送到串口屏(5 个量) */
static void push_ui(void)
{
    char ts[16];
    screen_show_urine(s_meas.total_ml);        /* ① 累计尿量 ml */
    screen_show_rate(s_meas.rate_mlh);         /* ② 实时速率 ml/h */
    screen_show_drops(s_meas.total_drops);     /* ③ 累计滴数 */
    fmt_elapsed(ts, sizeof(ts));               /* ④ 会话计时 mm:ss */
    screen_show_time(ts);
    screen_show_status(cur_status());          /* ⑤ 状态/报警 */
}

/* 记一条“第 minute 分钟”的快照到 record 缓存 */
static void record_snapshot(uint32_t minute)
{
    rec_rec_t r;
    memset(&r, 0, sizeof(r));
    r.minute    = minute;
    r.ml        = s_meas.total_ml;
    r.rate_mlh  = s_meas.rate_mlh;
    r.drops     = s_meas.total_drops;
    snprintf(r.status, sizeof(r.status), "%s", cur_status());
    record_push(&r);
}

/* ====== 串口屏按钮回传 → 测量状态机 ====== */
static void on_screen_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_START:                       /* 开始 / 继续 */
        if (s_state == MEAS_IDLE) {             /* 空闲→全新测量 */
            record_reset();                     /* 清空上次记录 */
            urine_meas_reset(&s_meas);
            s_elapsed_ms = 0;
            s_last_min   = 0;
            ESP_LOGI(TAG, "measure START (fresh)");
        } else if (s_state == MEAS_PAUSED) {    /* 暂停→继续 */
            ESP_LOGI(TAG, "measure CONTINUE");
        } else {
            break;                              /* 测量中, 忽略 */
        }
        s_state    = MEAS_RUNNING;
        s_acc_tick = timesvc_now_ms();
        record_snapshot(0);                     /* 记第 0 分钟 */
        push_ui();
        break;

    case SCREEN_EV_PAUSE:                       /* 暂停(仅测量中) */
        if (s_state == MEAS_RUNNING) {
            s_elapsed_ms += timesvc_now_ms() - s_acc_tick;  /* 结算到暂停瞬间 */
            s_state = MEAS_PAUSED;
            ESP_LOGI(TAG, "measure PAUSED");
        }
        break;

    case SCREEN_EV_RESET:                       /* 复位(仅暂停态有效) */
        if (s_state == MEAS_PAUSED) {
            record_save();                      /* 自动把本次记录写入U盘 */
            record_reset();                     /* 清空缓存 */
            urine_meas_reset(&s_meas);
            s_elapsed_ms = 0;
            s_last_min   = 0;
            s_state = MEAS_IDLE;
            push_ui();
            ESP_LOGI(TAG, "measure RESET (auto-saved)");
        } else {
            ESP_LOGW(TAG, "复位仅允许在暂停态");
        }
        break;

    case SCREEN_EV_SAVE:                        /* 暂停态手动保存到U盘 */
        if (s_state == MEAS_PAUSED) {
            record_save();
        }
        break;

    default:
        break;
    }
}

/* ====== 主循环：只在测量态读滴/计时/按分钟记录 ====== */
static void monitor_task(void *arg)
{
    (void)arg;
    drop_stats_t st;
    uint32_t prev_total = 0;   /* 上次读到的总滴数，用于取差值 */

    for (;;) {
        uint64_t now = timesvc_now_ms();

        if (s_state == MEAS_RUNNING) {
            /* ① 推进会话计时 */
            s_elapsed_ms += now - s_acc_tick;
            s_acc_tick    = now;

            /* ② 读液滴增量，喂给尿量解算 */
            uint32_t delta = 0;
            if (drop_get_stats(&st) == ERR_OK && st.total_count != prev_total) {
                delta = st.total_count - prev_total;
                prev_total = st.total_count;
            }
            urine_update(&s_meas, delta, now);
            if (delta > 0) {
                ESP_LOGI(TAG, "滴+%u 累计=%u滴 %.1fml %.1fml/h",
                         delta, s_meas.total_drops,
                         s_meas.total_ml, s_meas.rate_mlh);
            }

            /* ③ 每到新一分钟记一条快照 */
            uint32_t mins = (uint32_t)(s_elapsed_ms / 60000);
            if (mins > s_last_min) {
                s_last_min = mins;
                record_snapshot(mins);
            }
        } else {
            /* 非测量态：同步 prev_total，避免恢复测量时把暂停期间积压的滴一次算入 */
            if (drop_get_stats(&st) == ERR_OK) {
                prev_total = st.total_count;
            }
        }

        push_ui();                     /* 串口屏实时显示 */
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

/* ====== 启动 ====== */
err_t app_monitor_start(void)
{
    urine_meas_init(&s_meas);
    record_init();                    /* 记录缓存清零 */
    s_state = MEAS_IDLE;

    /* 串口屏按钮回传 → 测量状态机 */
    screen_set_event_cb(on_screen_event);

    BaseType_t ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    return (ok == pdPASS) ? ERR_OK : ERR_FAIL;
}

/* 强制复位(外部调用, 等价于在暂停态按复位) */
void app_monitor_reset(void)
{
    record_save();
    record_reset();
    urine_meas_reset(&s_meas);
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_state      = MEAS_IDLE;
    push_ui();
}

const urine_meas_t *app_monitor_get(void)
{
    return &s_meas;
}

