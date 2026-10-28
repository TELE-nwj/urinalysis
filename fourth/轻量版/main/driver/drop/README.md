# driver/drop — 液滴检测（方式 A：只采集，不判断）

> 每个尿滴 → GPIO10 上升沿 → ISR 消抖、记录时刻、把相邻间隔入队。
> 任务上下文周期调 `drop_poll()` 把间隔搬进环形表 `Time[]`。
> **判断（缺水/速率/累计量）在上层 `service/urine`**；采集开关也由 `service/urine` 代理。

---

## 1. 采集链路

```
GPIO10 上升沿
  → drop_isr()          ① 0.5s 消抖  ② s_total++  ③ 相邻间隔(µs)入队  ④ 记 s_prev_drop_us
  → drop_poll()         任务上下文：出队 → 换算成秒(饱和255) → 写入 Time[1..255]
                        攒满 255 个间隔 → 置"窗口完成"事件
  → service/urine       drop_total() / drop_last_interval_us() / drop_last_time_us()
                        / drop_window_done() / drop_interval_sec()
```

调用者链路：`app/monitor` 每 20 ms → `urine_poll()` → `drop_poll()`。

---

## 2. 参数（`drop.h`）

| 宏 | 默认 | 含义 |
|---|---|---|
| `DROP_DEBOUNCE_US` | 500000 | **0.5s 消抖**（距上一有效滴 < 0.5s 视为抖动） |
| `DROP_WIN_SIZE` | 255 | 一轮统计的间隔数（方式 A 用） |
| `DROP_RING_SIZE` | 256 | `Time[]` 容量（下标 1..255，弃用 Time[0]） |

---

## 3. 接口

| 接口 | 说明 |
|---|---|
| `drop_init()` | 配 GPIO10 上升沿中断、装 ISR 服务、挂 `drop_isr`（调用一次；由 `urine_capture_init()` 调）。ISR 服务若已被别的模块装过（`ESP_ERR_INVALID_STATE`）视为正常，挂中断失败则报错返回 |
| `drop_poll()` | **周期调用**（20 ms 一次）：排空队列、写 `Time[]`、维护累计与窗口事件 |
| `drop_total()` | 累计有效滴数（**永不清零**，上层算会话增量时减基线） |
| `drop_interval_sec(i)` | 读 `Time[i]`（i=1..255），该相邻间隔的秒数（>255 饱和为 255） |
| `drop_newest()` | 最近写入的 `Time[]` 下标（当前无调用者） |
| `drop_last_interval_us()` | 最近一次有效相邻间隔（µs） |
| `drop_last_time_us()` | **最近一滴的时刻**（`esp_timer` µs，0=尚无滴） |
| `drop_window_done()` | "窗口完成"事件（返回 1 时清除标志），上层据此跑一次缺水判定 |
| `drop_irq_disable()` | **临时关闭**液滴 GPIO 中断 |
| `drop_irq_enable()` | 恢复液滴 GPIO 中断 |
| `drop_reset_interval()` | 清掉旧间隔和待处理队列（**须在滴落中断关闭时调用**） |

### 采集开关用在哪

由 `service/urine` 代理，`app/monitor` 在状态转移时调用：

```c
/* service/urine/urine.c */
urine_capture_arm() : drop_irq_disable(); drop_reset_interval(); drop_irq_enable();
urine_capture_hold(): drop_irq_disable();
urine_capture_clear(): drop_reset_interval();
```

- 待开始/暂停 → 中断关闭，此时落下的滴被丢弃；
- K1 开始或继续测量 → `urine_capture_arm()` 清旧间隔并重新打开中断；
- 复位流程 → 先 `hold()` 再 `clear()`，避免清点与中断写点竞争。

> 注意：**U 盘写盘临界区不再关滴落中断**（`app/S1` 只拿 SPI 锁 + 暂停推屏 + 把 `CH376T` / `storage` 两个 TAG 的日志等级先保存再屏蔽），
> 否则介质重试期间会漏计。关中断只用于上面这几个很短的时刻。

### `drop_last_time_us()` 用在哪

`service/urine` 用它做**速率超时归零**：距最近一滴超过 `URINE_RATE_ZERO_MS`(7s)
就把实时速率显示成 0，否则屏上会一直停在最后一次的读数上。

---

## 4. 设计要点

- ISR 只做**极短操作**：取时间、消抖比较、入队、计数；`IRAM_ATTR` 标记，绝不打印
- ISR ↔ 任务 之间用 `portMUX_TYPE` 自旋锁保护；间隔用**小环形队列**传递
- `s_last_interval_us` 只在 `drop_poll()` 里更新（任务侧），ISR 不碰
- 64 位变量（如最近一滴时刻）在任务侧读取时要 `portENTER_CRITICAL` 保护

---

## 5. 排查建议

| 现象 | 可能原因 |
|---|---|
| 滴数不涨 | 传感器接反/无供电；或消抖窗 `DROP_DEBOUNCE_US` 比实际滴速还长；或中断被 `urine_capture_hold()` 关着（待开始/暂停态正常如此） |
| 滴数涨得比实际快 | 信号毛刺；0.5s 消抖只能抑制高频抖动，慢速毛刺要靠硬件滤波 |
| 速率一直不归零 | 上层没在跑 `urine_poll()`；或一直停在最后一次读数上（超过 7s 才会归零） |
| 缺水标志一直为 0 | 该判定要攒满 **255 个间隔**才跑一次，滴很慢时要等很久 |
