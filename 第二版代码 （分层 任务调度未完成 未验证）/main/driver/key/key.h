#ifndef DRIVER_KEY_H
#define DRIVER_KEY_H

/* driver 层：4 路按键扫描(轮询 + 消抖 + 短按/长按)。
 * 引脚见 bsp/pin_map.h (BSP_PIN_KEY1..KEY4)，低有效(按下接地) + 内部上拉。
 * 需上层周期性调用 key_scan()；检测到事件时回调 key_cb_t。 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KEY_NONE = 0,
    KEY1_PRESS, KEY1_LONG,   /* KEY1 短按 / 长按 */
    KEY2_PRESS, KEY2_LONG,
    KEY3_PRESS, KEY3_LONG,
    KEY4_PRESS, KEY4_LONG,
} key_event_t;

typedef void (*key_cb_t)(key_event_t ev);

/* 初始化 4 路按键 GPIO 并注册事件回调(cb 可为 NULL) */
err_t key_init(key_cb_t cb);

/* 供任务周期性调用(建议 10~20ms 一次)；内部做消抖/长短按判定并触发回调 */
err_t key_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_KEY_H */
