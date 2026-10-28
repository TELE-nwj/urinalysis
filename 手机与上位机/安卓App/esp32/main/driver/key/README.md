# driver/key — 2 路按键

> 轮询 + **软件消抖**，**只出"短按"事件**（无长按、无连发）。按下为低电平（内部上拉）。
> 引脚：`BSP_PIN_KEY1 = GPIO8`、`BSP_PIN_KEY2 = GPIO9`（见 `bsp/pin_map.h`）。
> 唯一上层：`service/input` —— `app/monitor` 每 20 ms 调一次 `input_poll()`，内部就是调 `key_scan()`。

## 事件类型

```c
typedef enum {
    KEY_NONE   = 0,
    KEY1_PRESS = 1,   /* 按键 1 短按 */
    KEY2_PRESS = 2,   /* 按键 2 短按 */
} key_event_t;
```

事件码到按键的映射：`KEY1_PRESS + idx`（`idx` 为 `s_pins[]` 下标）。

## 状态机 `key_slot_t`

每键一个槽位，`key_scan()` 需周期性调用（建议 10~20ms）：

```c
typedef struct {
    uint8_t raw;      /* 本次采样电平(0=释放/1=按下) */
    uint8_t stable;   /* 消抖后的稳定电平 */
    uint8_t cnt;      /* 连续相同采样计数 */
} key_slot_t;
```

## 判定流程

1. **消抖**：电平须连续 `DEBOUNCE_SAMPLES = 3` 次一致才切换稳定态（20 ms 周期 ≈ 60 ms）。
2. **稳定按下**：立即回调一次 `KEYn_PRESS`（响应快，不等松手）。
3. **按住期间**：`raw == stable`，不再触发（无连发）。
4. **松手**：稳定态切回释放，**不产生事件**。

> 与旧版的差别：不再记录 `down_tick`、不做长按判定，因此驱动**不依赖 FreeRTOS 头文件**，
> 只是纯 GPIO 轮询。旧版 4 路按键已收敛为 2 路。

## 对外接口

| 接口 | 说明 |
|---|---|
| `key_init(cb)` | 配置 2 路按键 GPIO（输入 + 内部上拉）并注册回调 `key_cb_t`（可传 `NULL`） |
| `key_scan()` | 周期调用，采样 + 消抖，稳定按下时触发一次回调；返回 `ERR_OK` |

路数由 `key.h` 的 `KEY_COUNT = 2` 决定；改路数只需改这一处和 `s_pins[]` 初始化表。

## 业务语义（由 app 决定，见 `main/app/monitor/monitor.c`）

| 按键 | 待开始 | 测量中 | 暂停 |
|---|---|---|---|
| K1 | 开始测量 | 暂停 | 继续测量 |
| K2 | 复位 | 复位 | 复位 |

- K2 在**任何状态**（含待开始）都执行复位。
- 按键**不直接发任何屏指令**：只改状态机状态，状态栏文本（命令字 `reset` / `start` / `stop`）由 `app/monitor` 的 `ui_sync()` 在下一个 20 ms 循环里按变化补发。
