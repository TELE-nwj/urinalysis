#ifndef SERVICE_SCREEN_H
#define SERVICE_SCREEN_H

/*
 * service 层：串口屏服务(淘晶驰 TJC / Nextion 类)。
 * 作为 app 层(monitor) 与 driver/screen_uart(底层 UART 字节收发)之间的桥接：
 *
 *   - 按淘晶驰协议组帧发送(指令以 0xFF 0xFF 0xFF 结尾)，函数名即“让串口屏显示X”
 *   - 页面切换、控件(文本/数值)刷新
 *   - 接收串口屏回传，解析成“语义事件”，经注册的回调通知 app 层(monitor)做状态判断
 *
 * 本服务【不建任务、不依赖 app 层】。由 app 层(monitor)调用其显示函数并注册事件回调。
 *
 * 说明：页面号/控件名(如 "n0"/"t0")是按 TJC 工程控件默认命名，请对照 .HMI 工程改 .c 顶部宏。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 串口屏按钮回传解码后的“语义事件”(映射待按真实 HMI return 协议补充) */
typedef enum {
    SCREEN_EV_NONE = 0,
    SCREEN_EV_START,   /* 屏上按下“开始/继续” */
    SCREEN_EV_PAUSE,   /* 屏上按下“暂停”      */
    SCREEN_EV_RESET,   /* 屏上按下“复位”(清零, 仅暂停态有效) */
    SCREEN_EV_SAVE,    /* 屏上按下“保存/记录”(暂停态手动写U盘) */
} screen_event_t;

typedef void (*screen_event_cb_t)(screen_event_t ev);

/* ---- 文本命令协议(上位机/串口屏发来): @用户名@命令\r\n ----
 * 用户名夹在两个 @ 之间、可变长；用户名为文件主体，建文件时拼 .txt。
 * 【用户名规则】可以含空格(含首尾空格)，但不含 '@' → 以两个 '@' 作为定界符唯一。
 *   · 解析时原样保留，不做 trim/压缩，保证“输入什么就是什么”。
 * 只有复位后允许换用户(由上层状态机判断)。 */
#define SCREEN_USERNAME_MAX  31u   /* 用户名最大长度(字符数) */

typedef enum {
    SCREEN_CMD_NONE = 0,
    SCREEN_CMD_LOGIN,   /* @用户@login\r\n  登录 */
    SCREEN_CMD_START,   /* @用户@start\r\n  开始/继续测量 */
    SCREEN_CMD_STOP,    /* @用户@stop\r\n   暂停测量 */
    SCREEN_CMD_RESET,   /* @用户@reset\r\n  复位 */
} screen_cmd_t;

typedef void (*screen_cmd_cb_t)(screen_cmd_t cmd, const char *username);

/* 注册"文本命令"回调：解析出 (指令, 用户名) 后调用。
 * 上层(monitor 状态机)只需收到 cmd 和 username，无需再自己解析。 */
void screen_set_cmd_cb(screen_cmd_cb_t cb);

/* 把一整行(不含\n)交给命令解析器处理(用于测试/控制台喂行)。
 * 正常运行时由 driver 收字节→on_uart_rx 内部自动按行调用。 */
void screen_parse_line(const char *line);

/* 初始化串口屏服务：注册 driver 接收回调并显示初始界面(不建任务) */
err_t screen_init(void);

/* 注册“回传事件”处理器：由 app 层(monitor 状态机)调用，收到屏事件时回调 */
void screen_set_event_cb(screen_event_cb_t cb);

/* ---- 通用屏操作(控件名由你的工程决定) ---- */
void screen_set_page(uint8_t page);
void screen_set_text(const char *ctrl, const char *txt);        /* 文本控件 */
void screen_set_num(const char *ctrl, int32_t val);             /* 数值控件 */
void screen_set_float_text(const char *ctrl, double v, int dec);
void screen_send_raw(const char *cmd); /* 发一条已拼好的指令(自动补 \xff\xff\xff) */

/* ---- 面向尿量仪的业务便捷接口(默认控件宏见 .c) ---- */
void screen_show_urine(float total_ml);      /* data.n0: 累计尿量 ml */
void screen_show_rate(float mlh);            /* data.n1: 实时速率 ml/h */
void screen_show_drops(uint32_t drops);      /* data.n2: 累计滴数(文本) */
void screen_show_time(const char *text);     /* data.t1: 计时/运行时间文本 */
void screen_show_status(const char *status); /* data.t0: 状态/报警文本 */

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_SCREEN_H */
