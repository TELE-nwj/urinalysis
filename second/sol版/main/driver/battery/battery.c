/* ============================================================
 * driver 层：锂电池电压采集（ADC 分压）
 *
 * 【职责边界】只把 ADC 原始读数变成"电池端电压 mV"，不做电量百分比换算 ——
 *   百分比是业务语义，归 service/power（见 battery.h 的定位说明）。
 *
 * 【为什么连采取平均】ADC 单次读数带随机噪声（±几十 mV），连采 BATTERY_SAMPLE_TIMES
 *   次求算术平均，可把随机噪声压到约 1/sqrt(N)。这里不做排序去极值：均值已足够稳，
 *   多一步只增加代码和内存，收益很小。
 *
 * 【为什么要校准】ADC 的增益/失调随芯片与温度漂移，直接按"raw × 满量程 / 4095"
 *   换算误差可达几十 mV；adc_cali(curve fitting) 用 eFuse 中的出厂校准参数反算电压，
 *   精度明显更好。但校准参数在部分裸片/测试板上可能缺失，此时不能卡住启动 ——
 *   退化为线性换算并 WARN 一次，仍返回 ERR_OK，让上层显示一个粗略电量。
 * ============================================================ */
#include "battery.h"

#include "bsp/pin_map.h"        /* ADC 单元/通道/分压比统一收敛在 bsp，驱动不写死引脚 */

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <stdbool.h>            /* bool：显式包含，不依赖其它头间接引入 */

static const char *TAG = "battery";

/* 降级路径（校准不可用）用的线性换算满量程：12bit 满码 4095 约对应 12dB 档的
 * 满量程 3100mV。这只是 ESP32-S3 12dB 档的经验值，未做逐片标定，精度不保证 ——
 * 正常路径必须走校准曲线。 */
#define BATTERY_LINEAR_FS_MV     3100u
#define BATTERY_RAW_FULL_SCALE   4095u

static adc_oneshot_unit_handle_t s_adc     = NULL;   /* ADC 单元句柄；NULL = 未初始化 */
static adc_cali_handle_t         s_cali    = NULL;   /* 校准句柄；仅 s_cali_ok 为真时有效 */
static bool                      s_cali_ok = false;  /* 校准曲线是否可用 */

err_t battery_init(void)
{
    /* 重复初始化保护：ADC 单元是全局资源，二次 new_unit 会返回 INVALID_STATE；
       上层若重复调用，这里直接当"已就绪"返回，避免无谓报错。 */
    if (s_adc != NULL) return ERR_OK;

    /* ---- 1) ADC 单元 ---- */
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = BSP_ADC_BATTERY_UNIT,   /* ADC1 */
        .ulp_mode = ADC_ULP_MODE_DISABLE,   /* 不用 ULP 协处理器，保持行为最简 */
    };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        s_adc = NULL;
        ESP_LOGE(TAG, "ADC 单元初始化失败");
        return ERR_FAIL;
    }

    /* ---- 2) 采样通道：12dB 衰减 ----
       分压后最高 2.1V（满电 4.2V/2），12dB 档约 3.1V 量程，既留了余量又不浪费分辨率；
       位宽交给驱动选默认（本 SoC 即最大 12bit），避免各型号写法不一致。 */
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc, BSP_BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败");
        adc_oneshot_del_unit(s_adc);        /* 回滚，保证失败后 s_adc 与硬件状态一致 */
        s_adc = NULL;
        return ERR_FAIL;
    }

    /* ---- 3) 校准曲线（curve fitting，用 eFuse 出厂参数）----
       失败不阻断启动：置 s_cali_ok=false，采样时走线性换算。 */
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BSP_ADC_BATTERY_UNIT,
        .chan     = BSP_BATTERY_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,   /* 必须与上面 chan_cfg 一致，否则折算系数不匹配 */
    };
    const esp_err_t cerr = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (cerr == ESP_OK) {
        s_cali_ok = true;
    } else {
        s_cali    = NULL;                   /* 未创建成功，明确置空避免误用 */
        s_cali_ok = false;
        ESP_LOGW(TAG, "ADC 校准不可用(%s)，退化为线性换算，电量精度下降",
                 esp_err_to_name(cerr));
    }

    /* 初始化只留这一条 INFO：写清本次用的是校准还是线性换算，便于现场判读电量可信度。 */
    ESP_LOGI(TAG, "电池 ADC 就绪(ADC1_CH%d, 12dB, 平均%u次, %s)",
             (int)BSP_BATTERY_ADC_CHANNEL, (unsigned)BATTERY_SAMPLE_TIMES,
             s_cali_ok ? "curve-fitting 校准" : "线性换算");
    return ERR_OK;
}

err_t battery_read_mv(uint32_t *mv)
{
    if (mv == NULL)    return ERR_PARAM;
    if (s_adc == NULL) return ERR_NOT_READY;    /* 未 init（或 init 失败）*/

    /* ---- 1) 连采取平均 ---- */
    uint32_t sum = 0;
    for (uint32_t i = 0; i < BATTERY_SAMPLE_TIMES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BSP_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            return ERR_FAIL;    /* 中途失败整次作废：不给上层半截数据，调用方保持旧值 */
        }
        sum += (uint32_t)raw;
    }
    const uint32_t raw_avg = sum / BATTERY_SAMPLE_TIMES;

    /* ---- 2) raw → 采样节点 mV ---- */
    uint32_t node_mv;
    if (s_cali_ok) {
        int out = 0;
        if (adc_cali_raw_to_voltage(s_cali, (int)raw_avg, &out) != ESP_OK || out < 0) {
            return ERR_FAIL;
        }
        node_mv = (uint32_t)out;
    } else {
        /* 线性换算：raw / 4095 × 3100mV。先乘后除并用 64 位中转，避免中间溢出。 */
        node_mv = (uint32_t)(((uint64_t)raw_avg * BATTERY_LINEAR_FS_MV)
                             / BATTERY_RAW_FULL_SCALE);
    }

    /* ---- 3) 采样节点 → 电池端电压 ----
       V_bat = V_node × DIV_NUM / DIV_DEN（本板 400/200，即 ×2）。
       分压比来自 bsp，换板子只改 pin_map.h；先乘后除，DIV_DEN 由 bsp 保证非 0。 */
    *mv = (uint32_t)(((uint64_t)node_mv * BSP_BATTERY_DIV_NUM) / BSP_BATTERY_DIV_DEN);
    return ERR_OK;
}
