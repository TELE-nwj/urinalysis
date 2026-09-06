#ifndef DRIVER_SCREEN_UART_H
#define DRIVER_SCREEN_UART_H

/*
 * driver 层：串口屏 UART 通讯。
 *
 * 职责：负责串口屏(UART)的底层收发，只做"发字节/收字节"，不解析任何屏幕协议。
 * 协议组帧、页面、控件刷新、回传解码等属于上层(service/screen)，由上层负责。
 *
 * 依据 F:\ESP32\niaio\project 中 UART1 参考实现改写，引脚收敛到 bsp/pin_map.h。
 */

#include "bsp/err.h"
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 收到一帧数据时回调(data 指向本次收到的缓冲，len 为字节数)。
 * 注意：回调运行在内部 RX 任务上下文，勿做耗时/阻塞操作，可拷贝数据后交业务处理。 */
typedef void (*screen_uart_rx_cb_t)(const uint8_t *data, size_t len);

/* 初始化串口屏所用 UART(引脚/波特率见 bsp/pin_map.h) */
err_t screen_uart_init(void);

/* 发送 len 字节(阻塞发送完) */
err_t screen_uart_send(const void *data, size_t len);

/* 发送一个 C 字符串(不含结尾 \0) */
err_t screen_uart_send_str(const char *str);

/* 同步读取：返回实际读到的字节数；超时返回 ERR_TIMEOUT */
int screen_uart_read(uint8_t *buf, size_t size, TickType_t timeout);

/* 可选：注册接收回调。注册后内部 RX 任务会把收到的数据回调给上层。
 * 传 NULL 表示关闭接收任务。 */
void screen_uart_set_rx_cb(screen_uart_rx_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SCREEN_UART_H */
