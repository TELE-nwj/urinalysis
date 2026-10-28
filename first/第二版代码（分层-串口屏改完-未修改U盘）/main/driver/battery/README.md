# driver/battery — 电池电量检测（占位 stub）

> ⚠️ **当前为占位实现**，未接入真实 ADC 采样。
> `main.c` 仍会调用 `battery_init()`，但实际无效果。

| 接口 | 现状 |
|---|---|
| `battery_init()` | 打日志，返回 `ERR_NOT_SUP` |
| `battery_read_percent(int*)` | 返回 `ERR_NOT_SUP` |
| `battery_low_flag()` | 恒返回 `0`（不报低电） |

## 引脚预留
`bsp/pin_map.h` 预留了：
- `BSP_ADC_BATTERY_UNIT = ADC_UNIT_1`
- `BSP_PIN_BATTERY_ADC  = GPIO7`

## 后续接入建议
- 用 IDF `esp_adc`/`esp_driver_*` 做电池电压采样，换算电量百分比；
- 实现 `battery_read_percent` 与 `battery_low_flag`；
- 视需要调整 `main/CMakeLists.txt` 的 `REQUIRES`（补充 ADC 组件）。
