#ifndef DRIVER_TIME_SVC_H
#define DRIVER_TIME_SVC_H

/* driver 层：单调毫秒时基（上电起算）。
 *
 * 【职责边界】只回答"从开机到现在过了多少毫秒"，不涉及日历/墙上时间；
 *   需要 hh:mm:ss 这类绝对时间请用 driver/walltime（它以本模块为计时基准）。
 *   会话计时（测量用了多久）也基于本模块，由上层决定暂停/继续怎么加减。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void timesvc_init(void);
uint64_t timesvc_now_ms(void);                 /* 开机以来毫秒，单调不减 */

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_TIME_SVC_H */
