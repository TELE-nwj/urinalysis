#ifndef APP_MONITOR_H
#define APP_MONITOR_H

#include "bsp/err.h"
#include "service/urine/urine.h"
#include "service/storage/storage.h"
#include <stdbool.h>

typedef enum {
    APP_IDLE = 0,
    APP_MEASURING,
    APP_PAUSED,
} app_state_t;

typedef enum {
    APP_COMMAND_START, APP_COMMAND_PAUSE, APP_COMMAND_RESET,
    APP_COMMAND_SYNC, APP_COMMAND_TOGGLE,
} app_command_type_t;

typedef struct {
    app_command_type_t type;
    uint64_t token;
    uint64_t expires_at_ms;
    uint8_t hour, minute, second;
} app_command_t;

typedef enum {
    APP_COMMAND_OK, APP_COMMAND_EXPIRED, APP_COMMAND_INVALID_ARGUMENT,
    APP_COMMAND_INTERNAL_ERROR,
} app_command_result_t;

typedef struct {
    uint64_t token;
    app_command_result_t result;
    app_state_t state;
    uint64_t state_seq;
} app_completion_t;

typedef void (*app_completion_cb_t)(const app_completion_t *, void *ctx);

typedef struct {
    uint64_t seq, state_seq, sample_uptime_ms;
    float total_ml, rate_mlh;
    uint64_t total_drops, elapsed_s;
    app_state_t state;
    char alert;
    int battery_percent;
    storage_state_t storage_state;
    bool storage_checked;
    uint64_t storage_checked_uptime_ms;
    char clock_hms[9];
    bool time_synced;
} app_snapshot_t;

/* 非阻塞入队；monitor 是唯一的执行者。 */
err_t app_monitor_submit_command(const app_command_t *command);
bool app_monitor_get_snapshot(app_snapshot_t *snapshot);
/* 启动期间注册一次。ctx 必须在设备整个生命周期内保持有效。 */
err_t app_monitor_set_completion_callback(app_completion_cb_t cb, void *ctx);

/* 按键扫描和状态处理都在 monitor 任务内执行。 */
err_t app_monitor_start(void);
void app_monitor_reset(void);
app_state_t app_monitor_state(void);
const urine_meas_t *app_monitor_get(void);
void app_monitor_dump(const char *note);

#endif
