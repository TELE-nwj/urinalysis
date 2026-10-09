#ifndef SERVICE_SCREEN_H
#define SERVICE_SCREEN_H

/*
 * service 层：串口屏显示服务（TJC/淘晶驰，单向：单片机 → 屏）。
 *
 * 【本版三条规矩】
 *   1) 单片机只发不收：屏端不再向 MCU 发命令，MCU 也不注册 UART 接收回调。
 *   2) 发给屏的指令**只有一种格式**：<控件名>.txt="内容" 后跟 FF FF FF。
 *      状态栏(data.t4)发的也是这种指令，内容为命令字 reset/start/stop。
 *      **没有**其它自定义协议（早期设计过的 @start\r\n 一类事件指令已取消）。
 *   3) 每一帧发往屏的数据都会在日志串口打印一行，便于核对屏上实际收到的内容。
 *
 * 【控件映射】下面 8 个宏是固件与 HMI 工程之间的唯一约定。
 *   数据控件(t0~t3)、状态栏(t4)、时钟/电量(t6/t7)的 id 已由 HMI 工程定稿；
 *   报警栏(t5)仍是占位值。将来改 id 只需改这一处，其余代码不用动。
 */

#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 控件映射（固件与 HMI 工程的对接口） ====================
 * 数据控件(t0~t3)、状态栏(t4)、时钟/电量(t6/t7) 的 id 已由 HMI 工程定稿；
 * ⚠ 报警栏(t5) HMI 尚未制作，id 为**占位值**，做好后替换。
 * 改 id 只需改这一处，其余代码不用动。 */
#define SCR_CTRL_URINE    "data.t0"   /* 累计尿量  mL，1 位小数 */
#define SCR_CTRL_RATE     "data.t1"   /* 实时速率  mL/h，1 位小数 */
#define SCR_CTRL_DROPS    "data.t2"   /* 累计滴数  整数 */
#define SCR_CTRL_ELAPSED  "data.t3"   /* 运行时间  hh:mm:ss（会话计时） */
#define SCR_CTRL_CLOCK    "data.t6"   /* 实时时间  hh:mm:ss */
#define SCR_CTRL_BATTERY  "data.t7"   /* 电量：如 85% */
/* ---- 状态栏：内容用命令字，屏端可直接显示也可做条件判断 ---- */
#define SCR_CTRL_STATUS   "data.t4"   /* 状态：reset / start / stop */
/* ---- 报警栏：HMI 尚未制作，id 为占位（待确定后替换） ---- */
#define SCR_CTRL_ALERT    "data.t5"   /* 报警：单字母 A=正常 B=无尿 C=少尿 D=多尿 E=尿袋满 */

/* ==================== 初始化 ==================== */

/* 上电初始化：发送首页与各控件初值(不注册任何接收)。 */
err_t screen_init(void);

/* ============ 数据区：TJC 文本控件指令（<控件>.txt="内容" + FF FF FF） ============ */

void screen_show_urine(float total_ml);         /* 累计尿量 */
void screen_show_rate(float rate_mlh);          /* 实时速率 */
void screen_show_drops(uint32_t drops);         /* 累计滴数 */
void screen_show_elapsed(const char *hhmmss);   /* 运行时间(会话计时) */
void screen_show_status(const char *status);    /* 状态：reset / start / stop */
void screen_show_alert(const char *alert);      /* 单字母：A/B/C/D/E */
void screen_show_battery(int percent);          /* 0~100；-1 时显示 "--" */
void screen_show_clock(const char *hhmmss);     /* 实时时间 */

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_SCREEN_H */
