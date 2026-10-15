#ifndef DRIVER_WALLTIME_H
#define DRIVER_WALLTIME_H

/*
 * driver 层：墙上时间（hh:mm:ss）。
 *
 * 【为什么需要】记录与屏显要的是"绝对时间"，而 time_svc 只给单调毫秒。
 *
 * 【时间来源】正式方案由手机上位机下发对时（后续大概率走 WiFi）。
 *   上位机目前未接入，所以未对时状态下从 00:00:00 本地起算并正常走时；
 *   接入时只需调用 walltime_set_hms()，本模块与上层都不用改。
 *
 * 【走时方式】以 time_svc 的单调毫秒为基准累加，跨天自动对 86400 取模。
 */

#include "bsp/err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：清零并以当前时刻为计时起点(未同步，从 00:00:00 开始走)。 */
void walltime_init(void);

/* 上位机对时接口(预留给后续 WiFi 上位机)：
 * 用给定时刻重置基准，之后继续靠 time_svc 走时。
 * hh 允许 0~23，mm/ss 允许 0~59；越界时按 24h 回绕处理。 */
void walltime_set_hms(int hh, int mm, int ss);

/* 取当前墙上时间，格式 "hh:mm:ss"。buf 至少 9 字节。
 * 24 小时回绕，不会出现 24:xx。 */
void walltime_now_hms(char *buf, size_t len);

/* 取当前墙上时间对应的当日秒数 0..86399。 */
uint32_t walltime_now_sec(void);

/* 是否已被上位机对过时(1=已对时，0=本地起算)。 */
int walltime_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_WALLTIME_H：头文件保护结束 */
