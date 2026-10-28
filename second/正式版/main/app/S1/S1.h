#ifndef APP_S1_H
#define APP_S1_H

/*
 * S1 复位待登录
 *   · 进入动作：把屏上状态刷成 LOGIN。
 *   · "收到 login → 转 S2" 的判断统一在 app/monitor 的 on_cmd() 里。
 */

#ifdef __cplusplus
extern "C" {
#endif

void S1_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S1_H */
