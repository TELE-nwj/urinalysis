#ifndef APP_MOBILE_H
#define APP_MOBILE_H
#include "bsp/err.h"
#include <stdint.h>
typedef struct {
    const char *device_id;
    const char *ap_ssid, *ap_password;
    uint8_t ap_channel, ap_max_connections;
} app_mobile_config_t;
/* 仅限启动阶段：在 app_monitor_start() 之前登记永久性的监控完成回调。
 * 字符串会被拷贝，允许使用栈上局部变量。 */
err_t app_mobile_init(const app_mobile_config_t *config);
/* 在 monitor 之后启动。失败时本地测量仍然可用。 */
err_t app_mobile_start(void);
/* 仅限生命周期拥有者，且在回调之外调用：先汇合 tick 任务再停止服务。 */
void app_mobile_stop(void);
#endif
