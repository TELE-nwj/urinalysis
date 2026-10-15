#ifndef SERVICE_URINE_H
#define SERVICE_URINE_H

/*
 * service 层：尿量计量与方式 A 分析（纯计算），并代理液滴采集硬件。
 *
 * 【分层意义】app 层不直接接触 driver/drop：
 *   - 采集控制(urine_capture_*)与周期搬运/解算(urine_poll)都收在本模块；
 *   - 会话结果由模块内部持有，app 通过 urine_get() 读只读指针。
 *
 * 【定位】采集在 driver/drop(间隔 Time[])，本模块做"分析"：
 *   - 累计：从 driver 的"永不清零 total"减去本次测量起点，得会话累计滴数/尿量；
 *   - 速率：取最近一次有效相邻间隔换算 ml/h，7s 无新滴归零；
 *   - 方式 A 缺水(Lack)：窗口满(255)时统计 Time[1..255] 中 >DROP_DRY_SEC 的个数，
 *     若 >DROP_DRY_CNT 判定缺水(断流)；
 *   - 尿袋满：累计尿量 >= URINE_BAG_FULL_ML。
 */

#include <stdint.h>
#include <stdbool.h>

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
 * 一次"解算 + 判断"的完整结果（与屏幕显示的各个量对应）。
 */
typedef struct {
    /* ---------- 计量结果 ---------- */
    uint32_t total_drops;   /* 会话累计滴数(从本次测量起点算起) */
    float    total_ml;      /* 会话累计尿量 ml */
    float    rate_mlh;      /* 实时速率 ml/h(最近一次有效间隔换算) */

    /* ---------- 判断/报警结果（1=触发） ---------- */
    uint8_t  flag_dry;      /* 缺水/断流(方式 A Lack) */
    uint8_t  flag_bag_full; /* 尿袋满(累计 >= URINE_BAG_FULL_ML) */
} urine_meas_t;

/* ============ 采集控制（封装 driver/drop） ============ */

/* 初始化液滴采集硬件(配置 GPIO 中断)。由 main 在启动阶段调用一次。 */
void urine_capture_init(void);

/* 开始 / 继续采集：清掉旧间隔并重新打开滴落中断。 */
void urine_capture_arm(void);

/* 暂停采集：关闭滴落中断(期间落下的滴被丢弃；0.5s 消抖下几毫秒关闭无影响)。 */
void urine_capture_hold(void);

/* 写盘时临时关闭液滴中断，返回原开关状态；不清间隔和待处理液滴。 */
bool urine_capture_suspend(void);
/* 恢复临时关闭前的状态；待开始/暂停状态不会被误开启。 */
void urine_capture_restore(bool enabled);

/* 只清间隔缓冲，不动中断开关(复位流程里配合 hold 使用)。 */
void urine_capture_clear(void);

/* ============ 会话数据 ============ */

/* 清零会话结果并重记"测量起点"(开始新测量 / 复位时调用)。
 * 调用前应先 urine_capture_hold()，避免清点与中断写点竞争。 */
void urine_reset(void);

/* 周期调用(由 app 的周期任务执行，例如 monitor 的 20ms 循环)：
 *   ① 把 ISR 里的滴间隔搬进 Time[]；
 *   ② 推进计时无关的纯解算(累计/速率/缺水/满袋)。
 * 会话计时由 app 层自己累加，本模块不管。 */
void urine_poll(void);

/* 只读的当前解算结果。 */
const urine_meas_t *urine_get(void);

/* 把实时速率清成 0(暂停冻结、继续起算时用，避免屏上停在旧读数)。 */
void urine_rate_zero(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_URINE_H：头文件保护结束 */
