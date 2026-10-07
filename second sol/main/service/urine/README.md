# service/urine — 尿量计量与判断（纯计算）

> 把所有"要解算的数据（含判断/报警结果）"收进 `urine_meas_t`，
> `urine_update()` 每次被调用时自动算好全部字段。
> app 层只管周期调用它、再把结果拿去显示/落盘，**不再自己算**。

---

## 1. 结果结构体 `urine_meas_t`

```c
typedef struct {
    uint32_t total_drops;   /* 会话累计滴数(从本次测量起点算起) */
    float    total_ml;      /* 会话累计尿量 ml */
    float    rate_mlh;      /* 实时速率 ml/h(瞬时，见 §3) */

    uint8_t  flag_dry;      /* 缺水/断流(方式A Lack) */
    uint8_t  flag_bag_full; /* 尿袋满(累计 >= URINE_BAG_FULL_ML) */
} urine_meas_t;
```

> 注意：**没有** `flag_low_urine`（少尿判断已移除），速率也不再走 60s 窗口。

---

## 2. 接口

| 接口 | 说明 |
|---|---|
| `urine_update(m)` | 每周期解算（**需先调用 `drop_poll()`**） |
| `urine_meas_reset(m)` | 清零结果 + 重记测量起点（开始新测量/换病人） |

---

## 3. 速率算法（**有 7 秒超时**）

```c
uint64_t us   = drop_last_interval_us();   /* 最近一次有效相邻间隔(µs) */
uint64_t last = drop_last_time_us();       /* 最近一滴的时刻 */
uint64_t now  = (uint64_t)esp_timer_get_time();

/* ① 瞬时速率：rate = 每滴ml ÷ 间隔秒 × 3600 */
if (us > 0) {
    m->rate_mlh = 3600.0f * URINE_DROP_ML_DEFAULT / ((float)us / 1e6f);
}

/* ② 超时归零：距最近一滴超过 URINE_RATE_ZERO_MS(7s) → 显示 0 */
if (last != 0 && (now - last) > (uint64_t)URINE_RATE_ZERO_MS * 1000ULL) {
    m->rate_mlh = 0.0f;
}
```

| 情况 | 屏上速率 |
|---|---|
| 正常滴落（1 滴/秒） | 180.0 |
| 最后一滴之后 **7 秒内** | 保持最后一次算出的值 |
| 最后一滴之后 **超过 7 秒** | **0.0** |
| 新的一滴到来 | 立刻按新间隔重算 |

- 判定基准是**硬件滴落时刻**（ISR 里用 `esp_timer_get_time()` 打的），不是软件轮询时刻，所以 7s 很准。
- **只影响"实时速率"**。U 盘里那条"本分钟平均流速"由 S3 用 `本分钟尿量 × 60` 单独算，不受影响。

---

## 4. 其他判定

### 缺水 / 断流（方式 A）

`drop_poll()` 每攒满一轮 255 个间隔就置"窗口完成"事件；`urine_update()` 收到后统计：

```c
if (drop_window_done()) {
    slow = Time[1..255] 中 > URINE_DRY_SEC(4s) 的个数;
    m->flag_dry = (m->total_drops > 0 && slow > URINE_DRY_CNT(100)) ? 1 : 0;
}
```

### 尿袋满

```c
m->flag_bag_full = (m->total_ml >= URINE_BAG_FULL_ML) ? 1 : 0;
```

### 累计（会话级）

```c
session = drop_total() - s_base;            /* 减去本次测量起点 */
delta   = session - m->total_drops;
m->total_drops = session;
m->total_ml   += delta * URINE_DROP_ML_DEFAULT;
```

> 用"driver 里永不清零的 total 减去基线"，所以**暂停期间落下的滴会在恢复后被算进来**
> （暂停只冻结计时，不停滴落计数）。

---

## 5. 参数（`urine.h`，都可改）

| 宏 | 默认 | 含义 |
|---|---|---|
| `URINE_DROP_ML_DEFAULT` | 0.5f | **每滴 ml，必须标定** |
| `URINE_BAG_FULL_ML` | 2000.0f | 尿袋满阈值 ml |
| `URINE_RATE_ZERO_MS` | **7000** | 距最近一滴超过 7s → 实时速率显示 0 |
| `URINE_DRY_SEC` | 4 | 方式A：单个间隔 >4s 视为过慢 |
| `URINE_DRY_CNT` | 100 | 方式A：一轮 255 个里 >100 个过慢 → 缺水 |
| `URINE_AVG_N` | 2 | 历史遗留（瞬时速率下已不用） |

---

## 6. 依赖

读 `driver/drop` 的：

| 接口 | 用途 |
|---|---|
| `drop_total()` | 永不清零的累计滴数（算会话增量） |
| `drop_last_interval_us()` | 最近一次相邻间隔（算瞬时速率） |
| `drop_last_time_us()` | 最近一滴的时刻（**7s 超时判定**） |
| `drop_window_done()` / `drop_interval_sec()` | 方式A 缺水判定 |

---

## 7. 排查建议

- 屏上速率一直不归零 → 确认跑的是本工程固件（`flash.bat`），且处于 **S3 测量中**（只有 S3 才刷新屏）
- 速率明显偏大/偏小 → 先标定 `URINE_DROP_ML_DEFAULT`
- 滴数在涨但尿量不变 → 看 `URINE_DROP_ML_DEFAULT` 是否被改成 0
- 缺水标志一直为 0 → 该判定要**攒满 255 个间隔**才跑一次，滴很慢时要等很久
