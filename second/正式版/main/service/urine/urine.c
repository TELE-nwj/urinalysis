#include "urine.h"
#include "driver/drop/drop.h"
#include "esp_timer.h"
#include <string.h>

/* 本次测量在 driver 累计里的"起点"。会话滴数 = drop_total() - s_base。 */
static uint32_t s_base = 0;

static void set_base(void)
{
    s_base = drop_total();      /* 记录测量起点 */
}

void urine_meas_init(urine_meas_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    set_base();
}

void urine_meas_reset(urine_meas_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    set_base();                 /* 重新记起点，避免把之前空闲/暂停期的滴算入 */
}

void urine_update(urine_meas_t *m)
{
    if (!m) return;

    /* ---------- 1) 会话累计滴数/尿量 ---------- */
    uint32_t tot = drop_total();
    if (tot < s_base) s_base = tot;      /* 防御 */
    uint32_t session = tot - s_base;
    if (session != m->total_drops) {
        uint32_t delta = session - m->total_drops;
        m->total_drops = session;
        m->total_ml   += (float)delta * URINE_DROP_ML_DEFAULT;
    }

    /* ---------- 2) 速率：瞬时速率(最近一次有效相邻间隔) → ml/h ---------- */
    {
        uint64_t us   = drop_last_interval_us();   /* 最近一次间隔(µs), 每滴更新 */
        uint64_t last = drop_last_time_us();       /* 最近一滴的时刻 */
        uint64_t now  = (uint64_t)esp_timer_get_time();

        if (us > 0) {
            /* rate = 每滴ml / 间隔秒 × 3600 */
            m->rate_mlh = 3600.0f * URINE_DROP_ML_DEFAULT / ((float)us / 1e6f);
        }
        /* us==0(尚无有效间隔)时保持旧值 */

        /* 距最近一滴超过 URINE_RATE_ZERO_MS 毫秒 → 认为是"没有新数据"，速率显示 0
           (否则屏上会一直停在最后一次的读数上) */
        if (last != 0 &&
            (now - last) > (uint64_t)URINE_RATE_ZERO_MS * 1000ULL) {
            m->rate_mlh = 0.0f;
        }
    }

    /* ---------- 3) 方式 A 缺水(Lack)：窗口满时统计一轮 ---------- */
    if (drop_window_done()) {
        uint16_t slow = 0;
        for (uint16_t i = 1; i <= DROP_WIN_SIZE; i++) {
            if (drop_interval_sec(i) > URINE_DRY_SEC) slow++;
        }
        m->flag_dry = (m->total_drops > 0 && slow > URINE_DRY_CNT) ? 1 : 0;
    }

    /* ---------- 4) 尿袋满 ---------- */
    m->flag_bag_full = (m->total_ml >= URINE_BAG_FULL_ML) ? 1 : 0;
}
