#ifndef APP_S3_H
#define APP_S3_H

/*
 * 测量期处理：有效计时、尿量解算、24 小时判定、推屏和按分钟记录。
 * 两键状态转换统一由 app/monitor 处理。
 */
#include <stdint.h>
#include "service/screen/screen.h"
#include "service/urine/urine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 状态入口 */
void S3_enter(void);                   /* 进入/恢复测量: 强制整体刷一次屏 */
void S3_run(void);                     /* 每周期: drop_poll + 解算 + 推屏(200ms 节流) */

/* 新测量分配独立文件；缺盘时允许计量，周期重试直到恢复。 */
void S3_storage_start(void);
void S3_storage_stop(void);

/* 测量数据处理(供 monitor 在状态转移时调用) */
void S3_reset_data(uint64_t now_ms);   /* 新测量: 清数据和计时 */
void S3_clear(uint64_t now_ms);        /* 复位: 清数据/计时(不记快照, 进空闲) */
void S3_resume(uint64_t now_ms);       /* 暂停后继续: 重新起算计时基准 */
void S3_update(uint64_t now_ms);       /* 每周期: 推进计时+解算+按分钟记录 */
void S3_pause(uint64_t now_ms);        /* 暂停: 结算计时 + 把数值冻结到屏 */

/* 推屏 */
void S3_push_values(void);             /* 只推 4 个数值/时间控件(不带状态) */
void S3_push_ui(void);                 /* 立即把 5 个数据全推到串口屏(调试用) */

/* 读测量结果(供上层/其它状态) */
const urine_meas_t *S3_meas(void);
const char *S3_session_file(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_S3_H */
