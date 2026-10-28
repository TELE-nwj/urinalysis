#include "drop.h"
#include "bsp/pin_map.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "drop";

static portMUX_TYPE s_spin = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t s_total_count = 0;
static volatile uint64_t s_last_ts_us = 0;
static volatile uint32_t s_debounce_us = 30000; /* 30ms 消抖 */

/* 中断服务：只做极短操作(记时间、消抖、累加)，绝不打印/做慢事 */
static void IRAM_ATTR drop_isr(void *arg)
{
    (void)arg;
    uint64_t now = esp_timer_get_time();
    uint64_t last;
    portENTER_CRITICAL(&s_spin);
    last = s_last_ts_us;
    if (now - last >= s_debounce_us) {   /* 有效滴 */
        s_last_ts_us = now;
        s_total_count++;
    }
    portEXIT_CRITICAL(&s_spin);
}

err_t drop_init(void)
{
    gpio_config_t cfg = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BSP_PIN_DROP_DETECT),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config fail");
        return ERR_FAIL;
    }
    if (gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1) != ESP_OK) {
        /* 若已安装则忽略 */
    }
    gpio_isr_handler_add(BSP_PIN_DROP_DETECT, drop_isr, NULL);
    ESP_LOGI(TAG, "drop detect init on GPIO%d", BSP_PIN_DROP_DETECT);
    return ERR_OK;
}

void drop_reset(void)
{
    portENTER_CRITICAL(&s_spin);
    s_total_count = 0;
    s_last_ts_us = 0;
    portEXIT_CRITICAL(&s_spin);
}

err_t drop_get_stats(drop_stats_t *out)
{
    if (!out) return ERR_PARAM;
    portENTER_CRITICAL(&s_spin);
    out->total_count = s_total_count;
    out->last_ts_us = s_last_ts_us;
    out->debounce_us = s_debounce_us;
    portEXIT_CRITICAL(&s_spin);
    return ERR_OK;
}

void drop_set_debounce_us(uint32_t us)
{
    if (us == 0) us = 1;
    portENTER_CRITICAL(&s_spin);
    s_debounce_us = us;
    portEXIT_CRITICAL(&s_spin);
}
