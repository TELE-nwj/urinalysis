#ifndef APP_S1_H
#define APP_S1_H

/*
 * S1 测量中（按状态数编号，对应状态机的"测量中"，由原 app/S3 迁入）
 *   · 测量期处理：会话计时、24 小时结论、每分钟往 U 盘写一行、200ms 推屏节流。
 *   · 数值控件(累计尿量/实时速率/累计滴数/会话计时)由本模块写；
 *     状态栏与报警栏文本由 app/monitor 写，本模块不碰那两个控件。
 *   · 液滴搬运与解算在 service/urine（monitor 每周期调 urine_poll()）。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 状态入口 */
void S1_enter(void);            /* 进入/继续测量: 计时基准挪到此刻 + 整体刷一次数值 */
void S1_run(void);              /* 每周期: 计时 → 24h 结论 → 每分钟写盘 → 200ms 推屏 */

/* 测量数据处理（由 monitor 在状态转移时调用） */
void S1_reset(void);            /* 新测量/复位: 清数据与计时，重记测量起点 */
void S1_settle(void);           /* 暂停结算: 计时推进到此刻并冻结，实时速率清零(S2 调用) */

/* 推屏 */
void S1_push_values(void);      /* 只推 4 个数值/时间控件(冻结、归零时用) */

/* 新测量分配独立文件；缺盘时允许计量，周期重试直到恢复。 */
void S1_storage_start(void);
void S1_storage_stop(void);
const char *S1_session_file(void);

/* 24 小时尿量结论 → 报警栏单字母: A=正常 / B=无尿 / C=少尿 / D=多尿
 * （E=尿袋满由 monitor 按尿量做 1 分钟插播，不是本函数的事） */
const char *S1_day_alert(void);
/* 属主任务的读取接口；墙上时间对时绝不会影响该值。 */
uint64_t S1_elapsed_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S1_H：头文件保护结束 */
