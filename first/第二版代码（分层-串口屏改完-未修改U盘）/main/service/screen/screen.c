#include "screen.h"
#include "driver/screen_uart/screen_uart.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "screen";

/* ============ 淘晶驰 TJC 协议常量 ============ */
#define NX_END_LEN  3
static const uint8_t s_nx_end[NX_END_LEN] = {0xFF, 0xFF, 0xFF};

/* ============ 页面号(按你的 HMI 工程调整) ============ */
#define SCREEN_PAGE_MAIN   0   /* 主界面 */
#define SCREEN_PAGE_MENU   1   /* 菜单/设置(示例) */

/* ============ 控件名(对照你的 HMI 工程) ============ */
#define CTRL_URINE   "n0"   /* 累计尿量 ml 数值控件 */
#define CTRL_RATE    "n1"   /* 分时速率 ml/h 数值控件 */
#define CTRL_DROPS   "n2"   /* 累计滴数 数值控件 */
#define CTRL_TIME    "t1"   /* 计时/运行时间 文本控件 */
#define CTRL_STATUS  "t0"   /* 状态文本控件(报警/就绪) */

/* 注册给 app 层(monitor)的“回传事件”回调 */
static screen_event_cb_t s_ev_cb = NULL;

/* ============ RX 帧缓冲(接收串口屏回传) ============ */
static uint8_t s_rx_buf[128];
static uint16_t s_rx_len = 0;

/* 发送一条完整指令: "cmd" + 0xFF 0xFF 0xFF */
static void send_cmd(const char *cmd)
{
    if (!cmd) {
        return;
    }
    screen_uart_send_str(cmd);
    screen_uart_send(s_nx_end, NX_END_LEN);
    ESP_LOGD(TAG, "TX: %s", cmd);
}

void screen_set_page(uint8_t page)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "page %u", page);
    send_cmd(cmd);
}

void screen_send_raw(const char *cmd)
{
    send_cmd(cmd);
}

void screen_set_text(const char *ctrl, const char *txt)
{
    if (!ctrl || !txt) {
        return;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", ctrl, txt);
    send_cmd(cmd);
}

void screen_set_num(const char *ctrl, int32_t val)
{
    if (!ctrl) {
        return;
    }
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "%s.val=%ld", ctrl, (long)val);
    send_cmd(cmd);
}

void screen_set_float_text(const char *ctrl, double v, int dec)
{
    if (!ctrl) {
        return;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s.txt=\"%.*f\"", ctrl, dec, v);
    send_cmd(cmd);
}

/* 便捷接口：显示累计尿量(ml) */
void screen_show_urine(float total_ml)
{
    screen_set_float_text(CTRL_URINE, (double)total_ml, 1);
}

void screen_show_rate(float mlh)
{
    screen_set_float_text(CTRL_RATE, (double)mlh, 1);
}

void screen_show_drops(uint32_t drops)
{
    screen_set_num(CTRL_DROPS, (int32_t)drops);
}

void screen_show_time(const char *text)
{
    screen_set_text(CTRL_TIME, text ? text : "");
}

void screen_show_status(const char *status)
{
    screen_set_text(CTRL_STATUS, status ? status : "");
}

/* ============ 串口屏回传解析(示例：先收帧再解码) ============ */
static void handle_rx_frame(const uint8_t *data, uint16_t len)
{
    /* 淘晶驰回传格式随工程不同(通常是 return 控件/触屏事件)。
     * 这里先打日志便于调试。等 .HMI 协议确定后，把"收到的 keycode/控件值"
     * 映射成 screen_event_t，再调用 s_ev_cb(ev) 通知 monitor 状态机
     * (s_ev_cb 由 screen_set_event_cb 注册)。 */
    char hex[128] = {0};
    int n = 0;
    for (uint16_t i = 0; i < len && n < (int)sizeof(hex) - 1; i++) {
        n += snprintf(hex + n, sizeof(hex) - (size_t)n, "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "RX frame(%u): %s", len, hex);

    /* 示例映射(需按真实协议调整)：
     *   if (len >= 1 && data[0] == 0x01) { if(s_ev_cb) s_ev_cb(SCREEN_EV_CLEAR_REQUEST); return; }
     *   if (len >= 1 && data[0] == 0x02) { if(s_ev_cb) s_ev_cb(SCREEN_EV_PAUSE_REQUEST); return; }
     */
}

static void on_uart_rx(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (s_rx_len < sizeof(s_rx_buf)) {
            s_rx_buf[s_rx_len++] = data[i];
        }
        /* 检测 0xFF 0xFF 0xFF 帧尾 */
        if (s_rx_len >= 3 &&
            s_rx_buf[s_rx_len - 1] == 0xFF &&
            s_rx_buf[s_rx_len - 2] == 0xFF &&
            s_rx_buf[s_rx_len - 3] == 0xFF) {
            handle_rx_frame(s_rx_buf, (uint16_t)(s_rx_len - 3));
            s_rx_len = 0;
        } else if (s_rx_len >= sizeof(s_rx_buf)) {
            s_rx_len = 0;   /* 溢出保护 */
        }
    }
}

/* ============ 串口屏服务初始化(不建任务，被动服务) ============ */
err_t screen_init(void)
{
    /* 注册串口屏回传接收回调(driver/screen_uart 会自建 uart_rx 任务) */
    screen_uart_set_rx_cb(on_uart_rx);

    /* 初始界面：主页面 + 就绪状态 + 归零 */
    screen_set_page(SCREEN_PAGE_MAIN);
    screen_show_status("READY");
    screen_show_urine(0.0f);

    ESP_LOGI(TAG, "screen service ready");
    return ERR_OK;
}

void screen_set_event_cb(screen_event_cb_t cb)
{
    s_ev_cb = cb;
}
