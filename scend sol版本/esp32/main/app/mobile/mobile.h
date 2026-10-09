#ifndef APP_MOBILE_H
#define APP_MOBILE_H
#include "bsp/err.h"
#include <stdint.h>
typedef struct {
    const char *device_id;
    const char *ap_ssid, *ap_password;
    uint8_t ap_channel, ap_max_connections;
} app_mobile_config_t;
/* Startup only: register the permanent monitor completion callback before
 * app_monitor_start(). Strings are copied and may be stack-local. */
err_t app_mobile_init(const app_mobile_config_t *config);
/* Start after monitor. Failure leaves local measurement available. */
err_t app_mobile_start(void);
/* Lifecycle owner only, outside callbacks: joins tick task before stopping service. */
void app_mobile_stop(void);
#endif
