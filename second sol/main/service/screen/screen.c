#include "screen.h"
#include "driver/screen_uart/screen_uart.h"
#include <stdio.h>
#include <string.h>

#define SCREEN_PAGE_MAIN 0
#define CTRL_URINE  "data.n0"
#define CTRL_RATE   "data.n1"
#define CTRL_DROPS  "data.n2"
#define CTRL_TIME   "data.t1"
#define CTRL_STATUS "data.t0"

static const uint8_t s_end[3] = {0xFF, 0xFF, 0xFF};

static void send_cmd(const char *cmd)
{
    if (!cmd) return;
    screen_uart_send_str(cmd);
    screen_uart_send(s_end, sizeof(s_end));
}

void screen_set_page(uint8_t page)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "page %u", (unsigned)page);
    send_cmd(cmd);
}

void screen_send_raw(const char *cmd)
{
    send_cmd(cmd);
}

void screen_set_text(const char *ctrl, const char *text)
{
    if (!ctrl || !text) return;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", ctrl, text);
    send_cmd(cmd);
}

void screen_set_num(const char *ctrl, int32_t value)
{
    if (!ctrl) return;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "%s.val=%ld", ctrl, (long)value);
    send_cmd(cmd);
}

void screen_set_float_text(const char *ctrl, double value, int decimals)
{
    if (!ctrl) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%.*f\"", ctrl, decimals, value);
    send_cmd(cmd);
}

void screen_show_urine(float total_ml)
{
    screen_set_float_text(CTRL_URINE, (double)total_ml, 1);
}

void screen_show_rate(float rate_mlh)
{
    screen_set_float_text(CTRL_RATE, (double)rate_mlh, 1);
}

void screen_show_drops(uint32_t drops)
{
    screen_set_float_text(CTRL_DROPS, (double)drops, 0);
}

void screen_show_time(const char *text)
{
    screen_set_text(CTRL_TIME, text ? text : "");
}

void screen_show_status(const char *status)
{
    screen_set_text(CTRL_STATUS, status ? status : "");
}

err_t screen_init(void)
{
    /* 不注册 UART RX 回调，因此不会创建接收任务。 */
    screen_set_page(SCREEN_PAGE_MAIN);
    screen_show_urine(0.0f);
    screen_show_rate(0.0f);
    screen_show_drops(0);
    screen_show_time("00:00:00");
    screen_show_status("待开始");
    return ERR_OK;
}
