# driver/drop — 液滴检测

> 每个尿滴落下 → GPIO 上升沿 → ISR 消抖并累加计数。
> 引脚：`BSP_PIN_DROP_DETECT = GPIO10`（见 `bsp/pin_map.h`）。
> 上层调用方：`app/monitor.c`。

## 数据与状态
```c
typedef struct {
    uint32_t total_count;  /* 累计有效滴数 */
    uint64_t last_ts_us;   /* 最近一次有效滴时间戳(us) */
    uint32_t debounce_us;  /* 消抖窗口(us)，默认 30000 = 30ms */
} drop_stats_t;
```
- `s_total_count` / `s_last_ts_us` / `s_debounce_us` 为 `volatile`，用自旋锁 `s_spin` 保护。
- `drop_isr` 用 `IRAM_ATTR` 标记（在中断上下文运行），**只做极短操作**：取时间、消抖比较、累加；**绝不打印/做慢操作**。

## 中断消抖逻辑
```c
if (now - last >= debounce_us) {   /* 距上次有效滴 ≥ 消抖窗才算有效 */
    s_last_ts_us = now;
    s_total_count++;
}
```

## 对外接口
| 接口 | 说明 |
|---|---|
| `drop_init()` | 配置 GPIO 上升沿中断，安装 ISR 服务并挂 `drop_isr`（调用一次） |
| `drop_reset()` | 复位计数（换病人/清零） |
| `drop_get_stats(out)` | 线程安全地取快照 |
| `drop_set_debounce_us(us)` | 修改消抖窗口（默认即可） |

## 设计要点
- 中断只维护**原子计数器**，不处理业务；上层任务轮询时取“差值”，避免丢滴或重复累计。
- ISR 内用 `portENTER_CRITICAL` 而非 OS 信号量，保证极短且安全。
