# app 层 — 主状态机 / 监控中枢

> 本层目前只含 `monitor.*`，是整机**唯一主动循环(状态机)**。
> 依赖方向：app 依赖 service / driver，自身不碰寄存器；**service 不依赖 app**。

## `monitor.c/h`

### 定位
- 唯一主动循环（任务 `monitor`，栈 4096 / 优先级 5）。
- 状态由**串口屏回传事件**驱动（经 `service/screen` 解码后回调进来）。
- 物理按键暂不接入业务（`driver/key` 接口保留在驱动层）。

### 状态机（串口屏事件回调 `on_screen_event`）
| 事件 | 动作 |
|---|---|
| `SCREEN_EV_CLEAR_REQUEST` | 清零累计尿量并刷新显示 |
| `SCREEN_EV_PAUSE_REQUEST` | 状态显示 "PAUSE"（示例） |

> 事件由 `service/screen` 的 `handle_rx_frame` 按真实 HMI 协议映射后 `emit()`；映射未定前不会触发。

### 主循环 `monitor_task`（20ms）
1. `drop_get_stats()` 读液滴，有新增则 `urine_add_drops()` 累计并 `screen_show_urine()` 推送（调 service）；
2. 每 `STORE_MS=10`s 拼一行文本 → `storage_write_text()` 落盘（调 service）。

### 对外接口
| 接口 | 说明 |
|---|---|
| `app_monitor_start()` | 初始化 urine、注册 screen 事件回调、创建 monitor 任务 |
| `app_monitor_reset()` | 清零累计尿量并刷新显示 |

## 为什么不把业务塞进别层
- monitor 是唯一会“自己醒来做事”的地方（读滴、写盘、响应屏事件），其余层均为被动服务/驱动，职责清晰。
