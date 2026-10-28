#ifndef APP_MONITOR_H
#define APP_MONITOR_H

#include "bsp/err.h"
#include "service/urine/urine.h"

typedef enum {
    APP_IDLE = 0,
    APP_MEASURING,
    APP_PAUSED,
} app_state_t;

/* 按键扫描和状态处理都在 monitor 任务内执行。 */
err_t app_monitor_start(void);
void app_monitor_reset(void);
app_state_t app_monitor_state(void);
const urine_meas_t *app_monitor_get(void);
void app_monitor_dump(const char *note);

#endif
