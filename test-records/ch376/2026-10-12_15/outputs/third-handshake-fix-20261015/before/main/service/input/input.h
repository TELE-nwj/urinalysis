#ifndef SERVICE_INPUT_H
#define SERVICE_INPUT_H

/*
 * service 层：按键服务 —— app 层唯一的人机输入入口。
 *
 * 【分层意义】GPIO 配置、采样、消抖都在 driver/key 里完成；本模块把它包装成
 *   "短按事件回调"，app 层不必知道按键接在哪个 GPIO、如何消抖。
 *
 * 【事件语义】只有"稳定按下"这一个事件：按下时触发一次，按住期间不连发，
 *   松开后再按才会再次触发。长按/双击不在本版范围内。
 */

#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_KEY1 = 0,   /* 按键 1：开始 / 暂停 / 继续 */
    INPUT_KEY2,       /* 按键 2：复位 */
    INPUT_KEY_COUNT
} input_key_t;

/* 短按事件回调。运行在调用 input_poll() 的任务上下文，
 * 因此实现里不要做耗时或阻塞操作。 */
typedef void (*input_cb_t)(input_key_t key);

/* 初始化两个按键 GPIO 并登记回调(main 启动阶段调一次)。
 * 只登记不注册也会正常返回，只是没有事件。 */
err_t input_init(input_cb_t cb);

/* 周期扫描(由 app 的周期任务调用，例如 monitor 的 20ms 循环)：
 * 内部完成采样与消抖，确认短按时回调 cb。 */
void input_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_INPUT_H：头文件保护结束 */
