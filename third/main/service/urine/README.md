# service/urine — 尿量计量与判断（纯计算 + 采集代理）

> 本模块做两件事：① 把 `driver/drop` 的采集控制收进来（app 不直接碰 driver）；
> ② 把"要解算的数据（含判断/报警结果）"收进 `urine_meas_t`，`urine_poll()` 每次被调用时算好全部字段。
> app 层只管周期调用它、再把结果拿去显示/落盘，**不再自己算**。

---

## 1. 结果结构体 `urine_meas_t`

```c
typedef struct {
    uint32_t total_drops;   /* 会话累计滴数(从本次测量起点算起) */
    float    total_ml;      /* 会话累计尿量 ml */
    float    rate_mlh;      /* 实时速率 ml/h(最近一次有效间隔换算) */

    uint8_t  flag_dry;      /* 缺水/断流(方式 A Lack) */
    uint8_t  flag_bag_full; /* 尿袋满(累计 >= URINE_BAG_FULL_ML) */
} urine_meas_t;
```

> 注意：**没有** `flag_low_urine`（少尿由 `app/S1` 的 24 小时结论给出）。

模块内部持有唯一一份 `s_meas`，app 通过 `urine_get()` 读只读指针 —— "累计/速率/缺水/满袋"只有一个写者。

---

## 2. 接口

### 采集控制（封装 `driver/drop`）

| 接口 | 说明 |
|---|---|
| `urine_capture_init()` | 初始化液滴采集硬件（`drop_init()`，配 GPIO 中断）；由 `main` 启动阶段调用一次 |
| `urine_capture_arm()` | 开始/继续采集：关中断 → 清旧间隔与待处理队列（`drop_reset_interval()`）→ 开中断 |
| `urine_capture_hold()` | 暂停采集：关滴落中断（期间落下的滴被丢弃） |
| `urine_capture_clear()` | 只清间隔缓冲，不动中断开关（复位流程里配合 `urine_capture_hold()` 使用） |

### 会话数据

| 接口 | 说明 |
|---|---|
| `urine_reset()` | 清零会话结果并重记"测量起点"（开始新测量 / 复位时调用）；调用前应先 `urine_capture_hold()` |
| `urine_poll()` | 每周期调用（`monitor` 的 20 ms 循环）：`drop_poll()` 搬运 + 纯解算。**会话计时不归它管**，由 `app/S1` 累加 |
| `urine_get()` | 只读的当前解算结果 |
| `urine_rate_zero()` | 把实时速率清成 0（暂停冻结、继续起算时用，避免屏上停在旧读数） |

---

## 3. 速率算法（**有 7 秒超时**）

```c
uint64_t us   = drop_last_interval_us();   /* 最近一次有效相邻间隔(µs) */
uint64_t last = drop_last_time_us();       /* 最近一滴的时刻 */
uint64_t now  = (uint64_t)esp_timer_get_time();

/* ① 瞬时速率：rate = 每滴ml ÷ 间隔秒 × 3600 */
if (us > 0) {
    s_meas.rate_mlh = 3600.0f * URINE_DROP_ML_DEFAULT / ((float)us / 1e6f);
}
/* us == 0(尚无有效间隔)时保持旧值 */

/* ② 超时归零：距最近一滴超过 URINE_RATE_ZERO_MS(7s) → 显示 0 */
if (last != 0 && (now - last) > (uint64_t)URINE_RATE_ZERO_MS * 1000ULL) {
    s_meas.rate_mlh = 0.0f;
}
```

| 情况 | 屏上速率 |
|---|---|
| 正常滴落（1 滴/秒，0.5 ml/滴） | 180.0 |
| 最后一滴之后 **7 秒内** | 保持最后一次算出的值 |
| 最后一滴之后 **超过 7 秒** | **0.0** |
| 新的一滴到来 | 立刻按新间隔重算 |

- 判定基准是**硬件滴落时刻**（ISR 里用 `esp_timer_get_time()` 打的），不是软件轮询时刻，所以 7s 很准。
- **只影响"实时速率"**。U 盘里那条"本分钟平均流速"由 `app/S1` 用 `本分钟尿量 × 60` 单独算，不受影响。

---

## 4. 其他判定

### 缺水 / 断流（方式 A）

`drop_poll()` 每攒满一轮 `DROP_WIN_SIZE = 255` 个间隔就置"窗口完成"事件；解算时收到后统计：

```c
if (drop_window_done()) {
    slow = Time[1..255] 中 > URINE_DRY_SEC(4s) 的个数;
    s_meas.flag_dry = (s_meas.total_drops > 0 && slow > URINE_DRY_CNT(100)) ? 1 : 0;
}
```

### 尿袋满

```c
s_meas.flag_bag_full = (s_meas.total_ml >= URINE_BAG_FULL_ML) ? 1 : 0;
```

### 累计（会话级）

```c
session = drop_total() - s_base;            /* 减去本次测量起点(urine_reset 时重记) */
delta   = session - s_meas.total_drops;
s_meas.total_drops = session;
s_meas.total_ml   += delta * URINE_DROP_ML_DEFAULT;
```

> `driver` 的累计滴数**永不清零**，本模块用"总累计 − 本次测量起点"得会话量，所以每次开始测量/复位都要重记 `s_base`。

---

## 5. 参数（`urine.h`，都可改）

| 宏 | 默认 | 含义 |
|---|---|---|
| `URINE_DROP_ML_DEFAULT` | 0.5f | **每滴 ml，必须按实物标定** |
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
| `drop_poll()` | 把 ISR 里的间隔搬进 `Time[]`（`urine_poll()` 第一步就调它） |
| `drop_total()` | 永不清零的累计滴数（算会话增量） |
| `drop_last_interval_us()` | 最近一次相邻间隔（算瞬时速率） |
| `drop_last_time_us()` | 最近一滴的时刻（**7s 超时判定**） |
| `drop_window_done()` / `drop_interval_sec()` | 方式A 缺水判定 |
| `drop_irq_disable/enable()`、`drop_reset_interval()` | 采集开关与间隔清理 |

---

## 7. 与 app 层的分工（暂停/继续要知道的）

- `monitor` 每 20 ms 调一次 `urine_poll()`，**与状态无关**（待开始/暂停也在跑）。
- 暂停：`S2_enter()` 先结算，然后 `urine_capture_hold()` **关滴落中断** → 暂停期间落下的滴被丢弃、不计入；`urine_capture_clear()` 清掉旧间隔。
- 继续：`urine_capture_arm()` 清旧间隔 + 开中断，会话累计从暂停前的值接着走。
- 复位：`urine_capture_hold()` → `urine_capture_clear()` → `urine_reset()`（重记测量起点）。

---

## 8. 排查建议

- 屏上速率一直不归零 → 确认处于**测量中**（只有测量中 `app/S1` 才推屏），且U盘事务已正常退出；写盘期间暂关液滴中断、暂停推屏，退出后恢复原采集状态并继续周期解算。
- 速率明显偏大/偏小 → 先标定 `URINE_DROP_ML_DEFAULT`。
- 滴数在涨但尿量不变 → 看 `URINE_DROP_ML_DEFAULT` 是否被改成 0。
- 缺水标志一直为 0 → 该判定要**攒满 255 个间隔**才跑一次，滴很慢时要等很久。
