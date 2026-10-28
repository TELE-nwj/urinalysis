#ifndef DRIVER_DROP_H
#define DRIVER_DROP_H

/*
 * driver 层：液滴检测。
 * 每个液滴落下 -> GPIO 上升沿中断 -> 内部累加计数/记录时间。
 * 本模块只负责"收脉冲、记时间"，不处理业务。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total_count;      /* 累计有效滴数 */
    uint64_t last_ts_us;       /* 最近一次有效滴的时间戳(us) */
    uint32_t debounce_us;      /* 消抖窗口(us) */
} drop_stats_t;

/* 初始化：配置 GPIO 中断并安装 ISR 服务。调用一次。 */
err_t drop_init(void);

/* 复位计数(换病人/清零时调用) */
void drop_reset(void);

/* 读当前统计(线程安全地取快照) */
err_t drop_get_stats(drop_stats_t *out);

/* 配置消抖窗口(默认即可，可调) */
void drop_set_debounce_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_DROP_H */
