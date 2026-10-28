#ifndef APP_S2_H
#define APP_S2_H

/*
 * S2 已登录未开始测量
 *   · 进入动作：把屏上状态刷成 READY。
 *   · "收到 start → 转 S3" 的判断统一在 app/monitor 的 on_cmd() 里。
 */

#ifdef __cplusplus
extern "C" {
#endif

void S2_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S2_H */
