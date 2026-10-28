# driver/key — 4 路按键

> 轮询 + 消抖 + 短按/长按检测。按下为低电平（内部上拉）。
> 引脚：`BSP_PIN_KEY1..KEY4 = GPIO4/5/8/9`（见 `bsp/pin_map.h`）。
> 上层：**暂未接入业务**。接口保留在驱动层；接入时由调用方(如 app/monitor)周期调 `key_scan()`，事件经注册回调处理。

## 事件类型
```c
typedef enum {
    KEY_NONE = 0,
    KEY1_PRESS, KEY1_LONG,   /* KEY1 短按 / 长按 */
    KEY2_PRESS, KEY2_LONG,
    KEY3_PRESS, KEY3_LONG,
    KEY4_PRESS, KEY4_LONG,
} key_event_t;
```

## 状态机 `key_slot_t`
每键一个槽位，`key_scan()` 需周期性调用（建议 10~20ms，本项目 15ms）：
```c
typedef struct {
    uint8_t raw;        /* 本次采样电平(0释放/1按下) */
    uint8_t stable;     /* 滤波后的稳定电平 */
    uint8_t cnt;        /* 连续相同采样计数 */
    uint8_t long_sent;  /* 本次按下是否已触发过长按 */
    TickType_t down_tick; /* 按下时刻 */
} key_slot_t;
```

## 判定流程
1. **消抖**：电平须连续 `DEBOUNCE_SAMPLES=3` 次一致才切换 `stable`。
2. **稳定按下**：记录 `down_tick`，重置 `long_sent`。
3. **稳定释放**：若未触发过长按 → 发**短按**事件。
4. **长按**：稳定按住 ≥ `LONG_PRESS_MS=1500`ms 且未发过 → 发**长按**（仅一次）。

## 对外接口
| 接口 | 说明 |
|---|---|
| `key_init(cb)` | 配置 4 路按键 GPIO（内部上拉）并注册回调 `key_cb_t` |
| `key_scan()` | 周期调用，扫描 + 消抖 + 长短按判定并触发回调 |

事件码到按键的映射：`KEY1_PRESS + idx*2 + (长按?1:0)`。
