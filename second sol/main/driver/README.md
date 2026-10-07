# driver 层 — 底层硬件驱动

> 每个外设一个独立子目录，向 service 层提供功能接口，内部实现硬件细节。
> 引脚/外设号统一收敛在 `bsp/pin_map.h`，换板只改 bsp。

| 子模块 | 目录 | 状态 | 被谁使用 | 文档 |
|---|---|---|---|---|
| U盘(USB主机) | `ch376/` | 可用 | `service/storage` | [`ch376/README.md`](ch376/README.md) |
| 液滴检测 | `drop/` | 可用 | `service/urine`、`app/S3` | [`drop/README.md`](drop/README.md) |
| 串口屏 UART | `screen_uart/` | 可用 | `service/screen` | [`screen_uart/README.md`](screen_uart/README.md) |
| 按键 ×2 | `key/` | 已接入三态控制 | `app/monitor` | [`key/README.md`](key/README.md) |
| 电池 | `battery/` | **占位 stub**（目标方案：ADC 分压 @ GPIO7，INA228 已取消） | — | [`battery/README.md`](battery/README.md) |
| 时间服务 | `time_svc/` | 可用 | `main.c`、`app/monitor`、`app/S3` | 见下 |

---

## 1. `time_svc`

极薄的取时封装，只提供"单调递增的毫秒数"，供上层做会话计时（无 RTC，不是绝对时间）。

```c
void     timesvc_init(void);
uint64_t timesvc_now_ms(void);
```

---

## 2. 依赖方向

```
app → service → driver → bsp
```

- driver 只被 `service` / `main` 使用，**不反向依赖**
- driver 之间不允许互相调用（`drop` 不调 `ch376`，等等）

---

## 3. 关键约定

| 约定 | 说明 |
|---|---|
| 中断服务必须极短 | 如 `drop_isr` 只做消抖 + 入队 + 计数，绝不打印/做业务 |
| 共享变量用自旋锁 | `drop` 用 `portMUX_TYPE s_spin` 保护 ISR/任务之间共享的变量 |
| 64 位变量读要保护 | ISR 里在写 `uint64_t` 时，任务侧读要 `portENTER_CRITICAL` |
| 不打印业务日志 | driver 只打硬件相关日志；`ch376` 的日志在写盘临界区内会被上层临时屏蔽 |

---

## 4. 已知遗留

1. `ch376/ch376_spi.c` 里旧的 **`CH376T_FILE_CREATE()` / `CH376T_BYTE_WRITE()` / `CH376T_SET_FILE_NAME()`**
   把文件名 `/ASD.TXT` 和内容 `"hello world"` 写死，**已无人调用**，可清理。
2. `ch376` 的 `wait()` 里 `ESP_LOGI` 较多，写盘时日志量大（正式版可降级为 `ESP_LOGD`）。
3. `battery` 是占位 stub，未实现。
4. `key`（4 路物理按键）接口完整但**业务未接入**。
