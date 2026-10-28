#include "urine.h"
#include "driver/drop/drop.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "urine";

/* ===== 模块内部持有的一次测量结果 =====
 * app 层不自己存 urine_meas_t，只通过 urine_get() 读只读指针：
 * 这样"累计/速率/缺水/满袋"四个量只有一个写者(本模块)，不存在两份数据不同步的问题。 */
static urine_meas_t s_meas;

/* 本次测量在 driver 累计里的"起点"：会话滴数 = drop_total() - s_base。
 * driver 的累计数永不清零，所以每次开始测量/复位都要重记起点，
 * 否则会把空闲期、暂停期落下的滴算进本次会话。 */
static uint32_t s_base = 0;

/* ============ 采集控制（把 driver/drop 收在本模块内） ============ */

void urine_capture_init(void)
{
    if (drop_init() != ERR_OK) {
        ESP_LOGE(TAG, "液滴采集初始化失败(GPIO 中断)，将收不到滴");
    }
}

void urine_capture_arm(void)
{
    /* drop.h 要求清间隔缓冲时中断必须是关的。调用者(monitor)通常已经 hold 过，
     * 这里再关一次是幂等的，纯粹防止以后有人在中断开着时误调本函数踩到竞争。 */
    drop_irq_disable();
    drop_reset_interval();      /* 开始/继续前清掉旧间隔与待处理队列 */
    drop_irq_enable();
}

void urine_capture_hold(void)
{
    /* 关中断：期间落下的滴被丢弃(0.5s 消抖下，几毫秒的关闭基本无影响) */
    drop_irq_disable();
}

void urine_capture_clear(void)
{
    /* 只清间隔缓冲、不动中断开关：复位流程里配合 urine_capture_hold() 使用 */
    drop_reset_interval();
}

/* ============ 会话数据 ============ */

void urine_reset(void)
{
    memset(&s_meas, 0, sizeof(s_meas));
    s_base = drop_total();      /* 重记测量起点 */
}

void urine_rate_zero(void)
{
    /* 暂停冻结、继续起算时调用，避免屏上停在旧读数 */
    s_meas.rate_mlh = 0.0f;
}

const urine_meas_t *urine_get(void)
{
    return &s_meas;
}

/* ===== 纯解算(与计时无关)：累计量 / 实时速率 / 方式A缺水 / 尿袋满 ===== */
static void urine_solve(void)
{
    /* ---------- 1) 会话累计滴数/尿量 ---------- */
    uint32_t tot = drop_total();
    if (tot < s_base) s_base = tot;      /* 防御：理论不会发生 */
    uint32_t session = tot - s_base;
    if (session != s_meas.total_drops) {
        uint32_t delta = session - s_meas.total_drops;
        s_meas.total_drops = session;
        s_meas.total_ml   += (float)delta * URINE_DROP_ML_DEFAULT;
    }

    /* ---------- 2) 速率：瞬时速率(最近一次有效相邻间隔) → ml/h ---------- */
    {
        uint64_t us   = drop_last_interval_us();   /* 最近一次间隔(µs), 每滴更新 */
        uint64_t last = drop_last_time_us();       /* 最近一滴的时刻 */
        uint64_t now  = (uint64_t)esp_timer_get_time();

        if (us > 0) {
            /* rate = 每滴ml / 间隔秒 × 3600 */
            s_meas.rate_mlh = 3600.0f * URINE_DROP_ML_DEFAULT / ((float)us / 1e6f);
        }
        /* us==0(尚无有效间隔)时保持旧值 */

        /* 距最近一滴超过 URINE_RATE_ZERO_MS 毫秒 → 认为是"没有新数据"，速率显示 0
           (否则屏上会一直停在最后一次的读数上) */
        if (last != 0 &&
            (now - last) > (uint64_t)URINE_RATE_ZERO_MS * 1000ULL) {
            s_meas.rate_mlh = 0.0f;
        }
    }

    /* ---------- 3) 方式 A 缺水(Lack)：窗口满时统计一轮 ---------- */
    if (drop_window_done()) {
        uint16_t slow = 0;
        for (uint16_t i = 1; i <= DROP_WIN_SIZE; i++) {
            if (drop_interval_sec(i) > URINE_DRY_SEC) slow++;
        }
        s_meas.flag_dry = (s_meas.total_drops > 0 && slow > URINE_DRY_CNT) ? 1 : 0;
    }

    /* ---------- 4) 尿袋满 ---------- */
    s_meas.flag_bag_full = (s_meas.total_ml >= URINE_BAG_FULL_ML) ? 1 : 0;
}

void urine_poll(void)
{
    drop_poll();        /* ① 把 ISR 里的滴间隔搬进 Time[] */
    urine_solve();      /* ② 推进解算(会话计时由 app 层自己累加，本模块不管) */
}
