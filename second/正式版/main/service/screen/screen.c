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
/* n0/n1/n2 用三级写法: data.<控件>.txt="值" */
#define CTRL_URINE   "data.n0"   /* 累计尿量 ml (data.n0.txt) */
#define CTRL_RATE    "data.n1"   /* 速率 ml/h (data.n1.txt) */
#define CTRL_DROPS   "data.n2"   /* 累计滴数 (data.n2.txt) */
#define CTRL_TIME    "data.t1"   /* 计时/运行时间 (data.t1.txt) */
#define CTRL_STATUS  "data.t0"   /* 状态/报警 (data.t0.txt) */

/* 注册给 app 层(monitor)的回调 */
static screen_event_cb_t s_ev_cb  = NULL;
static screen_cmd_cb_t   s_cmd_cb = NULL;   /* 文本命令: (cmd, username) */

/* ============ RX 行缓冲(接收文本指令, 以 '\n' 切行) ============ */
static char s_rx_buf[96];
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
    /* 按用户要求: 滴数也用文本方式 data.n2.txt="整数" */
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

/* ============ 文本指令解析: 收到完整一行(不含\n)后调用 ============ */
static void handle_cmd_line(const char *line)
{
    if (!s_cmd_cb) return;

    /* 允许行首混进杂字节(如屏上电上报)，直接从【第一个 '@'】开始解析 */
    const char *p = line ? strchr(line, '@') : NULL;
    if (!p) return;
    if (p != line) {
        ESP_LOGW(TAG, "行首有 %d 个杂字节，已跳过: '%s'", (int)(p - line), line);
    }
    line = p;

    /* 用户名 = 第1个@之后、第2个@之前(可变长, 可含空格, 不含@)
       —— 原样保留、不做 trim，保证“输入什么就是什么” */
    const char *u0 = line + 1;
    const char *sep = strchr(u0, '@');
    if (!sep) return;
    size_t ulen = (size_t)(sep - u0);
    if (ulen == 0 || ulen > SCREEN_USERNAME_MAX) {
        ESP_LOGW(TAG, "用户名长度非法(%u): '%s'", (unsigned)ulen, line);
        return;
    }

    char user[SCREEN_USERNAME_MAX + 1];
    memcpy(user, u0, ulen);
    user[ulen] = '\0';

    /* 指令关键字在第2个@之后(到\r/\n/行尾为止) */
    const char *c = sep + 1;
    screen_cmd_t cmd = SCREEN_CMD_NONE;
    if      (strncmp(c, "login", 5) == 0) cmd = SCREEN_CMD_LOGIN;
    else if (strncmp(c, "start", 5) == 0) cmd = SCREEN_CMD_START;
    else if (strncmp(c, "stop",  4) == 0) cmd = SCREEN_CMD_STOP;
    else if (strncmp(c, "reset", 5) == 0) cmd = SCREEN_CMD_RESET;

    if (cmd != SCREEN_CMD_NONE) {
        ESP_LOGI(TAG, "CMD=%d user=%s", (int)cmd, user);
        s_cmd_cb(cmd, user);
    }
}

void screen_parse_line(const char *line)
{
    handle_cmd_line(line);
}

static void on_uart_rx(const uint8_t *data, size_t len)
{
    /* 【调试】先把收到的原始字节打出来：
       没这行 → MCU 根本没收到字节(接线/波特率/屏端没发)
       有这行但后面没反应 → 收到了，但行尾或格式不对，被解析器丢掉了 */
    {
        char t[64];
        size_t k = (len < 48) ? len : 48;
        for (size_t i = 0; i < k; i++) {
            uint8_t c = data[i];
            t[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        }
        t[k] = '\0';
        ESP_LOGI(TAG, "RX %u 字节: %s", (unsigned)len, t);
        ESP_LOG_BUFFER_HEXDUMP(TAG, data, (uint16_t)len, ESP_LOG_INFO);
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        /* ---- 行尾判定：\r / \n / "/n" 都算一行结束 ----
           "/n" 是为了兼容屏端把 \r\n 写成了字面量两个字符：
           实测屏端发来的是 2F 6E ('/' 'n')，不是 0D 0A。 */
        if (b == '\r' || b == '\n') {
            if (s_rx_len) {
                s_rx_buf[s_rx_len] = '\0';
                handle_cmd_line(s_rx_buf);
                s_rx_len = 0;
            }
            continue;                    /* 空行(\r\n 的第二个字符)直接跳过 */
        }
        if (b == 'n' && s_rx_len && s_rx_buf[s_rx_len - 1] == '/') {
            s_rx_len--;                  /* 丢掉那个 '/' */
            s_rx_buf[s_rx_len] = '\0';
            if (s_rx_len) handle_cmd_line(s_rx_buf);
            s_rx_len = 0;
            continue;
        }

        /* ★ 命令必须以 '@' 开头 —— 缓冲为空时就把非 '@' 的字节直接丢掉。
           否则屏在开机/复位时会回传 "1A FF FF FF"(既不是行尾也不是 '/n')，
           这几个字节会赖在行缓冲里，把随后的第一帧命令顶成非 '@' 开头
           → 整帧被 handle_cmd_line 丢掉(表现为"上电后第一次登录没反应")。 */
        if (s_rx_len == 0 && b != '@') {
            continue;
        }

        if (s_rx_len < sizeof(s_rx_buf) - 1) {
            s_rx_buf[s_rx_len++] = (char)b;
        } else {
            s_rx_len = 0;                /* 溢出保护: 丢行 */
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

void screen_set_cmd_cb(screen_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}
