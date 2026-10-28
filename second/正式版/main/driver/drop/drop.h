#ifndef DRIVER_DROP_H
#define DRIVER_DROP_H

/*
 * driver 层：液滴信号采集（方式 A，仅硬件采集）。
 *
 * 职责：只负责"收滴、记时间、把相邻滴的间隔存进环形 Time[]"，不做业务判断。
 *   - ISR(GPIO10 上升沿)：0.5s 消抖，记录每滴时刻、累计滴数。
 *   - drop_poll()（任务上下文周期调用）：把相邻间隔写入内部 Time[256]（弃用
 *     Time[0]，用下标 1..255），攒满一轮(255 个间隔)后给出"窗口完成"事件。
 *
 * 分析（缺水/Lack、滑动平均速率、累计尿量等）放在上层 service/urine，
 * 由 urine 通过本层导出的接口读取间隔与窗口事件。阈值见下方宏(方式A默认)。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 方式 A 参数(默认，可按需改) ---- */
#define DROP_DEBOUNCE_US    500000u   /* 消抖窗口 0.5s */
#define DROP_WIN_SIZE       255       /* 一轮统计的间隔数 */
#define DROP_RING_SIZE      256       /* Time[] 容量(下标 1..255) */

/* 初始化：配置 GPIO10 上升沿中断并安装 ISR 服务(调用一次) */
err_t drop_init(void);

/* 周期性调用(任务上下文，如 monitor 主循环)：排空 ISR、把相邻间隔写入 Time[]、
   维护累计与"窗口完成"事件。 */
void drop_poll(void);

/* 临时关闭/恢复液滴 GPIO 中断。
 * 用途：写 U 盘等"不希望被液滴中断打断"的临界区里包住 SPI 操作。
 * 注意：关中断期间落下的滴会被丢弃(0.5s 消抖下，几毫秒的关中断基本无影响)。 */
void drop_irq_disable(void);
void drop_irq_enable(void);

/* 累计有效滴数(永不清零，供上层算增量) */
uint32_t drop_total(void);

/* 读取 Time[i]，i 取值 1..255，返回该相邻间隔秒数(0~255，>255 饱和 255)。
   未填写的槽位返回 0。 */
uint8_t drop_interval_sec(uint16_t i);

/* 最近一次写入的 Time[] 下标(1..255)；尚无间隔时返回 0。
   供上层(urine)从"最新"往旧回溯做滑动平均速率。 */
uint16_t drop_newest(void);

/* 最近一次有效相邻间隔(us) */
uint64_t drop_last_interval_us(void);

/* 最近一滴的时刻(esp_timer 微秒)。0 = 尚无滴。
 * 用途：上层判断"多久没有新滴了"(例如屏上速率超时归零)。 */
uint64_t drop_last_time_us(void);

/* "窗口完成"事件：本轮 255 个间隔刚攒满返回 1(并清除该标志)，否则 0。
   上层(urine)据此跑一次方式 A 缺水判定。 */
uint8_t drop_window_done(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_DROP_H */
