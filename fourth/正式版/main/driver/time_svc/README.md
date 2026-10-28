# driver/time_svc — 单调毫秒时基

> 极薄的取时封装，只回答"从开机到现在过了多少毫秒"。
> **无 RTC、不涉及日历/墙上时间**：需要 `hh:mm:ss` 请用 `driver/walltime`（它以本模块为基准）。

## 接口（`time_svc.h`）

```c
void     timesvc_init(void);
uint64_t timesvc_now_ms(void);     /* 开机以来毫秒，单调不减 */
```

| 接口 | 说明 |
|---|---|
| `timesvc_init()` | 初始化时基（底层是 `esp_timer`），由 `service/clock` 的 `clock_init()` 调用，顺序在 `walltime_init()` **之前** |
| `timesvc_now_ms()` | 单调递增毫秒；不受对时影响 |

## 被谁使用

| 使用方 | 用途 |
|---|---|
| `service/clock` → `clock_ms()` | app 层的会话计时（开始/继续/暂停的加减由 `app` 决定） |
| `service/clock` → `clock_now_hms()` | 转发给 `walltime`，由其推算墙上时间 |
| `driver/walltime` | 作为走时基准（driver 之间唯一的一处依赖） |
| `service/urine` | 速率超时判定用的是 `esp_timer_get_time()`（微秒），语义与这里一致 |

「单调不减」是会话计时的前提：`app/S1` 用相邻两次取样做差累加，暂停期间不计入。
