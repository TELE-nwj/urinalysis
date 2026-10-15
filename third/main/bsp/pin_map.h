#ifndef BSP_PIN_MAP_H
#define BSP_PIN_MAP_H

/*
 * bsp 层：所有板级引脚定义统一收敛到本文件。
 * 换板子/换接线时只改这里，上层驱动代码不用动。
 */

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"   /* 电池分压采样用的 ADC_UNIT_x / ADC_CHANNEL_x */

#ifdef __cplusplus
extern "C" { 
#endif

/* ==================== CH376 U盘/SD卡 (软SPI) ====================
 * 驱动是 GPIO 位翻转的软件 SPI，任意可用 GPIO 都行。
 * 本板沿用旧方案，与「正式版1nwj / 主控」原理图一致：
 *   SCS=IO2  SCK=IO42  SDI=IO41  SDO=IO40  INT=IO1
 *
 * ⚠ 本板模块 = ESP32-S3-WROOM-1-N16R8 (ESP32-S3R8，八线 PSRAM)，以下不可用：
 *   · IO35 / IO36 / IO37：官方手册写明已连到八线 PSRAM，不能他用
 *   · IO26~IO32：模块内部 flash/PSRAM，未引出
 *   其他注意：IO0/IO3/IO45/IO46 是 strapping 脚；IO43/IO44 是日志串口；
 *             IO19/IO20 是原生 USB；都已避开。
 *   说明：IO40~IO42 同时也是 JTAG 脚，占用于此就没有引脚 JTAG；
 *        如需调试可用 IO19/IO20 的 USB-Serial-JTAG（本板已引出）。
 */
#define BSP_PIN_CH376_SCS     GPIO_NUM_2    /* 片选 */
#define BSP_PIN_CH376_SCK     GPIO_NUM_42   /* 时钟 */
#define BSP_PIN_CH376_SDI     GPIO_NUM_41   /* 说明：MOSI -> CH376 SDI */
#define BSP_PIN_CH376_SDO     GPIO_NUM_40   /* 说明：MISO <- CH376 SDO */
#define BSP_PIN_CH376_INT     GPIO_NUM_1    /* 中断(低有效)，0=不用 */

/* ==================== 液滴检测 ==================== */
#define BSP_PIN_DROP_DETECT   GPIO_NUM_10   /* 每滴一个上升沿 */

/* ==================== 按键 (2 路) ====================
 * 低有效(按下接地) + 内部上拉；只做软件消抖，只出"短按"事件(无长按/连发)。
 * KEY1/KEY2 沿用原 KEY3/KEY4 的引脚。GPIO4/GPIO5/GPIO6 现空闲
 * （原 INA228 电量计方案已取消，见下方电池检测段）。
 */
#define BSP_PIN_KEY1          GPIO_NUM_8    /* 按键 1 */
#define BSP_PIN_KEY2          GPIO_NUM_9    /* 按键 2 */

/* ==================== 串口屏 (淘晶驰 TJC/Nextion 类, UART1) ================= */
#define BSP_UART_SCREEN_NUM   UART_NUM_1    /* 使用 UART1 (避开 UART0 日志口) */
#define BSP_PIN_UART_SCREEN_TX GPIO_NUM_17  /* MCU TX -> 屏 RX */
#define BSP_PIN_UART_SCREEN_RX GPIO_NUM_18  /* MCU RX <- 屏 TX */
#define BSP_UART_SCREEN_BAUD  115200        /* 波特率(按屏设置) */

/* ==================== 电池电压检测 (ADC 分压) ====================
 * 电池电压经板上电阻分压网络接到 ADC 引脚，由 driver/battery 采样后换算电量。
 * 注：原 INA228 电量计(I2C, 占 GPIO4/5/6)方案已取消，该三脚现为空闲。
 *     分压后的电压不得超过 ADC 量程(见 ADC_ATTEN_DB_12 ≈ 3.3V)。
 *
 * 分压网络(实物原理图)：
 *     BAT ──R59(200k)──┬──R61(1k)── ADC(GPIO7)
 *                      │
 *                    R60(200k)      C13(100nF)
 *                      │               │
 *                     GND             GND
 *
 *   采样节点电压 = V_bat × R60/(R59+R60) = V_bat × 200/400 = V_bat/2
 *   所以 V_bat = V_adc × DIV_NUM / DIV_DEN。
 *   R61(1k) 是串在 ADC 前的限流电阻，ADC 输入为高阻，不参与分压计算。
 *   满电 4.2V → 2.1V 入 ADC；放空 2.7V → 1.35V，均在 12dB 量程内。
 */
#define BSP_ADC_BATTERY_UNIT    ADC_UNIT_1        /* ADC 单元: ADC1 */
#define BSP_PIN_BATTERY_ADC     GPIO_NUM_7        /* 分压采样脚 */
#define BSP_BATTERY_ADC_CHANNEL ADC_CHANNEL_6     /* 说明：GPIO7 = ADC1_CH6 */
#define BSP_BATTERY_DIV_NUM     400u              /* R59+R60 (kΩ) */
#define BSP_BATTERY_DIV_DEN     200u              /* R60 (kΩ) */

#ifdef __cplusplus
}
#endif

#endif /* BSP_PIN_MAP_H：头文件保护结束 */
