#ifndef BSP_PIN_MAP_H
#define BSP_PIN_MAP_H

/*
 * bsp 层：所有板级引脚定义统一收敛到本文件。
 * 换板子/换接线时只改这里，上层驱动代码不用动。
 */

#include "driver/gpio.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== CH376 U盘 (SPI) ==================== */
#define BSP_PIN_CH376_SCS     GPIO_NUM_2
#define BSP_PIN_CH376_SDI     GPIO_NUM_41   /* MOSI -> CH376 SDI */
#define BSP_PIN_CH376_SDO     GPIO_NUM_40   /* MISO <- CH376 SDO */
#define BSP_PIN_CH376_SCK     GPIO_NUM_42
#define BSP_PIN_CH376_INT     GPIO_NUM_1    /* 中断(低有效)，0=不用 */

/* ==================== 液滴检测 ==================== */
#define BSP_PIN_DROP_DETECT   GPIO_NUM_10   /* 每滴一个上升沿 */

/* ==================== 按键 (4路, 低有效/内部上拉) ==================== */
#define BSP_PIN_KEY1          GPIO_NUM_4    /* KEY1 (原 START) */
#define BSP_PIN_KEY2          GPIO_NUM_5    /* KEY2 (原 SELECT) */
#define BSP_PIN_KEY3          GPIO_NUM_8    /* KEY3 */
#define BSP_PIN_KEY4          GPIO_NUM_9    /* KEY4 */

/* ==================== 串口屏 (淘晶驰 TJC/Nextion 类, UART1) ================= */
#define BSP_UART_SCREEN_NUM   UART_NUM_1    /* 使用 UART1 (避开 UART0 日志口) */
#define BSP_PIN_UART_SCREEN_TX GPIO_NUM_17  /* MCU TX -> 屏 RX */
#define BSP_PIN_UART_SCREEN_RX GPIO_NUM_18  /* MCU RX <- 屏 TX */
#define BSP_UART_SCREEN_BAUD  115200        /* 波特率(按屏设置) */

/* ==================== 电池电量检测 (ADC) ==================== */
#define BSP_ADC_BATTERY_UNIT  ADC_UNIT_1    /* ADC 单元 (TODO) */
#define BSP_PIN_BATTERY_ADC   GPIO_NUM_7    /* 电池电压采样脚 (TODO) */

#ifdef __cplusplus
}
#endif

#endif /* BSP_PIN_MAP_H */
