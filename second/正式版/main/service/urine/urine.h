#ifndef SERVICE_URINE_H
#define SERVICE_URINE_H

/*
 * service 层：尿量计量与方式 A 分析（纯计算）。
 *
 * 【定位】采集在 driver/drop(间隔 Time[])，本模块做"分析"：
 *   - 累计：从 driver 的"永不清零 total"减去本次测量起点，得会话累计滴数/尿量；
 *   - 速率：取最近 URINE_AVG_N 个相邻间隔做滑动平均 → ml/h（替代旧的 60s 窗口）；
 *   - 方式 A 缺水(Lack)：窗口满(255)时统计 Time[1..255] 中 >DROP_DRY_SEC 的个数，
 *     若 >DROP_DRY_CNT 判定缺水(断流)；
 *   - 尿袋满：累计尿量 >= URINE_BAG_FULL_ML。
 * app 层(monitor) 只周期调 urine_update()，把结果拿去显示/落盘。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 可调参数（方式 A / 计量） ============ */
#define URINE_DROP_ML_DEFAULT   0.5f   /* ml/滴，需按实际标定 */
#define URINE_BAG_FULL_ML       2000.0f /* 尿袋满阈值 ml */
#define URINE_AVG_N             2u      /* 速率滑动平均取的间隔个数 */
#define URINE_DRY_SEC           4u      /* 方式A: 单间隔 >4s 视为过慢 */
#define URINE_DRY_CNT           100u    /* 方式A: 一轮255中 >100 个过慢 → 缺水 */
#define URINE_RATE_ZERO_MS      7000u   /* 距最近一滴超过 7s → 实时速率显示 0 */

/*
 * 一次"解算 + 判断"的完整结果（与屏幕 5 个量对应）。
 */
typedef struct {
    /* ---------- 计量结果 ---------- */
    uint32_t total_drops;   /* 会话累计滴数(从本次测量起点算起) */
    float    total_ml;      /* 会话累计尿量 ml */
    float    rate_mlh;      /* 实时速率 ml/h(最近若干间隔滑动平均) */

    /* ---------- 判断/报警结果（1=触发） ---------- */
    uint8_t  flag_dry;      /* 缺水/断流(方式 A Lack) */
    uint8_t  flag_bag_full; /* 尿袋满(累计 >= URINE_BAG_FULL_ML) */
} urine_meas_t;

/* 初始化/清零：记录"测量起点"的 driver 累计(之后会话从 0 计) */
void urine_meas_init(urine_meas_t *m);

/*
 * 周期解算(monitor 每循环调用；需先 drop_poll())。内部：
 *   累计增量 → 滑动平均速率 → (窗口满时)方式A缺水判定 → 尿袋满判定。
 */
void urine_update(urine_meas_t *m);

/* 清零(换病人/复位/开始新测量)：重记测量起点 */
void urine_meas_reset(urine_meas_t *m);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_URINE_H */
