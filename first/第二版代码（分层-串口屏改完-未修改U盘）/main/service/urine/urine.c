#include "urine.h"
#include <string.h>

/* ============================================================
 * 内部速率统计（模块内单实例）
 * 用“滑动窗口”累计滴数，每满 URINE_RATE_WINDOW_MS 结算一次速率。
 * 这样即使某次 feed 只来 0 滴，窗口也会随时间推进、速率会回落。
 * ============================================================ */
static uint32_t s_win_drops     = 0;   /* 当前窗口内累计滴数 */
static uint64_t s_win_start_ms  = 0;   /* 当前窗口起始时刻(ms) */

void urine_meas_init(urine_meas_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    s_win_drops    = 0;
    s_win_start_ms = 0;
}

void urine_update(urine_meas_t *m, uint32_t delta_drops, uint64_t now_ms)
{
    if (!m) return;

    /* ---------- 1) 累计尿量 ---------- */
    if (delta_drops > 0) {
        m->total_drops += delta_drops;
        m->total_ml    += (float)delta_drops * URINE_DROP_ML_DEFAULT;
    }

    /* ---------- 2) 速率结算（每满一个窗口算一次 ml/h） ---------- */
    if (s_win_drops == 0) {
        s_win_start_ms = now_ms;         /* 空窗口：从此刻重新计时 */
    }
    s_win_drops += delta_drops;

    uint64_t elapsed = now_ms - s_win_start_ms;
    if (elapsed >= URINE_RATE_WINDOW_MS) {
        /* rate = 窗口内滴数/时长(秒) × 3600s × 每滴ml */
        m->rate_mlh = ((float)s_win_drops / (float)elapsed)
                      * 3600.0f * URINE_DROP_ML_DEFAULT;
        s_win_drops    = 0;              /* 开启下一个窗口 */
        s_win_start_ms = now_ms;
    }

    /* ---------- 3) 判断/报警 ---------- */
    /* 少尿：确实有尿过，但当前速率低于阈值 */
    m->flag_low_urine = (m->total_drops > 0 && m->rate_mlh < URINE_LOW_RATE_MLH) ? 1 : 0;
    /* 尿袋满：累计尿量达到 2L */
    m->flag_bag_full  = (m->total_ml >= URINE_BAG_FULL_ML) ? 1 : 0;
}

void urine_meas_reset(urine_meas_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    s_win_drops    = 0;
    s_win_start_ms = 0;
}
