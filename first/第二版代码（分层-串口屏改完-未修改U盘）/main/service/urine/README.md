# service/urine — 尿量计量与判断（纯计算）

> 把所有"要解算的数据(含判断/报警结果)"收进 `urine_meas_t`，
> 由 `urine_update()` 每次喂入"新增滴数+时刻"后自动算好。
> app 层(monitor)只周期喂数据、然后把结果拿去显示/落盘，不再自己算。

## 结果结构体 `urine_meas_t`
```c
typedef struct {
    uint32_t total_drops;    /* 累计滴数 */
    float    total_ml;       /* 累计尿量 ml = 滴数 × 每滴ml */
    float    rate_mlh;       /* 实时速率 ml/h(60s 窗口结算) */
    uint8_t  flag_low_urine; /* 少尿: 有尿但速率 < 17ml/h */
    uint8_t  flag_bag_full;  /* 尿袋满: 累计 ≥ 2000ml */
} urine_meas_t;
```

## 接口
| 接口 | 说明 |
|---|---|
| `urine_meas_init(m)` | 清零结果 |
| `urine_update(m, delta_drops, now_ms)` | 每周期喂入新增滴数+时刻，内部累计/算速率/做判断 |
| `urine_meas_reset(m)` | 清零结果与内部速率窗口(换病人/确认) |

## 参数(可在头文件改)
`URINE_DROP_ML_DEFAULT=0.05f`(ml/滴, 需标定)、`URINE_LOW_RATE_MLH=17.0f`、
`URINE_BAG_FULL_ML=2000.0f`、`URINE_RATE_WINDOW_MS=60000`。

> 本次范围只做不依赖 RTC 的部分。分时(时段)/24h 统计待接入 RTC 后扩展。
