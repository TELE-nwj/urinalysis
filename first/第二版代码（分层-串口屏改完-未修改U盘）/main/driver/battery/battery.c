#include "battery.h"
#include "esp_log.h"

static const char *TAG = "battery";

err_t battery_init(void)
{
    ESP_LOGI(TAG, "battery driver stub (TODO: 接入 ADC 采样)");
    return ERR_NOT_SUP;
}

err_t battery_read_percent(int *percent)
{
    (void)percent;
    return ERR_NOT_SUP;
}

err_t battery_low_flag(void)
{
    return 0;
}
