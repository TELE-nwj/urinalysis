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
#define BSP_PIN_CH376_SDI     GPIO_NUM_41   /* MOSI -> CH376 SDI */
#define BSP_PIN_CH376_SDO     GPIO_NUM_40   /* MISO <- CH376 SDO */
#define BSP_PIN_CH376_INT     GPIO_NUM_1    /* 中断(低有效)，0=不用 */

/* ==================== 液滴检测 ==================== */
#define BSP_PIN_DROP_DETECT   GPIO_NUM_10   /* 每滴一个上升沿 */

/* ==================== 按键 (4路) —— 本板不使用 ====================
 * 新板(正式版)取消按键，GPIO4/5/8/9 已释放。
 * driver/key 仍在仓库里，但未编入本板构建；换板要用时恢复本段并把
 * driver/key/key.c 加回 main/CMakeLists.txt 的 SRCS。
 * 原定义：KEY1=GPIO4(原START)  KEY2=GPIO5(原SELECT)  KEY3=GPIO8  KEY4=GPIO9
 *         （低有效 / 内部上拉 / 3 次采样消抖 / 1.5s 长按）
 */

/* ==================== 串口屏 (淘晶驰 TJC/Nextion 类, UART1) ================= */
#define BSP_UART_SCREEN_NUM   UART_NUM_1    /* 使用 UART1 (避开 UART0 日志口) */
#define BSP_PIN_UART_SCREEN_TX GPIO_NUM_17  /* MCU TX -> 屏 RX */
#define BSP_PIN_UART_SCREEN_RX GPIO_NUM_18  /* MCU RX <- 屏 TX */
#define BSP_UART_SCREEN_BAUD  115200        /* 波特率(按屏设置) */

/* ==================== INA228 电量计 (I2C) ====================
 * 本板用 INA228 经 I2C 上报电池电压/电流/电量，取代原 ESP32 ADC 电池检测
 * （故 GPIO7 的 ADC 用途已取消，该脚现为空闲）。
 * 引脚从原按键/ADC 释放出来的 GPIO 里选取，与旧原理图上的 INA228 接法一致。
 * 注：写 INA228 驱动时需把 esp_driver_i2c 加入 main/CMakeLists.txt 的 REQUIRES，
 *     并 include "driver/i2c_master.h"。
 */
#define BSP_I2C_INA_PORT      0             /* I2C 端口号 (I2C0) */
#define BSP_PIN_INA_SDA       GPIO_NUM_5    /* I2C SDA -> INA228 SDA */
#define BSP_PIN_INA_SCL       GPIO_NUM_6    /* I2C SCL -> INA228 SCL */
#define BSP_I2C_INA_FREQ_HZ   400000        /* 400kHz 快速模式 */
#define BSP_PIN_INA_ALERT     GPIO_NUM_4    /* INA228 ALERT(低有效), 0=不用 */
#define BSP_INA228_ADDR       0x40          /* 7 位地址 (A1=A0 接 GND) */

#ifdef __cplusplus
}
#endif

#endif /* BSP_PIN_MAP_H */
