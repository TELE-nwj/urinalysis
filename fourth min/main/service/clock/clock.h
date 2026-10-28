#ifndef SERVICE_CLOCK_H
#define SERVICE_CLOCK_H

/*
 * service 层：时间服务 —— app 层唯一的时间入口。
 *
 * 【分层意义】app 层不直接碰 driver/time_svc 与 driver/walltime，
 *   凡是要"取时间"都走本模块：
 *     - clock_ms()      单调毫秒，用于会话计时(暂停/继续的加减由 app 决定)
 *     - clock_now_hms() 墙上时间，用于屏显；未对时时从 00:00:00 起算
 *
 * 【对时】上位机(后续 WiFi)接入后调 clock_sync_hms()，本模块不用改。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化底层时基与墙上时钟(main 启动阶段调一次)。 */
void clock_init(void);

/* 单调递增毫秒(上电起算，不受对时影响，用于会话计时)。 */
uint64_t clock_ms(void);

/* 墙上时间 "hh:mm:ss"。buf 至少 9 字节。 */
void clock_now_hms(char *buf, size_t len);

/* 对时(预留给后续上位机，如 WiFi)：调用后墙上时间从 hh:mm:ss 继续走。 */
void clock_sync_hms(int hh, int mm, int ss);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_CLOCK_H：头文件保护结束 */
