/* Compile-only integration harness: all production app/service/driver sources.
 * No simulator business is inserted into production. Not flashed or executed. */
#include "app/mobile/mobile.h"
#include "app/monitor/monitor.h"
#include "service/clock/clock.h"
#include "service/urine/urine.h"
#include "service/storage/storage.h"
#include "service/power/power.h"
#include "service/screen/screen.h"
void app_main(void)
{
    clock_init();
    urine_capture_init();
    (void)screen_init();
    (void)power_init();
    (void)storage_init();
    const app_mobile_config_t config = {
        .device_id = "NIAIO-build-check", .ap_ssid = "NIAIO-Monitor",
        .ap_password = "niaio-v1-test", .ap_channel = 1, .ap_max_connections = 1,
    };
    (void)app_mobile_init(&config);
    if (app_monitor_start() == ERR_OK) (void)app_mobile_start();
}
