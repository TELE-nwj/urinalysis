#ifndef DRIVER_WIFI_H
#define DRIVER_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t channel;
    uint8_t max_connections;
} wifi_ap_config_t;

/* Task-context calls. Owns the WiFi radio until stop; an existing WiFi driver
 * or default AP netif is rejected. SSID: 1..32 bytes; password: NULL/empty (open), or 8..63
 * bytes (WPA2); channel: 1..13; max_connections: 1..10.
 * Credentials use RAM storage. NVS init failures never erase user data.
 * The shared default event loop and esp-netif stack remain initialized.
 * If SDK stop/deinit fails, ownership is retained for a later stop retry;
 * is_running stays true if SDK stop failed and start remains ERR_BUSY. */
err_t wifi_ap_start(const wifi_ap_config_t *config);
void wifi_ap_stop(void);
bool wifi_ap_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
