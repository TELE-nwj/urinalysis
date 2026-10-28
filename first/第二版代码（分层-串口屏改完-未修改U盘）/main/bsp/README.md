# bsp 层 — 板级支持包

> 本层存放板级/通用基础头文件：
> - `pin_map.h`：把**所有板级引脚/外设号**统一收敛在一处，换板/换接线只改这里。
> - `err.h`：通用错误码（`err_t`/`ERR_OK`…，由原 `common/err.h` 移入）。

## 引脚定义一览

| 外设 | 宏 | 值 | 说明 |
|---|---|---|---|
| CH376 片选 | `BSP_PIN_CH376_SCS` | GPIO2 | |
| CH376 MOSI | `BSP_PIN_CH376_SDI` | GPIO41 | MCU → CH376 SDI |
| CH376 MISO | `BSP_PIN_CH376_SDO` | GPIO40 | CH376 SDO → MCU |
| CH376 SCK | `BSP_PIN_CH376_SCK` | GPIO42 | |
| CH376 中断 | `BSP_PIN_CH376_INT` | GPIO1 | 低有效（0=不用） |
| 液滴检测 | `BSP_PIN_DROP_DETECT` | GPIO10 | 每滴一个上升沿 |
| KEY1..4 | `BSP_PIN_KEY1..4` | GPIO4/5/8/9 | 低有效 + 内部上拉 |
| 串口屏 UART 号 | `BSP_UART_SCREEN_NUM` | UART1 | 避开 UART0 日志口 |
| 串口屏 TX/RX | `BSP_PIN_UART_SCREEN_TX/RX` | GPIO17/18 | |
| 串口屏波特率 | `BSP_UART_SCREEN_BAUD` | 115200 | |
| 电池 ADC 单元 | `BSP_ADC_BATTERY_UNIT` | ADC_UNIT_1 | TODO |
| 电池采样脚 | `BSP_PIN_BATTERY_ADC` | GPIO7 | TODO |

> 蜂鸣器引脚 `BSP_PIN_BUZZER` 已随蜂鸣器模块一并删除。

## 使用约定
- driver 层通过 `#include "bsp/pin_map.h"` 读取所需宏，不写死任何引脚编号。
- 本文件包含 `driver/gpio.h`、`driver/uart.h` 等，作为对外统一的“板级信息”入口。
