#include "monitor.h"
#include "app/S1/S1.h"
#include "app/S2/S2.h"
#include "service/clock/clock.h"
#include "service/input/input.h"
#include "service/power/power.h"
#include "service/screen/screen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

/*
 * app 层：三态状态机主体（待开始 / 测量中=S1 / 暂停=S2）。
 *
 * 【本模块负责什么】
 *   ① 20ms 主循环：按键扫描、液滴搬运与解算、1 秒节拍、状态栏与报警栏推送；
 *   ② 按键语义（全部走 service/input，不碰 GPIO/消抖细节）；
 *   ③ 状态栏与报警栏的文本**唯一写者**：数值控件(尿量/速率/滴数/会话计时)
 *      由 S1/S2 写，状态栏/报警栏由这里写，避免同一控件两个写者互相覆盖。
 *      状态栏(data.t4)发的是命令字 reset/start/stop —— 屏端既可直接显示，
 *      也可拿它做条件判断。全工程只有文本控件这一种屏指令，没有别的自定义协议。
 *      报警栏(data.t5)发单字母 A/B/C/D/E，由 1 秒节拍每秒发（A 一直发）。
 *
 * 【状态与状态栏取值】
 *   待开始 = "reset"   测量中 = "start"   暂停 = "stop"
 *   K1 在三态间流转、K2 任意状态复位；状态栏文本由 ui_sync() 在内容变化时补发。
 */

static const char *TAG = "monitor";

/* 主循环周期：按键扫描、液滴搬运、状态同步都以它为单位 */
#define LOOP_MS 20u
/* 1 秒节拍周期(ms)：与是否测量无关，上电就开始 */
#define TICK_MS 1000u

static app_state_t s_state = APP_IDLE;

/* 1 秒节拍的下一次触发时刻(clock_ms 绝对时刻)。
 * 为什么不用"计 50 次 20ms 循环"：测量中的每分钟写盘、掉盘 5 秒重试都会让
 * 单次循环远超 20ms，计数法会把 1 秒刷成 1.0~1.5 秒；用绝对时刻比较则只推迟
 * 当次、不累积漂移。初值 0 = 上电第一轮先发一次，把时钟/电量立刻显示出来。 */
static uint64_t s_next_tick_ms = 0;

/* 状态栏的"上次已发送内容"缓存：只有内容变化才发指令 */
static char s_last_status[32];

static const char *state_name(app_state_t state)
{
    switch (state) {
    case APP_IDLE:      return "待开始";
    case APP_MEASURING: return "测量中";
    case APP_PAUSED:    return "暂停";
    default:            return "?";
    }
}

/* 状态栏文本：用命令字表示三态，屏端既可直接显示也可做条件判断。
 *   reset = 待开始(上电或复位后) / start = 测量中 / stop = 暂停 */
static const char *status_text(void)
{
    switch (s_state) {
    case APP_MEASURING: return "start";
    case APP_PAUSED:    return "stop";
    case APP_IDLE:
    default:            return "reset";
    }
}

/* ===== 报警栏：单字母协议 =====
 *   A=正常  B=无尿  C=少尿  D=多尿  E=尿袋满
 *
 * 【发送时机】本栏由 1 秒节拍每秒发一次（不像状态栏那样只在变化时发）：
 *   A 要"一直发送"，屏上才能始终有值；B/C/D 是 S1 满 24h 的结论，锁定后持续有效；
 *   E 只在"累计尿量首次达到 2L"时插播 1 分钟，之后回到本该显示的字母。 */
#define ALERT_BAG_FULL_MS  60000ULL   /* 尿袋满提示持续 1 分钟 */

static uint64_t s_bag_until_ms = 0;   /* E 提示窗口的截止时刻；0 = 不在窗口内 */
static uint8_t  s_bag_prev     = 0;   /* 上一拍的满袋标志，用于抓上升沿 */

/* 当前该显示的报警字母 */
static const char *alert_letter(void)
{
    if (s_bag_until_ms != 0 && clock_ms() < s_bag_until_ms) {
        return "E";                             /* 满袋插播窗口内优先显示 E */
    }
    if (s_state == APP_IDLE) return "A";        /* 待开始(含复位后)：正常 */
    return S1_day_alert();                      /* A / B / C / D */
}

/* 每秒调用：维护满袋插播窗口，并把当前字母发到屏上 */
static void alert_tick(void)
{
    const uint8_t full = urine_get()->flag_bag_full;

    /* 只看上升沿(0→1)：flag_bag_full 一旦置 1 就不会自己回落(累计尿量只增)，
     * 若不做边沿检测，就会把"一直满袋"误当成持续事件、E 永远刷下去。 */
    if (full && !s_bag_prev) {
        s_bag_until_ms = clock_ms() + ALERT_BAG_FULL_MS;
        ESP_LOGI(TAG, "尿袋满(累计%.1fml)：报警栏插播 E 一分钟",
                 (double)urine_get()->total_ml);
    }
    s_bag_prev = full;

    screen_show_alert(alert_letter());
}

/* 状态栏：内容变了才发（每帧的屏通信日志由 service/screen 打）。
 * 按键只改状态，文本由这里按变化补发；报警栏走每秒的 alert_tick()。 */
static void ui_sync(void)
{
    const char *st = status_text();

    if (strcmp(st, s_last_status) != 0) {
        snprintf(s_last_status, sizeof(s_last_status), "%s", st);
        screen_show_status(st);
    }
}

/* ===== 1 秒节拍：电量采样 + 时钟/电量/报警栏上屏 =====
 * 与是否测量无关，上电就跑；按 clock_ms() 的绝对时刻判定，
 * 环内偶发阻塞(写盘/掉盘重试)只会推迟当次，不会让周期越走越长。 */
static void tick_1s(void)
{
    char hms[9];

    power_poll();                           /* ① 读电压 → 滤波 → 查表得电量 */
    clock_now_hms(hms, sizeof(hms));         /* ② 墙上时间(未对时时从 00:00:00 起算) */
    screen_show_clock(hms);
    screen_show_battery(power_percent());    /* 未就绪时 -1 → 屏上显示 "--" */
    alert_tick();                            /* ③ 报警栏：每秒发一次(A 一直发) */
}

static void set_state(app_state_t state)
{
    if (state == s_state) return;
    /* 状态转换是排障最重要的线索，保留一行日志(屏上文本由 ui_sync 按变化补发) */
    ESP_LOGI(TAG, "状态 %s -> %s", state_name(s_state), state_name(state));
    s_state = state;
}

/* ===== 复位：K2 与 app_monitor_reset() 共用 =====
 * 有意做成"任何状态都执行"(含待开始)：原代码在待开始时直接 return，
 * 但屏端自己也有显示状态，K2 时即使 MCU 已在待开始也要把状态栏刷成 "reset"，
 * 才能保证屏和 MCU 停在同一个界面上（ui_sync 会按内容变化补发）。 */
static void do_reset(void)
{
    urine_capture_hold();     /* 先停采集再清点，避免清点与中断写点竞争 */
    urine_capture_clear();
    S1_storage_stop();        /* 关掉本次会话文件，下次测量重新分配 */
    S1_reset();               /* 数据/计时/24h 结论全部归零，重记测量起点 */
    S1_push_values();         /* 4 个数值控件归零 */
    set_state(APP_IDLE);      /* 状态栏随之变成 "reset"，由 ui_sync 补发 */
    s_bag_until_ms = 0;       /* 取消可能还在进行中的 E 插播窗口 */
    s_bag_prev     = 0;       /* 满袋边沿检测重新起算，下次达到 2L 再触发 */
}

static void on_key(input_key_t key)
{
    if (key == INPUT_KEY2) {          /* K2：复位（任何状态都响应） */
        do_reset();
        return;
    }
    if (key != INPUT_KEY1) return;

    switch (s_state) {
    case APP_IDLE:
        /* 先分配记录文件，再以实际开始采集的时刻为计时起点。
         * 待开始状态下滴落中断是关的(上电与复位都 hold 过)，可以直接清点。 */
        S1_storage_start();
        S1_reset();
        urine_capture_arm();          /* 清旧间隔 + 打开滴落中断，正式开始采集 */
        S1_enter();                   /* 计时基准=此刻，并把数值整体刷一次 */
        set_state(APP_MEASURING);
        break;

    case APP_MEASURING:
        /* 先把 ISR 里的间隔搬出来，结算才不会漏掉最后一滴 */
        urine_poll();
        S2_enter();                   /* 结算计时到暂停瞬间 + 数值冻结到屏 */
        urine_capture_hold();         /* 冻结之后再关中断：暂停期落下的滴丢弃 */
        urine_capture_clear();
        set_state(APP_PAUSED);
        break;

    case APP_PAUSED:
        S1_enter();                   /* 计时基准=此刻(暂停期间不计时) */
        urine_capture_arm();          /* 清旧间隔 + 打开滴落中断，继续采集 */
        set_state(APP_MEASURING);
        break;

    default:
        break;
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        input_poll();                            /* ① 按键：确认短按后回调 on_key */

        urine_poll();                            /* ② 液滴搬运 + 解算(与状态无关) */

        if (s_state == APP_MEASURING) {
            S1_run();                            /* ③ 测量中：计时/24h/写盘/200ms 推屏 */
        }

        uint64_t tick_now = clock_ms();          /* 取在可能阻塞的 S1_run() 之后 */
        if (tick_now >= s_next_tick_ms) {        /* ④ 1 秒节拍 */
            tick_1s();
            /* 用加法推进而不是 = now + 1000：偶发的几十毫秒延迟不会累积漂移。
             * 若被阻塞超过一整拍(写盘最坏情况)，直接跳到下一个未来时刻，
             * 不补发漏掉的拍，避免连推好几帧。 */
            s_next_tick_ms += TICK_MS;
            if (s_next_tick_ms <= tick_now) s_next_tick_ms = tick_now + TICK_MS;
        }

        ui_sync();                               /* ⑤ 状态栏/报警栏：变化才发 */
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

err_t app_monitor_start(void)
{
    /* 采集硬件(drop)已由 main 的 urine_capture_init() 配好；
     * 上电先保持"停采集"，等 K1 才 arm。 */
    urine_capture_hold();
    if (input_init(on_key) != ERR_OK) return ERR_FAIL;

    s_state = APP_IDLE;
    S1_reset();
    S1_push_values();
    /* 状态栏("待开始")与报警栏("正常")由 ui_sync 的首轮循环补齐 */

    BaseType_t ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ERR_OK : ERR_FAIL;
}

void app_monitor_reset(void)
{
    /* 与 K2 完全同义：任何状态都复位（状态栏随之为 "reset"） */
    do_reset();
}

app_state_t app_monitor_state(void)
{
    return s_state;
}

const urine_meas_t *app_monitor_get(void)
{
    return urine_get();
}

void app_monitor_dump(const char *note)
{
    const urine_meas_t *m = urine_get();
    ESP_LOGI(TAG, "[%s] %s 尿量=%.1fml 速率=%.1fml/h 滴数=%lu 满袋=%u",
             note ? note : "快照", state_name(s_state),
             (double)m->total_ml, (double)m->rate_mlh,
             (unsigned long)m->total_drops, (unsigned)m->flag_bag_full);
}
