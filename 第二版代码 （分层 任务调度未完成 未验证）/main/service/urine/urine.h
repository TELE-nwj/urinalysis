#ifndef SERVICE_URINE_H
#define SERVICE_URINE_H

/*
 * service 层：尿量计量与判断核心（纯计算，无硬件、无任务）。
 *
 * 【定位】把所有“要解算的数据（含判断/报警结果）”收进一个结构体 urine_meas_t，
 * 由统一的 urine_update() 在每次喂入“新增滴数 + 当前时刻”后自动算好。
 * app 层(monitor) 只负责周期喂数据、然后把结构体里的结果拿去显示/落盘，不再自己算。
 *
 * 【本次范围】只做不依赖 RTC 的部分：累计滴数/尿量、实时速率(ml/h)、
 * 少尿判断、尿袋满判断。分时(时段)尿量、24h 统计留待接入 RTC 后再加。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 可调参数（阈值/窗口，按技术要求） ============ */
#define URINE_DROP_ML_DEFAULT   0.05f    /* ml/滴，需按实际滴嘴标定 */
#define URINE_LOW_RATE_MLH      17.0f    /* 少尿阈值：<17 ml/h（≈<0.4L/24h） */
#define URINE_BAG_FULL_ML       2000.0f  /* 尿袋满阈值：累计 ≥2000 ml(2L) */
#define URINE_RATE_WINDOW_MS    60000u   /* 速率统计窗口：60s 结算一次 */

/*
 * 一次“解算 + 判断”的完整结果。
 * monitor 拿到这份结果后，直接读出各项去驱动串口屏 / 写 U盘 即可。
 */
typedef struct {
    /* ---------- 计量结果 ---------- */
    uint32_t total_drops;   /* 累计有效滴数 */
    float    total_ml;      /* 累计尿量 ml = total_drops × drop_ml */
    float    rate_mlh;      /* 实时速率 ml/h（窗口内结算） */

    /* ---------- 判断/报警结果（1=触发，0=正常） ---------- */
    uint8_t  flag_low_urine;/* 少尿：有尿但速率 < URINE_LOW_RATE_MLH */
    uint8_t  flag_bag_full; /* 尿袋满：累计尿量 >= URINE_BAG_FULL_ML */
} urine_meas_t;

/* 初始化/清零一份结果结构体 */
void urine_meas_init(urine_meas_t *m);

/*
 * 周期喂入数据并自动解算。
 *   delta_drops : 自上次以来新增的有效滴数（可为 0）
 *   now_ms      : 当前开机毫秒（timesvc_now_ms()），供速率窗口计时
 * 内部会：累计滴数/毫升 → 按窗口结算速率 → 做少尿/尿袋满判断。
 * 要求上层(monitor) 每周期都调用（即使 0 滴），以便速率随时间回落。
 */
void urine_update(urine_meas_t *m, uint32_t delta_drops, uint64_t now_ms);

/* 清零全部结果与内部速率窗口（换病人/确认时调用） */
void urine_meas_reset(urine_meas_t *m);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_URINE_H */
