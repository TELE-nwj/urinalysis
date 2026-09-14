#ifndef APP_S4_H
#define APP_S4_H

/*
 * S4 测量暂停
 *   · 进入动作：把屏上状态刷成 PAUSE（4 个数值已由 S3_pause 冻结在屏上）。
 *   · "start 继续 → S3"、"reset → S1" 的判断统一在 app/monitor 的 on_cmd() 里。
 */

#ifdef __cplusplus
extern "C" {
#endif

void S4_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S4_H */
