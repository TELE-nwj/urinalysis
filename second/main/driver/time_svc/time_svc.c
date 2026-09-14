#include "time_svc.h"
#include "esp_timer.h"
#include <stdio.h>

void timesvc_init(void)
{
    /* 如需 RTC 在此初始化 */
}

uint64_t timesvc_now_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}

void timesvc_stamp(char *buf, size_t len)
{
    /* TODO: 接入 RTC 后输出 "yyyy-mm-dd hh:mm:ss" */
    if (buf && len) {
        snprintf(buf, len, "T+%llus", (unsigned long long)(timesvc_now_ms() / 1000ULL));
    }
}
