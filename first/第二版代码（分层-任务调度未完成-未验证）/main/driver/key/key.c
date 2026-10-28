#include "key.h"
#include "bsp/pin_map.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "key";

#define KEY_CNT            4
#define DEBOUNCE_SAMPLES   3        /* 需连续 N 次相同电平才切换(消抖) */
#define LONG_PRESS_MS      1500     /* 长按判定时间 */

typedef struct {
    uint8_t  raw;          /* 本次采样电平(0=释放,1=按下) */
    uint8_t  stable;       /* 滤波后稳定电平 */
    uint8_t  cnt;          /* 连续相同采样计数 */
    uint8_t  long_sent;    /* 本次按下是否已触发过长按 */
    TickType_t down_tick;  /* 按下时刻 */
} key_slot_t;

static key_cb_t s_cb = NULL;
static key_slot_t s_key[KEY_CNT];

static const gpio_num_t s_pins[KEY_CNT] = {
    BSP_PIN_KEY1, BSP_PIN_KEY2, BSP_PIN_KEY3, BSP_PIN_KEY4
};

/* 按键按下低有效，内部上拉，默认高电平(释放) */
static void fire_event(uint8_t idx, uint8_t is_long)
{
    if (!s_cb) {
        return;
    }
    /* KEY1_PRESS=1, KEY1_LONG=2, KEY2_PRESS=3, ... */
    key_event_t ev = (key_event_t)(KEY1_PRESS + idx * 2 + (is_long ? 1 : 0));
    s_cb(ev);
}

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
    for (int i = 0; i < KEY_CNT; i++) {
        io.pin_bit_mask |= (1ULL << s_pins[i]);
        s_key[i].raw = s_key[i].stable = 0;   /* 初始视为释放 */
        s_key[i].cnt = 0;
        s_key[i].long_sent = 0;
        s_key[i].down_tick = 0;
    }
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    ESP_LOGI(TAG, "4 keys ready: GPIO%d/%d/%d/%d", s_pins[0], s_pins[1],
             s_pins[2], s_pins[3]);
    return ERR_OK;
}

err_t key_scan(void)
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < KEY_CNT; i++) {
        int lvl = gpio_get_level(s_pins[i]);
        uint8_t pressed = (lvl == 0) ? 1 : 0;   /* 按下=低 */

        /* 消抖滤波：连续相同采样达到阈值才切换稳定态 */
        if (pressed == s_key[i].raw) {
            if (s_key[i].cnt < DEBOUNCE_SAMPLES) {
                s_key[i].cnt++;
            }
        } else {
            s_key[i].raw = pressed;
            s_key[i].cnt = 1;
        }

        if (s_key[i].cnt >= DEBOUNCE_SAMPLES &&
            s_key[i].raw != s_key[i].stable) {
            s_key[i].stable = s_key[i].raw;
            if (s_key[i].stable) {                 /* 稳定按下：记录起点 */
                s_key[i].down_tick = now;
                s_key[i].long_sent = 0;
            } else {                                /* 稳定释放：判定短按 */
                if (!s_key[i].long_sent) {
                    fire_event((uint8_t)i, 0);      /* 短按 */
                }
            }
        }

        /* 长按判定 */
        if (s_key[i].stable && !s_key[i].long_sent &&
            (now - s_key[i].down_tick) >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
            s_key[i].long_sent = 1;
            fire_event((uint8_t)i, 1);              /* 长按 */
        }
    }
    return ERR_OK;
}
