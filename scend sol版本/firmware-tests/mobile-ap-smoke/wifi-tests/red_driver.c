/* Reproducible missing-feature fixture for the original RED step. */
#include "driver/wifi/wifi.h"
err_t wifi_ap_start(const wifi_ap_config_t *config) { (void)config; return ERR_NOT_SUP; }
void wifi_ap_stop(void) {}
bool wifi_ap_is_running(void) { return false; }
