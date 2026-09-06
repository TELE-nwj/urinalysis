#include "monitor.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "driver/drop/drop.h"
#include "driver/time_svc/time_svc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "monitor";

/* 主循环周期 / 写盘周期(ms) */
#define LOOP_MS   20
#define STORE_MS  10000

/* ============================================================
 * 本层持有的“解算+判断”结果。
 * 所有字段由 service/urine 的 urine_update() 每周期算好，
 * monitor 只读它，不再自己算。
 * ============================================================ */
static urine_meas_t s_meas;

/* ============================================================
 * 把“解算结果”五个数据一次性推送到串口屏。
 *   ① 累计尿量 ml   → n0  (screen_show_urine)
 *   ② 实时速率 ml/h → n1  (screen_show_rate)
 *   ③ 累计滴数      → n2  (screen_show_drops)
 *   ④ 计时/运行时间 → t1  (screen_show_time)
 *   ⑤ 状态/报警     → t0  (screen_show_status)
 * 控件名若与 HMI 不符，改 service/screen/screen.c 顶部的 CTRL_* 宏即可。
 * ============================================================ */
static void push_ui(void)
{
    char ts[32];

    screen_show_urine(s_meas.total_ml);        /* ① 累计尿量 ml */
    screen_show_rate(s_meas.rate_mlh);         /* ② 实时速率 ml/h */

    screen_show_drops(s_meas.total_drops);     /* ③ 累计滴数 */

    timesvc_stamp(ts, sizeof(ts));             /* ④ 计时(开机运行时间) */
    screen_show_time(ts);

    /* ⑤ 状态/报警（合成一句话，按优先级取当前状态） */
    if (s_meas.flag_bag_full) {
        screen_show_status("BAG FULL");
    } else if (s_meas.flag_low_urine) {
        screen_show_status("LOW URINE");
    } else {
        screen_show_status("READY");
    }
}

/* ====== 串口屏回传事件 → 主状态机(方向②: 屏→单片机) ====== */
static void on_screen_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_CLEAR_REQUEST:            /* 屏上“清零” */
        urine_meas_reset(&s_meas);
        screen_show_urine(s_meas.total_ml);
        ESP_LOGI(TAG, "screen: clear");
        break;
    case SCREEN_EV_PAUSE_REQUEST:            /* 屏上“暂停”(示例状态) */
        screen_show_status("PAUSE");
        ESP_LOGI(TAG, "screen: pause");
        break;
    default:
        break;
    }
}

/* ====== 主循环：喂数据 → 拿结果 → 外发 ====== */
static void monitor_task(void *arg)
{
    (void)arg;
    drop_stats_t st;
    uint32_t prev_total = 0;      /* 上次读到的总滴数，用于取差值 */
    uint64_t last_store = 0;      /* 上次写盘时刻 */

    for (;;) {
        uint64_t now = timesvc_now_ms();      /* 当前时刻(ms) */

        /* ① 读液滴，得到自上次以来的“新增滴数”(可为0) */
        uint32_t delta = 0;
        if (drop_get_stats(&st) == ERR_OK && st.total_count != prev_total) {
            delta = st.total_count - prev_total;
            prev_total = st.total_count;
        }

        /* ② 喂给 urine 解算：内部累计/算速率/做判断，整份结果写进 s_meas */
        urine_update(&s_meas, delta, now);
        if (delta > 0) {
            ESP_LOGI(TAG, "滴+%u, 累计%u滴=%.1f ml, %.1f ml/h",
                     delta, s_meas.total_drops, s_meas.total_ml, s_meas.rate_mlh);
        }

        /* ③ 把五个数据(mL/速率/滴数/计时/状态)推送到串口屏 */
        push_ui();

        /* ④ 每 10s 按结果写一条到 U盘 */
        if (now - last_store >= STORE_MS) {
            last_store = now;
            char line[96];
            char ts[32];
            timesvc_stamp(ts, sizeof(ts));
            snprintf(line, sizeof(line), "%s drops=%lu ml=%.1f rate=%.1f\n",
                     ts,
                     (unsigned long)s_meas.total_drops,
                     s_meas.total_ml,
                     (double)s_meas.rate_mlh);
            if (storage_write_text(line) != ERR_OK) {
                ESP_LOGW(TAG, "写U盘失败(检查磁盘/连接)");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

/* ====== 启动 ====== */
err_t app_monitor_start(void)
{
    /* 清零解算结果 */
    urine_meas_init(&s_meas);

    /* 串口屏回传事件 → 本状态机 */
    screen_set_event_cb(on_screen_event);

    BaseType_t ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    return (ok == pdPASS) ? ERR_OK : ERR_FAIL;
}

void app_monitor_reset(void)
{
    urine_meas_reset(&s_meas);
    screen_show_urine(s_meas.total_ml);
}

const urine_meas_t *app_monitor_get(void)
{
    return &s_meas;
}
