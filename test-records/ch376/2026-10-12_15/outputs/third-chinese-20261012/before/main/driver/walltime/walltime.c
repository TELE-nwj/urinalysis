/*
 * driver 层实现：墙上时间（hh:mm:ss）。
 *
 * 【为什么用单调时基而不是 RTC】
 *   1) 本板没有给 RTC 域供电的纽扣电池/VBAT，掉电后 RTC 一样归零，靠它拿不到
 *      真正的"墙上时间"；
 *   2) 走 SNTP/system time 需要联网，而且对时那一下会让系统时间跳变——
 *      记录时序、会话计时都建立在"时间只往前走"这个前提上，跳变会把它们搞乱；
 *   3) ESP-IDF 的 esp_timer 是单调递增的，抖动小、不受对时影响，所以我们只用它
 *      当"尺子"，再显式记住一个起点，用"起点 + 经过的时间"推算出墙上时间。
 *   代价是：起点必须由外部给出（上位机对时），否则只能从 00:00:00 本地起算。
 *
 * 【为什么未对时要从 00:00:00 本地起算】
 *   屏显和记录行里必须始终有一个合法时刻，不能空着、也不能出现 24:xx 这种
 *   非法值。从 00:00:00 起走的好处是：格式与已对时时完全一致、随时间单调递增、
 *   便于人工比对日志；上层不需要写"未对时"的分支，读到的就是本模块输出的字符串。
 *   等到上位机（后续 WiFi）接入，只需调用一次 walltime_set_hms() 就切换成真实
 *   时刻，本模块的接口和上层代码都不用改——这就是 is_synced 存在的意义：
 *   让上层能区分"这是本地起算的相对时间"还是"已对时的真实时刻"。
 *
 * 【本版的并发约定】
 *   代码求简，读侧不加锁：set_hms 目前没有任何调用者（本版是预留接口）。
 *   若后续上位机在另一个任务里对时，需要重新评估——set 与取时并发时，取时可能
 *   读到"新起点 + 旧基准毫秒"的组合（错一次对时偏移量的量级）。接对时时再按需
 *   加临界区，现在不为一个没有调用者的路径引入锁。
 */

#include "driver/walltime/walltime.h"
#include "driver/time_svc/time_svc.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "walltime";

#define SEC_PER_DAY 86400U   /* 一天的秒数，24h 回绕的分母 */

/*
 * 状态：只需要一个"起点"就能描述全部走时信息。
 *   s_base_sec —— 起点那一刻的当日秒数(0..86399)；
 *   s_base_ms  —— 取得起点时的单调毫秒数；
 *   s_synced   —— 0=本地起算(从未对时)，1=上位机已对时。
 *
 * 【为什么不自己每秒累加一个计数器】
 *   没有 RTC 中断、也不打算为计时单开一个任务：靠周期任务累加会受任务抖动和
 *   漏拍影响，误差越滚越大。改成"读单调毫秒做差"，误差只取决于读的那一刻，
 *   不随时间累积，代码也更少。
 */
static uint32_t s_base_sec;
static uint64_t s_base_ms;
static int      s_synced;

/*
 * 把调用方给的 hh/mm/ss 归一到当日秒数 0..86399。
 * 先按 24h 回绕（头文件契约：越界即回绕），再兜住负数——
 * C 里负数取模得到的是负余数，补一个周期才能落回 [0, 86400)。
 * 中间量用 int64：调用方真塞进来 INT_MAX 这种值时，int 相乘会溢出。
 */
static uint32_t hms_to_sec(int hh, int mm, int ss)
{
    int64_t total = (int64_t)hh * 3600 + (int64_t)mm * 60 + (int64_t)ss;

    total %= (int64_t)SEC_PER_DAY;
    if (total < 0) {
        total += (int64_t)SEC_PER_DAY;
    }
    return (uint32_t)total;
}

/*
 * 当前当日秒数 = 起点秒数 + 起点之后经过的整秒，再对 86400 取模。
 * 取模带来两个正好需要的性质：一是跨天自动回到 00:00:00，二是返回值恒在
 * 0..86399，调用方（屏显/记录）不用自己处理"24:xx"。
 * timesvc_now_ms() 单调不减，减去 s_base_ms 不会下溢，所以这里不需要判错分支。
 */
static uint32_t wall_sec_now(void)
{
    uint64_t elapsed_ms = timesvc_now_ms() - s_base_ms;

    return (uint32_t)((s_base_sec + elapsed_ms / 1000ULL) % SEC_PER_DAY);
}

void walltime_init(void)
{
    s_base_sec = 0;                 /* 未对时：从本地 00:00:00 起算 */
    s_base_ms  = timesvc_now_ms();  /* 以"现在"为计时起点，此后由 time_svc 走时 */
    s_synced   = 0;

    /*
     * init 一定会把状态清成"未对时"，这里仍按实际状态打日志：
     * 万一以后改成"复位时保留已对时结果"，日志不会说谎。全模块只此一条 INFO，
     * 取时路径不打日志（可能被屏显高频调用）。
     */
    ESP_LOGI(TAG, "init: synced=%d (0=从 00:00:00 本地起算)", s_synced);
}

void walltime_set_hms(int hh, int mm, int ss)
{
    /* 先把参数归一，再取单调时间样本：这样基准毫秒对应的就是"该时刻"的这一刻，
     * 参数校验耗时不会算进走时误差里。 */
    s_base_sec = hms_to_sec(hh, mm, ss);
    s_base_ms  = timesvc_now_ms();
    s_synced   = 1;
}

void walltime_now_hms(char *buf, size_t len)
{
    uint32_t sec;

    if (buf == NULL || len == 0) {
        return;                     /* 没有可写的缓冲，直接什么都不做 */
    }

    sec = wall_sec_now();
    /*
     * 用 snprintf 而不是手写 sprintf：它按 len 截断，调用方给的缓冲不足 9 字节时
     * 最多写 len-1 个字符再加 '\0'，绝不越界；这种情况内容会被截断，属于调用方
     * 违约（头文件要求 buf 至少 9 字节），本模块的底线是"不越界"。
     * sec 已对 86400 取模，所以小时位恒为 00..23，不会出现 24:xx。
     */
    snprintf(buf, len, "%02u:%02u:%02u",
             (unsigned)(sec / 3600U),
             (unsigned)((sec % 3600U) / 60U),
             (unsigned)(sec % 60U));
}

uint32_t walltime_now_sec(void)
{
    return wall_sec_now();
}

int walltime_is_synced(void)
{
    return s_synced ? 1 : 0;        /* 契约要求只回 0/1，不外露内部取值 */
}
