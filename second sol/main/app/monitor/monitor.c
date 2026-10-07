#include "monitor.h"
#include "app/S3/S3.h"
#include "driver/drop/drop.h"
#include "driver/key/key.h"
#include "driver/time_svc/time_svc.h"
#include "service/screen/screen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "monitor";
#define LOOP_MS 20u

static app_state_t s_state = APP_IDLE;

static const char *state_name(app_state_t state)
{
    switch (state) {
    case APP_IDLE:      return "待开始";
    case APP_MEASURING: return "测量中";
    case APP_PAUSED:    return "暂停";
    default:            return "?";
    }
}

void app_monitor_dump(const char *note)
{
    const urine_meas_t *m = S3_meas();
    ESP_LOGI(TAG, "[%s] %s 尿量=%.1fml 速率=%.1fml/h 滴数=%lu 满袋=%u",
             note ? note : "快照", state_name(s_state),
             (double)m->total_ml, (double)m->rate_mlh,
             (unsigned long)m->total_drops, (unsigned)m->flag_bag_full);
}

static void set_state(app_state_t state)
{
    if (state == s_state) return;
    ESP_LOGI(TAG, "状态 %s -> %s", state_name(s_state), state_name(state));
    s_state = state;
    if (state == APP_IDLE) {
        screen_show_status("待开始");
    } else if (state == APP_PAUSED) {
        screen_show_status("暂停");
    } else {
        S3_enter();
    }
}

void app_monitor_reset(void)
{
    if (s_state == APP_IDLE) return;
    drop_irq_disable();
    drop_reset_interval();
    S3_storage_stop();
    S3_clear(timesvc_now_ms());
    S3_push_values();
    set_state(APP_IDLE);
}

static void on_key(key_event_t event)
{
    if (event == KEY2_PRESS) {
        app_monitor_reset();
        return;
    }
    if (event != KEY1_PRESS) return;

    if (s_state == APP_IDLE) {
        /* 先分配记录文件，再以实际开始采集的时刻为计时起点。 */
        S3_storage_start();
        drop_irq_disable();
        S3_reset_data(timesvc_now_ms());
        drop_reset_interval();
        drop_irq_enable();
        set_state(APP_MEASURING);
    } else if (s_state == APP_MEASURING) {
        /* 先结算停止瞬间的数据，随后关闭滴落中断。 */
        uint64_t now = timesvc_now_ms();
        drop_poll();
        S3_update(now);
        drop_irq_disable();
        drop_reset_interval();
        S3_pause(now);
        set_state(APP_PAUSED);
    } else {
        S3_resume(timesvc_now_ms());
        drop_reset_interval();
        drop_irq_enable();
        set_state(APP_MEASURING);
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        key_scan();
        if (s_state == APP_MEASURING) S3_run();
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

err_t app_monitor_start(void)
{
    drop_irq_disable();
    if (key_init(on_key) != ERR_OK) return ERR_FAIL;
    s_state = APP_IDLE;
    S3_clear(timesvc_now_ms());
    S3_push_values();
    screen_show_status("待开始");
    BaseType_t ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ERR_OK : ERR_FAIL;
}

app_state_t app_monitor_state(void)
{
    return s_state;
}

const urine_meas_t *app_monitor_get(void)
{
    return S3_meas();
}
