# driver/battery — 电池电压采集（ADC 分压）

> **方案**：ESP32-S3 内置 **ADC + 板上电阻分压网络**测电池电压。
> **INA228 电量计方案已取消**（不再走 I2C 外部电量计，GPIO4/5/6 现空闲）。
>
> **职责边界**：本驱动只把 ADC 读数变成**电池端电压 mV**，**不做电量百分比换算** ——
> 百分比是业务语义，放在 `service/power`（12 点分段线性查表 + 一阶滤波）。
> 本驱动已被编入构建（`main/CMakeLists.txt`），由 `service/power` 的 `power_init()` 调用。

## 引脚与分压网络

| 项 | 值 |
|---|---|
| 采样脚 | **GPIO7** = `ADC1_CH6`（`BSP_PIN_BATTERY_ADC` / `BSP_BATTERY_ADC_CHANNEL`） |
| ADC 单元 | `ADC_UNIT_1`（`BSP_ADC_BATTERY_UNIT`） |
| 衰减 / 位宽 | `ADC_ATTEN_DB_12` / `ADC_BITWIDTH_DEFAULT`（本 SoC 即 12 bit） |
| 分压比 | `V_bat = V_adc × 400/200`，即 **×2**（`BSP_BATTERY_DIV_NUM/DEN`） |

```text
BAT ──R59(200k)──┬──R61(1k)── ADC(GPIO7)
                 │
               R60(200k)      C13(100nF)
                 │               │
                GND             GND
```

- 采样节点电压 = `V_bat × R60/(R59+R60)` = `V_bat × 200/400` = `V_bat / 2`
- `R61(1k)` 是 ADC 前的限流电阻（ADC 输入高阻，不参与分压计算）；`C13(100nF)` 为滤波电容
- 满电 4.2 V → 2.1 V 入 ADC；放空 2.7 V → 1.35 V，均在 12 dB 量程内
- 分压电阻值写在 `bsp/pin_map.h`，换板只改那里

## 接口（`battery.h`）

| 接口 | 说明 |
|---|---|
| `battery_init()` | 建 ADC 单元、配通道（12 dB）、创建 curve-fitting 校准曲线。重复调用视为"已就绪"直接返回 `ERR_OK` |
| `battery_read_mv(uint32_t *mv)` | 连采 `BATTERY_SAMPLE_TIMES = 16` 次取平均 → 换算采样节点 mV → 按分压比还原电池端 mV；返回非 `ERR_OK` 时不修改 `*mv` |

错误码：`ERR_PARAM`（`mv == NULL`）、`ERR_NOT_READY`（未 init 或 init 失败）、`ERR_FAIL`（采样中途失败，整次作废，不给上层半截数据）。

## 实现要点

1. **连采取平均**：ADC 单次读数带 ±几十 mV 随机噪声，16 次算术平均可把随机噪声压到约 `1/√N`。不做排序去极值（均值已足够稳）。
2. **校准优先**：用 `adc_cali_create_scheme_curve_fitting()` 读 eFuse 出厂参数反算电压，精度明显优于"raw × 满量程 / 4095"。
   - 校准不可用时**不阻断启动**：退化为线性换算（满量程 `BATTERY_LINEAR_FS_MV = 3100 mV`、满码 `BATTERY_RAW_FULL_SCALE = 4095`），WARN 一次并仍返回 `ERR_OK`。
   - 初始化只打一条 INFO，写明本次用的是"curve-fitting 校准"还是"线性换算"，便于现场判读电量可信度。
3. **分压还原先乘后除**：用 64 位中转避免中间溢出（`node_mv × DIV_NUM / DIV_DEN`）。
4. **不做百分比**：`service/power` 负责滤波与查表。

> ⚠ 电量的绝对值精度取决于 `service/power` 里那张电压→电量表的**实机标定**；
> 本驱动保证的是"电压读得准"，不是"百分比准"。
