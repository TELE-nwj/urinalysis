#ifndef DRIVER_KEY_H
#define DRIVER_KEY_H

/* driver 层：2 路按键扫描(轮询 + 软件消抖，**只出"短按"事件**)。
 * 引脚见 bsp/pin_map.h (BSP_PIN_KEY1 / BSP_PIN_KEY2)，低有效(按下接地) + 内部上拉。
 * 无长按、无连发：电平稳定为"按下"时回调一次，按住期间不重复触发，松手不影响。
 * 需上层周期性调用 key_scan()（建议 10~20ms）。 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_COUNT   2u      /* 按键路数 */

typedef enum {
    KEY_NONE      = 0,
    KEY1_PRESS    = 1,      /* 按键 1 短按 */
    KEY2_PRESS    = 2,      /* 按键 2 短按 */
} key_event_t;

typedef void (*key_cb_t)(key_event_t ev);

/* 初始化 2 路按键 GPIO(输入+内部上拉)并注册事件回调(cb 可为 NULL) */
err_t key_init(key_cb_t cb);

/* 供任务周期性调用(建议 10~20ms 一次)；内部消抖，稳定按下即触发一次回调 */
err_t key_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_KEY_H：头文件保护结束 */
