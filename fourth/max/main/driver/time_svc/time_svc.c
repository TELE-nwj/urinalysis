#include "time_svc.h"
#include "esp_timer.h"

/* driver 层：单调毫秒时基（上电起算）。
 * 只提供 esp_timer 的毫秒读数，不做任何日历换算（墙上时间见 driver/walltime）。 */

void timesvc_init(void)
{
    /* 目前只依赖 esp_timer，无需显式初始化；
       保留这个空函数是为了将来接 RTC 时有一个统一入口，且 clock_init() 的调用序列不变。 */
}

uint64_t timesvc_now_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}
