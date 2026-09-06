#ifndef SERVICE_TIME_SVC_H
#define SERVICE_TIME_SVC_H

/* service 层：时间/时间戳服务。
 * 目前基于开机计时(esp_timer)；接入 RTC 后可换真实时间实现。 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void timesvc_init(void);
uint64_t timesvc_now_ms(void);                 /* 开机以来毫秒 */
void timesvc_stamp(char *buf, size_t len);     /* 形如 "T+12345s" 或 RTC 时间 */

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_TIME_SVC_H */
