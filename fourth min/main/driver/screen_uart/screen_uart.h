#ifndef DRIVER_SCREEN_UART_H
#define DRIVER_SCREEN_UART_H

/*
 * driver 层：串口屏 UART 通讯（单向：单片机 → 屏）。
 *
 * 【职责】只做"发字节"，不解析任何屏幕协议。协议组帧、控件刷新属于上层
 *   (service/screen)，由上层负责。
 *
 * 【本版设计】按"单片机只发不收"的要求：
 *   - 删除了接收回调与 uart_rx 任务，固件从不调用 uart_read_bytes；
 *   - 驱动仍会分配 RX 缓冲——ESP-IDF 要求 rx_buffer_size 必须大于 UART FIFO，
 *     传 0 会被 uart_driver_install() 直接拒绝，所以只能分配但**永远不读**；
 *     屏端发来的数据填满这个缓冲后由驱动丢弃(详见 screen_uart.c 文件头)；
 *   - 物理 RX 线仍可保留(见引脚表)，但固件不读它。
 *
 * 依据 F:\ESP32\niaio\project 中 UART1 参考实现改写，引脚收敛到 bsp/pin_map.h。
 */

#include "bsp/err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化串口屏所用 UART(引脚/波特率见 bsp/pin_map.h)。只发不收。 */
err_t screen_uart_init(void);

/* 发送 len 字节。
 * 注意：UART 装了 TX 环形缓冲(见 screen_uart.c 的 TX_BUF_SIZE)，所以本函数是
 * "把数据写进环形缓冲即返回"，不保证此刻已从线上发完——上层无需关心，
 * 缓冲内的数据由 UART 驱动在后台发出。 */
err_t screen_uart_send(const void *data, size_t len);

/* 发送一个 C 字符串(不含结尾 \0) */
err_t screen_uart_send_str(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SCREEN_UART_H：头文件保护结束 */
