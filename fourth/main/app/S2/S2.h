#ifndef APP_S2_H
#define APP_S2_H

/*
 * S2 暂停（按状态数编号，对应状态机的"暂停"，本版新建）
 *   · 进入动作：把会话计时结算到暂停瞬间，并把 4 个数值冻结在屏上。
 *   · 采集中断的关闭由 monitor 在状态转换时做(urine_capture_hold)：本状态不再采集。
 *   · 状态栏"暂停"由 monitor 按状态统一推送(它同时负责状态栏/报警栏的"变化才发")，
 *     本模块不重复发同一个控件，避免两个写者互相覆盖。
 */

#ifdef __cplusplus
extern "C" {
#endif

void S2_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S2_H：头文件保护结束 */
