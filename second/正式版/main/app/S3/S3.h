#ifndef APP_S3_H
#define APP_S3_H

/*
 * 应用层状态 S3：测量中
 *   · 承载"测量期"数据处理：会话计时 / 解算 5 值 / 推屏 / 按分钟记录。
 *   · 状态转移(收到 stop → S4)由 app/monitor 的 on_cmd() 统一判断。
 */
#include <stdint.h>
#include "service/screen/screen.h"
#include "service/urine/urine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 状态入口 */
void S3_enter(void);                   /* 进测量中: 强制整体刷一次屏 */
void S3_run(void);                     /* 每周期: drop_poll + 解算 + 推屏(200ms 节流) */

/* ---- U盘文件开关(monitor 在状态转移时调用) ----
 * 文件【不常开】：只在"每分钟写盘那一刻"进临界区后 打开→写→关闭。
 * 这样拔盘/断电最多丢当前这一分钟，不会丢整个文件。
 * 这两个函数只负责"记住/清掉本次测量的用户名"并做一次介质检查。 */
err_t S3_file_open(const char *user);   /* 开始/继续测量: 记住要写入哪个用户文件 */
void  S3_file_close(void);              /* 暂停/复位: 清掉用户名 */

/* 测量数据处理(供 monitor 在状态转移时调用) */
void S3_reset_data(uint64_t now_ms);   /* 开始/继续: 清数据+计时归零+记第0分钟 */
void S3_clear(uint64_t now_ms);        /* 复位: 清数据/计时(不记快照, 进空闲) */
void S3_resume(uint64_t now_ms);       /* 暂停后继续: 重新起算计时基准 */
void S3_update(uint64_t now_ms);       /* 每周期: 推进计时+解算+按分钟记录 */
void S3_pause(uint64_t now_ms);        /* 暂停: 结算计时 + 把数值冻结到屏 */

/* 推屏 */
void S3_push_values(void);             /* 只推 4 个数值/时间控件(不带状态) */
void S3_push_ui(void);                 /* 立即把 5 个数据全推到串口屏(调试用) */

/* 读测量结果(供上层/其它状态) */
const urine_meas_t *S3_meas(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S3_H */
