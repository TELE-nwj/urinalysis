/*
 * service 层：时间服务 —— app 层唯一的时间入口。
 *
 * 【为什么要这一层】
 *   app 层同时需要两样东西：会话计时用的"单调毫秒"（driver/time_svc），
 *   屏显与记录用的"墙上时间 hh:mm:ss"（driver/walltime）。
 *   如果让 app 直接 include 这两个 driver 头文件，app 就和"时间从哪来"焊死了：
 *   以后换成 RTC、或接上上位机对时，改动会顺着 app 扩散到每个用时间的文件。
 *   本模块把它们收成唯一入口，app 只认 clock_*，底层换实现不用动 app。
 *
 * 【本层只转发，不重复实现】
 *   走时、跨天回绕、合法性处理全部留在 driver：那才是它们该在的地方。
 *   这里每个函数只有一次调用，保证上层看到的行为与 driver 完全一致，
 *   也避免同一套逻辑写在两处、日后改一处漏一处。
 */
#include "clock.h"
#include "driver/time_svc/time_svc.h"
#include "driver/walltime/walltime.h"
#include "esp_log.h"

static const char *TAG = "clock";

void clock_init(void)
{
    /* 顺序不能反：walltime 以 time_svc 的单调毫秒为累加基准，
       先起时基，它的计时起点才落在正确的时刻上。 */
    timesvc_init();
    walltime_init();
    ESP_LOGI(TAG, "时间服务就绪: 单调毫秒 + 墙上时间(未对时从 00:00:00 起算)");
}

uint64_t clock_ms(void)
{
    /* 纯转发：app 要的"开机以来毫秒"，语义与 timesvc_now_ms() 完全相同。 */
    return timesvc_now_ms();
}

void clock_now_hms(char *buf, size_t len)
{
    /* 纯转发：格式化与 24h 回绕都在 walltime 里；buf 至少 9 字节(见 clock.h)。 */
    walltime_now_hms(buf, len);
}

void clock_sync_hms(int hh, int mm, int ss)
{
    /* 纯转发：越界按 24h 回绕由 walltime 负责。
       上位机(后续 WiFi)接入后只需调这里对时，本模块无需改动。 */
    walltime_set_hms(hh, mm, ss);
}
