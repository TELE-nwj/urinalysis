# service 层

本层在 app 与硬件驱动之间提供六项能力，也是 **app 层唯一可见的接口面**：app 不 include 任何 `driver/*` 头文件。

| 模块 | 职责 | 初始化入口 |
|---|---|---|
| `urine/` | 尿量计量与方式 A 分析（纯计算）+ 代理液滴采集控制；对 app 只暴露 `urine_get()` 只读结果 | `urine_capture_init()` |
| `screen/` | 串口屏（TJC）单向显示：文本控件刷新（含状态栏命令字 `reset`/`start`/`stop`）；本工程唯一给屏组帧的地方 | `screen_init()`（内部起 UART） |
| `storage/` | CH376 U 盘会话文件：分配/恢复文件、每分钟追加一行 | `storage_init()`（软 SPI + 主机模式 + 检测磁盘） |
| `power/` | 电池电量业务：读电压 → 一阶滤波 → 12 点分段线性查表得百分比；**百分比单调不增**（只降不升，换电池/充电后需重新上电恢复） | `power_init()`（内部初始化 ADC） |
| `clock/` | 时间服务：`clock_ms()` 单调毫秒 + `clock_now_hms()` 墙上时间；对时预留 | `clock_init()`（内部起 time_svc 与 walltime） |
| `input/` | 按键服务：把 driver 的按键事件翻译成 `INPUT_KEY1/2` 短按回调 | `input_init(cb)` |

要点：

- **服务层不建立任务**。各周期函数由 app 的 20 ms 任务调用：`input_poll()`、`urine_poll()` 每轮；`S1_run()` 每轮（仅测量中）；`power_poll()` 与 `clock_now_hms()` 由 1 秒节拍调用。
- 硬件初始化收在各自的 `*_init()` 里，`main.c` 只调 service 的 init，因此入口层和应用层都不会散落驱动细节。
- 依赖方向：`service → driver → bsp`。service 之间不互相调用（`screen` 不调 `storage`，等等），跨模块协作全部由 `app/monitor` 编排。
- 电量检测**本版已启用**（原 INA228 电量计方案取消，改为 ADC 分压）。
- `clock_sync_hms()`（对时）目前是预留接口，没有调用者；上位机接入后由它转发到 `driver/walltime`。
