#ifndef APP_MONITOR_H
#define APP_MONITOR_H

/*
 * app 层：主状态机 / 监控中枢（唯一主动循环，尽量保持"薄"）。
 *
 * 【设计】monitor 不做任何换算与判断——所有"要解算的数据(含判断结果)"都在
 * service/urine 里算好并放进 urine_meas_t（本层持有一份 s_meas）。
 * monitor 只做三件事：
 *   ① 每周期喂"新增滴数+时间"给 urine_update()，让它算好整份结果；
 *   ② 把结果发去串口屏（发送函数待用户重定义，届时一并合成发送）；
 *   ③ 该写 U盘 时按结果写一条。
 *
 * 状态由"串口屏回传事件"(经 service/screen 回调)驱动；物理按键暂不接入。
 */

#include "bsp/err.h"
#include "service/urine/urine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建监控/状态机任务（内部会初始化计量结果并注册串口屏事件回调） */
err_t app_monitor_start(void);

/* 清零累计尿量并刷新显示（换病人/确认） */
void  app_monitor_reset(void);

/* 读当前"解算+判断"结果（供将来的串口屏合成发送等读取，只读返回） */
const urine_meas_t *app_monitor_get(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MONITOR_H */
