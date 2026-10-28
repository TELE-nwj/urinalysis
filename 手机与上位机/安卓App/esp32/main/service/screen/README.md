# service/screen：串口屏显示（MCU → 屏，单向）

本模块是本工程**唯一给屏组帧**的地方。屏端只显示，MCU 不接收、不解析屏发来的任何数据（`driver/screen_uart` 分配了 256 字节 RX 缓冲但**固件从不读取**，也没有接收任务）。

## 1. 只有一种指令格式

全工程**只有一种**发往屏的指令：

```text
<控件名>.txt="内容"  +  FF FF FF
```

- 例：`data.t0.txt="12.5"` + `FF FF FF`、`data.t4.txt="start"` + `FF FF FF`
- 页切换 `page 0` 也属于这一类，同样以 `FF FF FF` 结尾（页号常量 `SCR_PAGE_MAIN = 0` 单独放在 `screen.c` 里，换页号只改那一行）
- 结尾字节固定为 3 个 `0xFF`（`screen.c` 的 `s_txt_end[3]`），**所有**帧共用这一个出口（`send_data_frame()`）
- **没有第二条通道**，也**没有**任何以 `\r\n` 结尾的指令

> ⚠ 早期设计过 `@start` / `@stop` / `@reset` + `\r\n` 的事件指令，**已按用户要求取消**（`screen_notify_*()`、内部辅助 `send_line_cmd()`、常量 `s_line_end` 都已从固件删除）。状态栏现在也用文本控件承载（见 §2 的 `SCR_CTRL_STATUS`）。若将来要再加协议，务必先与 HMI 工程约定，不要在这一条文本通道之外另起一套。

## 2. 控件映射（8 个）

8 个控件名全部定义在 `screen.h` 的 `SCR_CTRL_*` 宏里，`screen.c` 中不出现任何控件名字面量：

| 宏 | 控件名 | 内容 |
|---|---|---|
| `SCR_CTRL_URINE` | `data.t0` | 累计尿量 mL，1 位小数 |
| `SCR_CTRL_RATE` | `data.t1` | 实时速率 mL/h，1 位小数 |
| `SCR_CTRL_DROPS` | `data.t2` | 累计滴数（整数） |
| `SCR_CTRL_ELAPSED` | `data.t3` | 运行时间（会话计时）`hh:mm:ss` |
| `SCR_CTRL_STATUS` | `data.t4` | 状态栏：**命令字** `reset` / `start` / `stop` |
| `SCR_CTRL_ALERT` | ⚠ `data.t5` | 报警栏：**单字母** `A`~`E`（A=正常 / B=无尿 / C=少尿 / D=多尿 / E=尿袋满） |
| `SCR_CTRL_CLOCK` | `data.t6` | 实时时间 `hh:mm:ss` |
| `SCR_CTRL_BATTERY` | `data.t7` | 电量（如 `85%`；未就绪 `--`） |

> - **状态栏发的是命令字，不是中文**：`reset` = 待开始、`start` = 测量中、`stop` = 暂停（由 `app/monitor` 的 `status_text()` 产生）。屏端既可直接显示，也可拿它做条件判断。
> - **报警栏发的是单字母**：由 `app/monitor` 的 `alert_letter()` 产生、`alert_tick()` 在 1 秒节拍里发送（**每秒都发**，不像状态栏那样只在变化时发）；`E` 只在累计尿量首次达到 2 L 时插播 1 分钟。
> - **编号一律以 `screen.h` 的 `SCR_CTRL_*` 宏为准**：旧版的 `data.n0/n1/n2` 与 `data.t0~t4` 那一套编号已全部作废，不要在 HMI 工程里沿用。改控件名只改这几个宏，其余代码不用动。
> - ⚠ **报警栏 `data.t5` 是占位 id：该栏 HMI 工程尚未制作**，做好后按实际控件名替换宏值即可；其余 7 个控件已由 HMI 工程定稿。

## 3. 行为要点

- `screen_init()`：起 UART（`screen_uart_init()`）→ 发 `page 0` → 把 8 个控件刷成确定初值（`0.0` / `0` / `00:00:00` / `reset` / `A` / `--`）。UART 起不来直接返回 `ERR_FAIL`，`main.c` 会停止启动。
- `screen_show_status()` 的取值只有 `reset` / `start` / `stop` 三种（命令字），由 `app/monitor` 在状态变化时推送；按键本身不发任何屏指令，只改状态机，文本由 `ui_sync()` 在下一个 20 ms 循环里补发。
- `screen_show_battery(percent)`：`percent < 0` 显示 `--`；`> 100` 钳到 100；否则 `%d%%`。
- 帧缓冲上限 `SCR_FRAME_MAX = 64`；超长宁可不发也不发残缺指令（只打 `ESP_LOGE`）。
- **每一帧都会用 `ESP_LOGI` 打印一行** `I (…) screen: [SCR] <可见内容>`，便于逐行核对屏上收到什么（结尾字节不在日志里）。
- 本模块不建立任务、不打业务日志；状态栏命令字与报警栏字母由 `app/monitor` 决定，数值由 `app/S1` 推送。

完整字段映射、初始化序列和取值表见工程根目录的《单片机与串口屏通信格式.md》。
