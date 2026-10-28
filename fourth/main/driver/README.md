# driver 层 — 底层硬件驱动

> 每个外设一个独立子目录，向 service 层提供功能接口，内部实现硬件细节。
> 引脚/外设号统一收敛在 `bsp/pin_map.h`，换板只改 bsp。

| 子模块 | 目录 | 状态 | 被谁使用 | 文档 |
|---|---|---|---|---|
| U盘(USB主机) | `ch376/` | 可用 | `service/storage` | [`ch376/README.md`](ch376/README.md) |
| 液滴检测 | `drop/` | 可用 | `service/urine` | [`drop/README.md`](drop/README.md) |
| 串口屏 UART | `screen_uart/` | 可用（只发不收） | `service/screen` | [`screen_uart/README.md`](screen_uart/README.md) |
| 按键 ×2 | `key/` | 可用 | `service/input` | [`key/README.md`](key/README.md) |
| 电池 | `battery/` | 可用（ADC 分压 @ GPIO7） | `service/power` | [`battery/README.md`](battery/README.md) |
| 单调时基 | `time_svc/` | 可用 | `service/clock`、`driver/walltime` | [`time_svc/README.md`](time_svc/README.md) |
| 墙上时间 | `walltime/` | 可用（未对时从 00:00:00 起） | `service/clock` | [`walltime/README.md`](walltime/README.md) |

---

## 1. 各模块一句话

| 模块 | 接口面 |
|---|---|
| `ch376` | `CH376_PORT_INIT()`、`CH376T_Get_IC_Ver()`、`CH376T_SetUsbMode()`、`CH376T_DiskCheckRaw()`、`CH376T_DISK_MOUNT()`、`CH376T_FILE_OPEN_NAME()`、`CH376T_FILE_CREATE_NAME()`、`CH376T_BYTE_WRITE_DATA()`、`CH376T_BYTE_LOCATE()`、`CH376T_FILE_CLOSE()`、`CH376T_GetFileSize()` |
| `drop` | `drop_init()`、`drop_poll()`、`drop_irq_disable/enable()`、`drop_reset_interval()`、`drop_total()`、`drop_interval_sec()`、`drop_newest()`、`drop_last_interval_us()`、`drop_last_time_us()`、`drop_window_done()` |
| `screen_uart` | `screen_uart_init()`、`screen_uart_send()`、`screen_uart_send_str()` —— **只有发送，没有接收** |
| `key` | `key_init(cb)`、`key_scan()`；事件 `KEY1_PRESS` / `KEY2_PRESS`（只有短按） |
| `battery` | `battery_init()`、`battery_read_mv(uint32_t *mv)` —— 只给"电池端 mV"，不算百分比 |
| `time_svc` | `timesvc_init()`、`timesvc_now_ms()` —— 开机以来单调毫秒 |
| `walltime` | `walltime_init()`、`walltime_now_hms()`、`walltime_set_hms()`、`walltime_now_sec()`、`walltime_is_synced()` |

---

## 2. 依赖方向

```
main → app → service → driver → bsp
```

- driver 只被 `service` 使用，**不反向依赖**；app 层不再直接调用任何 driver。
- driver 之间**只有一处例外**：`walltime` 以 `time_svc` 的单调毫秒为走时基准（`walltime.c` include `driver/time_svc/time_svc.h`）。`drop` 不调 `ch376`，`key` 不调 `screen_uart`，以此类推。

---

## 3. 关键约定

| 约定 | 说明 |
|---|---|
| 中断服务必须极短 | 如 `drop_isr` 只做消抖 + 入队 + 计数，绝不打印/做业务 |
| 共享变量用自旋锁 | `drop` 用 `portMUX_TYPE` 保护 ISR/任务之间共享的变量 |
| 64 位变量读要保护 | ISR 里在写 `uint64_t` 时，任务侧读要 `portENTER_CRITICAL` |
| 不打印业务日志 | driver 只打硬件相关日志；写盘临界区内 `app/S1` 会把 `CH376T`（driver/ch376）与 `storage`（service/storage）两个 TAG 的日志等级**先保存再**降到 `ESP_LOG_NONE`，退出时按保存值恢复（不硬编码写回某个级别） |
| 关滴落中断要成对 | `drop_irq_disable()` / `drop_irq_enable()` 期间落下的滴会被丢弃，只用于极短的临界段 |

---

## 4. 已知遗留

1. `ch376/ch376_spi.c` 里旧接口 **`CH376T_FILE_CREATE()` / `CH376T_BYTE_WRITE()` / `CH376T_SET_FILE_NAME()`**
   以及 `CH376T_CheckExist()` / `CH376T_SetBaudrate()` / `CH376T_EnterSleep()` / `CH376T_SetSdoInt()`
   仍保留但**已无人调用**，可清理（`storage` 现在只用 `*_NAME` / `*_DATA` 系列）。
2. `ch376` 的 `wait()` / `CH376_GetStatus()` 里 `ESP_LOGI` 较多，写盘时日志量大（正式版可降级为 `ESP_LOGD`；目前靠上层在临界区内屏蔽）。
3. `drop_newest()` 已导出但**当前无调用者**（`service/urine` 的速率改用 `drop_last_interval_us()`）；`URINE_AVG_N` 同属历史遗留。
4. `walltime_now_sec()`、`walltime_is_synced()`、`power_mv()`、`app_monitor_dump()` 等已导出但当前无调用者，属预留/调试接口。
5. `walltime_set_hms()`（经 `service/clock` 的 `clock_sync_hms()`）是留给后续 WiFi 上位机的对时接口，**当前无调用者**，因此 `walltime` 未加锁。
