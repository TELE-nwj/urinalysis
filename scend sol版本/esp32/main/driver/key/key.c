#include "key.h"
#include "bsp/pin_map.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "key";

#define DEBOUNCE_SAMPLES   3        /* 电平须连续 N 次一致才认(软件消抖) */

typedef struct {
    uint8_t raw;      /* 本次采样电平(0=释放, 1=按下) */
    uint8_t stable;   /* 消抖后的稳定电平 */
    uint8_t cnt;      /* 连续相同采样计数 */
} key_slot_t;

static key_cb_t   s_cb = NULL;
static key_slot_t s_key[KEY_COUNT];

static const gpio_num_t s_pins[KEY_COUNT] = {
    BSP_PIN_KEY1, BSP_PIN_KEY2
};

err_t key_init(key_cb_t cb)
{
    s_cb = cb;

    gpio_config_t io = {
        .pin_bit_mask = 0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < KEY_COUNT; i++) {
        io.pin_bit_mask |= (1ULL << s_pins[i]);
        s_key[i].raw    = 0;      /* 上拉: 默认高电平 = 释放 */
        s_key[i].stable = 0;
        s_key[i].cnt    = 0;
    }

    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    ESP_LOGI(TAG, "%u keys ready: GPIO%d/GPIO%d", (unsigned)KEY_COUNT,
             s_pins[0], s_pins[1]);
    return ERR_OK;
}

err_t key_scan(void)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        int     lvl     = gpio_get_level(s_pins[i]);
        uint8_t pressed = (lvl == 0) ? 1 : 0;      /* 按下 = 低电平 */

        /* 消抖：连续相同采样达到阈值才切换稳定态 */
        if (pressed == s_key[i].raw) {
            if (s_key[i].cnt < DEBOUNCE_SAMPLES) s_key[i].cnt++;
        } else {
            s_key[i].raw = pressed;
            s_key[i].cnt = 1;
        }

        /* 稳定态发生变化才动作；按住期间 raw==stable，不会重复触发 */
        if (s_key[i].cnt >= DEBOUNCE_SAMPLES &&
            s_key[i].raw != s_key[i].stable) {
            s_key[i].stable = s_key[i].raw;
            if (s_key[i].stable && s_cb) {         /* 稳定按下: 触发一次短按 */
                s_cb((key_event_t)(KEY1_PRESS + i));
            }
        }
    }
    return ERR_OK;
}
