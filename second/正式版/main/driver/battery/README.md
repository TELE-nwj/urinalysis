# driver/battery — 电池电压检测（ADC 分压）

> **方案**：ESP32-S3 内置 **ADC + 板上电阻分压网络**测电池电压，再换算电量百分比。
> **INA228 电量计方案已取消**（不再走 I2C 外部电量计）。
>
> ⚠️ **代码尚未实现**：`battery.c` 目前仍是 stub，三个接口都返回 `ERR_NOT_SUP`；
> 且 `main/CMakeLists.txt` 里 `battery.c` 仍处于注释状态，**未编入构建**。
> 引脚宏已在 `bsp/pin_map.h` 中就位，驱动本体待写。

## 引脚

| 项 | 值 |
|---|---|
| 采样脚 | **GPIO7**（ESP32-S3 对应 `ADC1_CH6`） |
| 检测方式 | 电池电压经**电阻分压**后接到 GPIO7 |
| ADC 单元 | `ADC_UNIT_1`（ADC1） |

对应的宏已在 `bsp/pin_map.h` 里定义好：

```c
#define BSP_ADC_BATTERY_UNIT  ADC_UNIT_1    /* ADC1 */
#define BSP_PIN_BATTERY_ADC   GPIO_NUM_7    /* 分压采样脚 (ESP32-S3 ADC1_CH6) */
```

## 接口（现状）

| 接口 | 现状 |
|---|---|
| `battery_init()` | stub：打日志，返回 `ERR_NOT_SUP` |
| `battery_read_percent(int*)` | stub：返回 `ERR_NOT_SUP` |
| `battery_low_flag()` | stub：恒返回 `0`（不报低电） |

## 实现要点（待做）

1. **ADC 采样**：用 IDF `esp_adc`，单元 `ADC_UNIT_1`、通道 `ADC_CHANNEL_6`（GPIO7），
   衰减建议 `ADC_ATTEN_DB_12`（满量程约 3.3V），做多次采样取平均降噪。
2. **分压还原**：`V_bat = V_adc × (R1 + R2) / R2`。**R1/R2 实际阻值待确认**，
   拿到后补进本文档，并把分压比写成 `pin_map.h` 里的宏而不是硬编码。
3. **电压→电量**：锂电池放电曲线非线性，需分段线性/查表，不能简单线性换算。
4. **低电判定**：`battery_low_flag()` 按电压阈值（如 3.4V）返回 0/1。
5. **构建**：把 `esp_adc` 加进 `main/CMakeLists.txt` 的 `REQUIRES`，
   并把 `driver/battery/battery.c` 加进 `SRCS`。
   另外别忘了在 `main.c` 里调一次 `battery_init()`。

> ❓ **待确认**：分压电阻 R1 / R2 的阻值（决定分压比与满量程电压）。

