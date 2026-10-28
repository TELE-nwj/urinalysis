/* ============================================================
 * service 层：电池电量业务（电压 → 百分比）
 *
 * 【分层意义】driver/battery 只给"电池端电压 mV"；本模块把电压翻译成电量百分比，
 *   并做一阶滤波去抖，供 app 层显示。app 只调 power_percent()，完全不接触 ADC。
 *
 * 【为什么滤波】带负载时电压会随瞬时电流波动（液滴泵、串口屏刷新都会拉一下），
 *   直接查表会让百分比来回跳。一阶低通把这种快抖动压掉，代价是几秒的跟随延迟 ——
 *   电量显示本来就只需要秒级响应。
 *
 * 【为什么用分段线性 LUT 而不是公式】锂电池放电曲线中段平坦、两端陡，单一公式拟合
 *   要么中段偏差大、要么两端不准；查表插值直观、可逐个刻度标定，现场调表也只改数据。
 * ============================================================ */
#include "power.h"

#include "driver/battery/battery.h"     /* service 只消费 driver 给出的"电池端 mV" */
#include "esp_log.h"
#include <stdbool.h>            /* bool：显式包含，不依赖其它头间接引入 */

static const char *TAG = "power";

/* ---------- 一阶低通滤波系数 α（新值权重）----------
 * α 越大越信新值（跟随快、抖动大），越小越平滑（抖动小、跟随慢）。
 * 取 0.3：在 POWER_POLL_PERIOD_MS = 1000ms 下，时间常数约 1/α ≈ 3.3 个采样周期，
 * 足以吃掉带载瞬间的压降毛刺，又能在十几秒内跟上真实的电量变化。
 * 用 float 是因为 S3 有硬件 FPU，1Hz 调用的开销可忽略，且比定点写法直观、不易写错。 */
#define POWER_FILTER_ALPHA      0.3f

/* ---------- 电压 → 电量 分段线性表（LUT）----------
 * ⚠ 表中数值是按电池资料推算，必须实机标定后定稿：
 *   依据 = 126280 锂聚合物电池（10000mAh，标称 3.8V，充电截止 4.20V，
 *   保护板放电保护 2.7V）的典型放电曲线；未标定前只保证"电压↔电量单调"，
 *   绝对值可能有 ±10% 量级偏差。
 *   标定方法 = 用可调电源从 4.20V 起按 0.1V 步进下调，每档静置后记录屏显电压，
 *   并与恒流放电的 Ah 积分（真实剩余电量）对应，逐个替换下表的 mV/percent 对。
 *
 * 表按电压降序（高电压在前）排列，插值函数依赖该顺序做区间查找。 */
typedef struct {
    uint16_t mv;        /* 电池端电压 mV */
    uint8_t  percent;   /* 对应电量 % */
} power_lut_t;

static const power_lut_t s_lut[] = {
    { 4200, 100 },      /* 充电截止 */
    { 4050,  90 },
    { 3950,  80 },
    { 3870,  70 },
    { 3820,  60 },
    { 3800,  50 },      /* 平台段：50% 附近电压变化最平，最容易跳变，故点最密 */
    { 3770,  40 },
    { 3740,  30 },
    { 3700,  20 },
    { 3600,  10 },
    { 3400,   5 },
    { 3000,   0 },      /* 3.0V 以下留给保护板动作，不再细分 */
};

/* ---------- 有效电压下限 ----------
 * 电池端电压低于此值即判为"无效读数"：保护板的过放保护在 2.7V 附近动作，能正常
 * 工作的电池不会低于它；读到更低的值多半是 ADC 引脚悬空 / 没接电池 / 校准异常，
 * 而不是真实电压 —— 直接采纳会让电量瞬间掉到 0%，现场会误判成设备故障。
 * 阈值留 200mV 余量取 2.5V（线损与带载瞬时压降也能吃掉）。 */
#define POWER_MV_VALID_MIN      2500u

/* 以下三个状态只被 app 的 monitor 任务读写（power_poll() 与 power_percent() 同任务、
 * 1Hz 串行调用），因此不加锁、不用 volatile。若将来多任务调用需自行加保护。 */
static bool     s_ready = false;    /* 是否已采到过有效电压 */
static uint32_t s_mv    = 0;        /* 滤波后的电池端电压 mV */
static int      s_pct   = -1;       /* 对应电量 %；未就绪为 -1（屏上显示 "--"）*/

/* 电压 → 电量：在 LUT 上做分段线性插值。表是降序的，两端先夹紧再找区间。 */
static int lut_percent(uint32_t mv)
{
    const size_t n = sizeof(s_lut) / sizeof(s_lut[0]);

    if (mv >= s_lut[0].mv)       return s_lut[0].percent;        /* 高于表顶 → 100 */
    if (mv <= s_lut[n - 1].mv)   return s_lut[n - 1].percent;    /* 低于表底 → 0   */

    for (size_t i = 0; i + 1 < n; i++) {
        const power_lut_t *hi = &s_lut[i];        /* 区间高电压端 */
        const power_lut_t *lo = &s_lut[i + 1];    /* 区间低电压端 */
        if (mv <= hi->mv && mv >= lo->mv) {
            /* p = lo% + (mv - lo.mv) × (hi% - lo%) / (hi.mv - lo.mv)
               +span/2 是为了四舍五入；hi.mv > lo.mv 由表的降序保证，无除零风险；
               乘法上限 1300 × 95 ≈ 1.2e5，u32 不会溢出。 */
            const uint32_t span = (uint32_t)(hi->mv - lo->mv);
            const uint32_t d    = mv - lo->mv;
            const uint32_t dp   = (uint32_t)(hi->percent - lo->percent);
            return (int)lo->percent + (int)((d * dp + span / 2u) / span);
        }
    }
    return s_lut[n - 1].percent;    /* 兜底：两端已夹紧，正常到不了这里 */
}

err_t power_init(void)
{
    /* 底层 ADC 的初始化由本 service 负责（组合根只调 service 的 init），
       这样 main/app 层都不会出现 ADC 细节。 */
    const err_t e = battery_init();
    if (e != ERR_OK) {
        ESP_LOGE(TAG, "电池 ADC 初始化失败(%d)，电量将一直显示 --", (int)e);
        return e;
    }

    /* 复位为"未就绪"：还没采到电压时必须先给 -1，
       否则屏上会显示 0%（像没电）而不是"--"（未知）。 */
    s_ready = false;
    s_mv    = 0;
    s_pct   = -1;

    ESP_LOGI(TAG, "电量服务就绪(每 %ums 轮询一次)", (unsigned)POWER_POLL_PERIOD_MS);
    return ERR_OK;
}

void power_poll(void)
{
    uint32_t mv = 0;

    /* 异常日志限流：ADC 读失败与"读数不合理"共用同一个翻转标志 —— 两者都属于
       "本次采样不可用"，长期异常时只在"正常 → 异常"时报一条，恢复正常后再次异常
       才再报，避免 1Hz 刷屏把串口里有用的信息冲掉。 */
    static bool warn_logged = false;

    if (battery_read_mv(&mv) != ERR_OK) {
        if (!warn_logged) {
            ESP_LOGW(TAG, "电池电压采样失败，电量保持上次值");
            warn_logged = true;
        }
        return;
    }

    /* 先把滤波候选值算出来，判无效就整次丢弃（不落 s_mv / s_pct）。
       首个样本直接取原值而不是从 0mV 起滤波：否则滤波会慢慢爬升，开机会先显示
       一个假电量。 */
    const uint32_t mv_new = s_ready
                          ? (uint32_t)((float)s_mv + POWER_FILTER_ALPHA * ((float)mv - (float)s_mv))
                          : mv;

    if (mv_new < POWER_MV_VALID_MIN) {
        /* 无效读数：丢弃本次采样，三个状态一律不变。
           power_mv() 这里选择"冻结上一次有效值"，而不是清零返回 0 —— 拔电池/换电池
           的瞬间屏上电量突然变成 "--"，现场会误判成设备故障；冻结旧值更像"读数保持"。
           从未采到过有效值时 s_ready 仍为 false，两个 getter 自然给出 0 和 -1。 */
        if (!warn_logged) {
            ESP_LOGW(TAG, "电池电压异常(%umV < %umV 下限)，忽略本次采样",
                     (unsigned)mv_new, (unsigned)POWER_MV_VALID_MIN);
            warn_logged = true;
        }
        return;
    }
    warn_logged = false;

    s_mv    = mv_new;
    s_ready = true;

    /* 电量显示只减不增(单调不增)：
     *   放电过程中电池电压会随负载、温度波动，直接查表会让屏上百分比来回跳，
     *   甚至出现"越用越多"的观感。这里把它压成单调不增——只有新值比当前显示值
     *   更低时才更新，更高就继续显示原值。
     *   s_pct 同时兼任"已显示水位"：s_pct < 0 表示还没显示过，直接采用首个有效值。
     *   ⚠ 换电池或充电后电量会真实回升，本逻辑会把它压住；重新上电
     *     (power_init 会把水位复位成 -1) 即可恢复真实读数。 */
    const int pct_new = lut_percent(s_mv);
    if (s_pct < 0 || pct_new < s_pct) {
        s_pct = pct_new;
    }

    /* 周期采样不打日志：1Hz 一条会把串口刷满，正常运行时不输出任何信息。 */
}

int power_percent(void)
{
    return s_ready ? s_pct : -1;
}

uint32_t power_mv(void)
{
    return s_ready ? s_mv : 0;
}
