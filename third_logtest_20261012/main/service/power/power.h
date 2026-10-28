#ifndef SERVICE_POWER_H
#define SERVICE_POWER_H

/*
 * service 层：电池电量业务。
 *
 * 【分层意义】
 *   driver/battery 只给"电池端电压 mV"；本模块负责把电压翻译成电量百分比，
 *   并做滤波去抖，供 app 层显示。app 层只调 power_percent()，不接触 ADC。
 *
 * 【电压→电量】按 126280 锂聚合物电池(10000mAh，标称 3.8V，
 *   保护板 4.20V 充电截止 / 2.7V 放电保护)的典型放电曲线做分段线性插值。
 *   ⚠ 表中数值是按电池资料推算的，**必须实机标定后定稿**。
 *
 * 【为什么滤波】带负载时电压会随瞬时电流波动，直接查表会让百分比来回跳。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 建议的采样周期(ms)：app 每秒调一次 power_poll() */
#define POWER_POLL_PERIOD_MS   1000u

/* 初始化(内部会初始化 driver/battery)。 */
err_t power_init(void);

/* 周期采样并更新电量：读电压 → 一阶滤波 → 查表得百分比。
 * 上层按 POWER_POLL_PERIOD_MS 调用即可。 */
void power_poll(void);

/* 当前电量 0~100；尚未采到有效值时为 -1(屏上显示 "--")。
 *
 * ⚠ 本值**单调不增**：一旦显示出某个百分比，之后只会持平或下降，绝不会因电压
 *   波动而回升(避免"越用越多"的观感)。副作用是换电池/充电后电量虽真实回升，
 *   显示仍会停在水位上——重新上电即可恢复。 */
int power_percent(void);

/* 最近一次滤波后的电池电压 mV；未就绪时为 0。 */
uint32_t power_mv(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_POWER_H：头文件保护结束 */
