#include "mobile.h"
#include "app/monitor/monitor.h"
#include "service/mobile/mobile.h"
#include "service/clock/clock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static char s_device_id[65], s_ssid[33], s_password[64];
static service_mobile_config_t s_config;
static bool s_initialized, s_tick_running;
static TaskHandle_t s_tick_task;
static portMUX_TYPE s_mobile_lock = portMUX_INITIALIZER_UNLOCKED;

static err_t mobile_submit(const service_mobile_request_t *request, void *ctx)
{
    (void)ctx;
    if (!request) return ERR_PARAM;
    app_command_type_t type;
    switch (request->command) {
    case SERVICE_MOBILE_START: type = APP_COMMAND_START; break;
    case SERVICE_MOBILE_PAUSE: type = APP_COMMAND_PAUSE; break;
    case SERVICE_MOBILE_RESET: type = APP_COMMAND_RESET; break;
    case SERVICE_MOBILE_TIME_SYNC: type = APP_COMMAND_SYNC; break;
    default: return ERR_PARAM;
    }
    const app_command_t command = {
        .type = type, .token = request->token,
        .expires_at_ms = request->expires_uptime_ms,
        .hour = request->hour, .minute = request->minute, .second = request->second,
    };
    return app_monitor_submit_command(&command);
}

static bool snapshot(service_mobile_snapshot_t *out, void *ctx)
{
    (void)ctx;
    app_snapshot_t value;
    if (!out || !app_monitor_get_snapshot(&value)) return false;
    *out = (service_mobile_snapshot_t){
        .seq = value.seq, .state_seq = value.state_seq,
        .sample_uptime_ms = value.sample_uptime_ms,
        .total_ml = value.total_ml, .rate_mlh = value.rate_mlh,
        .total_drops = value.total_drops, .elapsed_s = value.elapsed_s,
        .state = (service_mobile_state_t)value.state, .alert = value.alert,
        .battery_percent = value.battery_percent,
        .storage_state = (service_mobile_storage_t)value.storage_state,
        .storage_checked_known = value.storage_checked,
        .storage_checked_uptime_ms = value.storage_checked_uptime_ms,
        .time_synced = value.time_synced,
    };
    memcpy(out->clock_hms, value.clock_hms, sizeof(out->clock_hms));
    return true;
}

static uint64_t monotonic_ms(void *ctx)
{
    (void)ctx;
    return clock_ms();
}

static void completion(const app_completion_t *value, void *ctx)
{
    (void)ctx;
    service_mobile_result_t result;
    switch (value->result) {
    case APP_COMMAND_OK: result = SERVICE_MOBILE_OK; break;
    case APP_COMMAND_EXPIRED: result = SERVICE_MOBILE_EXPIRED; break;
    case APP_COMMAND_INVALID_ARGUMENT: result = SERVICE_MOBILE_INVALID_ARGUMENT; break;
    default: result = SERVICE_MOBILE_INTERNAL_ERROR; break;
    }
    service_mobile_complete(value->token, result, (service_mobile_state_t)value->state, value->state_seq);
}

static void tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        portENTER_CRITICAL(&s_mobile_lock);
        bool running = s_tick_running;
        portEXIT_CRITICAL(&s_mobile_lock);
        if (!running) break;
        service_mobile_tick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    portENTER_CRITICAL(&s_mobile_lock);
    s_tick_task = NULL;
    portEXIT_CRITICAL(&s_mobile_lock);
    vTaskDelete(NULL);
}

err_t app_mobile_init(const app_mobile_config_t *config)
{
    if (!config || !config->device_id || !config->ap_ssid || !config->ap_password) return ERR_PARAM;
    size_t id_len = strlen(config->device_id), ssid_len = strlen(config->ap_ssid), pass_len = strlen(config->ap_password);
    if (!id_len || id_len > 64 || !ssid_len || ssid_len > 32 || pass_len > 63 ||
        (pass_len && pass_len < 8) || !config->ap_channel || config->ap_channel > 14 ||
        !config->ap_max_connections || config->ap_max_connections > 10) return ERR_PARAM;
    for (size_t i = 0; i < id_len; i++) {
        char c = config->device_id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return ERR_PARAM;
    }
    if (s_initialized) return ERR_BUSY;
    err_t result = app_monitor_set_completion_callback(completion, NULL);
    if (result != ERR_OK) return result;
    memcpy(s_device_id, config->device_id, id_len + 1);
    memcpy(s_ssid, config->ap_ssid, ssid_len + 1);
    memcpy(s_password, config->ap_password, pass_len + 1);
    s_config = (service_mobile_config_t){
        .device_id = s_device_id, .ap_ssid = s_ssid, .ap_password = s_password,
        .ap_channel = config->ap_channel, .ap_max_connections = config->ap_max_connections,
        .submit = mobile_submit, .snapshot = snapshot, .monotonic_ms = monotonic_ms,
    };
    s_initialized = true;
    return ERR_OK;
}

err_t app_mobile_start(void)
{
    if (!s_initialized) return ERR_NOT_READY;
    app_snapshot_t ready;
    if (!app_monitor_get_snapshot(&ready)) return ERR_NOT_READY;
    if (s_tick_task) return ERR_BUSY;
    err_t result = service_mobile_start(&s_config);
    if (result != ERR_OK) return result;
    s_tick_running = true;
    if (xTaskCreate(tick_task, "mobile", 4096, NULL, 4, &s_tick_task) != pdPASS) {
        s_tick_running = false;
        service_mobile_stop();
        return ERR_NO_MEM;
    }
    return ERR_OK;
}

void app_mobile_stop(void)
{
    portENTER_CRITICAL(&s_mobile_lock);
    s_tick_running = false;
    portEXIT_CRITICAL(&s_mobile_lock);
    for (;;) {
        portENTER_CRITICAL(&s_mobile_lock);
        bool running = s_tick_task != NULL;
        portEXIT_CRITICAL(&s_mobile_lock);
        if (!running) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    service_mobile_stop();
}
