#ifndef DRIVER_BATTERY_H
#define DRIVER_BATTERY_H

/*
 * driver 层：锂电池电压采集（ADC 分压）。
 *
 * 【定位】只负责"把 ADC 读数变成电池端电压"，不做电量百分比换算 —— 百分比是业务
 *   语义，放在 service/power。上层(service/power)只调 battery_read_mv()。
 *
 * 【硬件】分压网络见 bsp/pin_map.h：采样节点 = V_bat/2，故 V_bat = V_adc × 2。
 *   采样脚 GPIO7 = ADC1_CH6，12dB 衰减。
 *
 * 【为什么多次采样】ADC 单次读数噪声较大，内部连采并取平均后再还原电压。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单次 battery_read_mv() 内部连采的次数(取平均用) */
#define BATTERY_SAMPLE_TIMES   16u

/* 初始化 ADC 单元/通道与校准曲线。返回 ERR_OK 表示可以开始采样。
 * 校准数据缺失时退化为线性换算并告警，仍返回 ERR_OK(不阻断启动)。 */
err_t battery_init(void);

/* 读取电池端电压，单位 mV。
 * 内部连采 BATTERY_SAMPLE_TIMES 次取平均，再按分压比还原到电池端。
 * mv 为输出参数；返回非 ERR_OK 时不修改 *mv。 */
err_t battery_read_mv(uint32_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_BATTERY_H：头文件保护结束 */
