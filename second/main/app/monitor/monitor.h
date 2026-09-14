#ifndef APP_MONITOR_H
#define APP_MONITOR_H

/*
 * app 层：状态机(monitor)。
 *   · 启动 / 注册命令回调
 *   · 【唯一一处】命令 → 状态 的转移判断（on_cmd 里那一个 switch）
 *   状态自己的显示/数据逻辑在 S1~S4：各自只提供 Sx_enter()，S3 另有测量接口。
 *
 * 状态：
 *   S1 复位待登录 / S2 已登录未开始 / S3 测量中 / S4 暂停
 */

#include "bsp/err.h"
#include "service/urine/urine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 状态编号 */
typedef enum {
    APP_S1 = 1,   /* 复位待登录 */
    APP_S2,       /* 已登录未开始 */
    APP_S3,       /* 测量中 */
    APP_S4,       /* 暂停 */
} app_state_t;

/* 启动状态机(初始化 + 注册命令回调 + 建任务) */
err_t app_monitor_start(void);

/* 强制复位(外部调用) */
void app_monitor_reset(void);

/* 读当前测量结果(转发 S3 的结果) */
const urine_meas_t *app_monitor_get(void);

/* 【调试用】把“当前信息”快照打到串口日志：
 * 状态 / 用户 / 尿量 / 速率 / 滴数 / 最近间隔 / 干满标志 / 记录条数。
 * 不传 note 时用默认标题。 */
void app_monitor_dump(const char *note);

#ifdef __cplusplus
}
#endif

#endif /* APP_MONITOR_H */
