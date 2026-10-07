#ifndef SERVICE_SCREEN_H
#define SERVICE_SCREEN_H

#include "bsp/err.h"
#include <stdint.h>

/* TJC 屏只接收 MCU 发来的显示命令；实体按键承担全部输入。 */
err_t screen_init(void);
void screen_set_page(uint8_t page);
void screen_set_text(const char *ctrl, const char *text);
void screen_set_num(const char *ctrl, int32_t value);
void screen_set_float_text(const char *ctrl, double value, int decimals);
void screen_send_raw(const char *cmd);

void screen_show_urine(float total_ml);
void screen_show_rate(float rate_mlh);
void screen_show_drops(uint32_t drops);
void screen_show_time(const char *text);
void screen_show_status(const char *status);

#endif
