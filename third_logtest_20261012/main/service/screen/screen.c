#include "screen.h"
#include "driver/screen_uart/screen_uart.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "screen";

/* ============================ 指令格式（本文件的核心） ============================
 * 本文件是本工程唯一给屏"组帧"的地方。全工程**只有一种**发往屏的指令：
 *
 *     <控件名>.txt="内容"  + 0xFF 0xFF 0xFF
 *
 * 例：<SCR_CTRL_URINE>.txt="12.5" <FF FF FF>
 * 页切换 "page N" 也属于这一类，同样以 FF FF FF 结尾。
 *
 * 状态栏(data.t4)发的也是这种文本指令，内容为命令字 reset/start/stop ——
 * 屏端既可直接显示，也可拿它做条件判断。**没有**其它自定义协议
 * (早期设计过的 `@start\r\n` 一类事件指令已按用户要求取消)。
 *
 * 按用户要求：每一帧发往屏的数据都要在日志串口打印一行，便于逐行核对屏上实际
 * 收到的内容(周期性帧如时间/电量/尿量同样要打)。日志行只打印可见内容，不含结尾字节。
 * ============================================================================== */

/* 每一帧的结尾：3 个 \xff 字节（TJC 指令的结束标志）。
 * 写成字符串字面量 "\xff\xff\xff"：数组大小正好取 3 个字节，结尾的 '\0' 不进数组。 */
static const uint8_t s_txt_end[3] = "\xff\xff\xff";

/* 一帧的最大长度：控件名 + ".txt=\"\"" + 内容都装得下(最长是滴数满 10 位数时) */
#define SCR_FRAME_MAX 64

/* 首页(主界面)页号：HMI 工程里主页面即 page0。
 * 页切换指令也属于数据区(结尾同样是 FF FF FF)，但它不是"文本控件"，所以不在
 * SCR_CTRL_* 控件宏表里，单独放这一处，将来换页号只改这一行。 */
#define SCR_PAGE_MAIN 0

/* 每一帧发往屏的数据都走项目统一日志打印一行，单行呈现为：
 *   I (1234) screen: [SCR] <SCR_CTRL_URINE>.txt="12.5"
 * 用 ESP_LOGI(不是 printf)：带时间戳与 TAG，能跟着项目日志级别统一开关。
 * ⚠ frame 里含引号、内容也可能含 '%'，所以必须用 "%s" 把它当参数传，
 *    绝不能把 frame 直接当格式串(那会变成格式串漏洞)。
 * 失败/异常才用 ESP_LOGE(见下面各发送函数)。 */
static void scr_log_frame(const char *frame)
{
    ESP_LOGI(TAG, "[串口屏] %s", frame);
}

/* 数据区通道统一出口：打印日志 → 发可见内容 → 追加 FF FF FF。
 * 文本控件指令与页切换指令都走这里(两者结尾字节相同，属于同一条数据区通道)。 */
static void send_data_frame(const char *frame)
{
    scr_log_frame(frame);
    if (screen_uart_send_str(frame) != ERR_OK ||
        screen_uart_send(s_txt_end, sizeof(s_txt_end)) != ERR_OK) {
        ESP_LOGE(TAG, "数据帧发送失败: %s", frame);
    }
}

/* 通道①：文本控件指令 <控件名>.txt="内容" + FF FF FF
 * ctrl 一律传 screen.h 里的 SCR_CTRL_* 宏，本文件不出现任何控件名字面量。 */
static void send_text_cmd(const char *ctrl, const char *text)
{
    char frame[SCR_FRAME_MAX];
    int n = snprintf(frame, sizeof(frame), "%s.txt=\"%s\"", ctrl, text);
    if (n < 0 || n >= (int)sizeof(frame)) {
        /* 帧太长被截断：宁可不发，也不发一条残缺的指令(屏会显示乱码或误解析) */
        ESP_LOGE(TAG, "文本帧过长已丢弃: %s", ctrl);
        return;
    }
    send_data_frame(frame);
}

/* 通道①的数值版本：尿量/速率这类 1 位小数的值，先格式化成字符串再走文本通道
 * (屏上控件显示的是文本，不是二进制数值)。缓冲区 48 字节是按 float 用 "%.1f"
 * 格式化时可能的最长输出(整数部分最多 39 位 + 小数点 + 1 位小数)留足余量。 */
static void send_text_1f(const char *ctrl, float value)
{
    char text[48];
    snprintf(text, sizeof(text), "%.1f", (double)value);
    send_text_cmd(ctrl, text);
}

/* ============================ 文本控件刷新 ============================ */

void screen_show_urine(float total_ml)
{
    send_text_1f(SCR_CTRL_URINE, total_ml);          /* 1 位小数，如 12.5 */
}

void screen_show_rate(float rate_mlh)
{
    send_text_1f(SCR_CTRL_RATE, rate_mlh);           /* 1 位小数，如 180.0 */
}

void screen_show_drops(uint32_t drops)
{
    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)drops);  /* 整数，无小数点 */
    send_text_cmd(SCR_CTRL_DROPS, text);
}

void screen_show_elapsed(const char *hhmmss)
{
    send_text_cmd(SCR_CTRL_ELAPSED, hhmmss ? hhmmss : "");
}

void screen_show_status(const char *status)
{
    send_text_cmd(SCR_CTRL_STATUS, status ? status : "");
}

void screen_show_alert(const char *alert)
{
    send_text_cmd(SCR_CTRL_ALERT, alert ? alert : "");
}

void screen_show_battery(int percent)
{
    char text[8];
    if (percent < 0) {
        snprintf(text, sizeof(text), "--");          /* 电量还没测出来：显示 -- */
    } else {
        /* 契约是 0~100；越界值钳到 100，免得屏上冒出 120% 这类怪数字 */
        if (percent > 100) {
            percent = 100;
        }
        snprintf(text, sizeof(text), "%d%%", percent);  /* 屏上显示如 85% */
    }
    send_text_cmd(SCR_CTRL_BATTERY, text);
}

void screen_show_clock(const char *hhmmss)
{
    send_text_cmd(SCR_CTRL_CLOCK, hhmmss ? hhmmss : "");
}

/* ================================ 初始化 ================================ */

err_t screen_init(void)
{
    /* 1. UART 先起来：main.c 作为组合根只调用 service 层的 init，所以串口屏
     *    UART 的初始化入口收敛在这里；UART 失败就没必要再发指令，直接上报失败。 */
    if (screen_uart_init() != ERR_OK) {
        ESP_LOGE(TAG, "串口屏 UART 初始化失败");
        return ERR_FAIL;
    }

    /* 2. 切到首页：页切换属于数据区指令，结尾同样追加 FF FF FF */
    char page[SCR_FRAME_MAX];
    snprintf(page, sizeof(page), "page %u", (unsigned)SCR_PAGE_MAIN);
    send_data_frame(page);

    /* 3. 各控件初值：上电先把 8 个控件刷成确定的初始内容，避免屏上残留 HMI 工程
     *    的默认文本、与固件实际状态不一致。 */
    screen_show_urine(0.0f);            /* 累计尿量 0.0 mL */
    screen_show_rate(0.0f);             /* 实时速率 0.0 mL/h */
    screen_show_drops(0);               /* 累计滴数 0 */
    screen_show_elapsed("00:00:00");    /* 会话计时从零起算 */
    screen_show_status("reset");        /* 状态栏初值 = 待开始 */
    screen_show_alert("A");              /* 报警栏初值：A=正常 */
    screen_show_battery(-1);            /* 电量尚未采样：先显示 -- */
    screen_show_clock("00:00:00");      /* 实时时钟未同步时也从 00:00:00 起算 */

    return ERR_OK;
}
