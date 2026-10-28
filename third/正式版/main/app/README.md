# app：两键三态状态机

| 目录 | 内容 |
| --- | --- |
| `monitor/` | 三态状态机主体（待开始 / 测量中 / 暂停）：20 ms 主循环、按键处理、1 秒节拍（时钟 + 电量 + 报警栏）、状态栏推送 |
| `S1/` | 测量中处理：会话计时、首个 24 小时结论、200 ms 推屏、每分钟往 U 盘写一行 |
| `S2/` | 暂停处理：结算计时到暂停瞬间、把数值冻结在屏上 |

状态枚举在 `monitor/monitor.h`：`APP_IDLE` / `APP_MEASURING` / `APP_PAUSED`。原来的 `S3`、`S4` 已删除；报警栏、状态栏与 1 秒节拍搬进 `monitor`，测量期处理留在 `S1`。状态栏用文本控件发**命令字**（`reset` / `start` / `stop`，见下文）。

## monitor：一个 20 ms 任务做五件事

`monitor_task()` 每轮（`LOOP_MS = 20u`）依次：

1. `input_poll()` —— 按键扫描，确认短按后回调 `on_key()`；
2. `urine_poll()` —— 液滴搬运 + 解算（**与状态无关**，待开始/暂停也在跑，保证 `urine_get()` 里的会话累计随时可用）；
3. 仅当 `APP_MEASURING` 时 `S1_run()`；
4. 到点触发一次 `tick_1s()`：**判定点取在可能阻塞的 `S1_run()` 之后**，用 `clock_ms()` 与 `s_next_tick_ms` 比较（`TICK_MS = 1000u`，用**加法**推进下一次时刻，环内偶发阻塞只推迟当次、不累积漂移；被阻塞超过一整拍则跳到下一个未来时刻，不补发），执行 `power_poll()` → `clock_now_hms()` → `screen_show_clock()` + `screen_show_battery(power_percent())` → `alert_tick()`（**上电即跑，与是否测量无关**；`s_next_tick_ms` 初值 0，上电第一轮就先发一次）；
5. `ui_sync()` —— 只推状态栏（`status_text()` 产生的命令字），内容变化才发；报警栏不在这里，走每秒的 `alert_tick()`。

按键语义（`on_key()`），全部走 `service/input`，本层不碰 GPIO 与消抖：

| 状态 | K1（`INPUT_KEY1`） | K2（`INPUT_KEY2`） |
| --- | --- | --- |
| `APP_IDLE` | `S1_storage_start()` → `S1_reset()` → `urine_capture_arm()` → `S1_enter()` → 测量中（状态栏随后变 `start`） | `do_reset()` → 待开始（状态栏刷成 `reset`） |
| `APP_MEASURING` | `urine_poll()` → `S2_enter()` → `urine_capture_hold()` → `urine_capture_clear()` → 暂停（状态栏随后变 `stop`） | 同上 |
| `APP_PAUSED` | `S1_enter()` → `urine_capture_arm()` → 测量中（状态栏随后变 `start`） | 同上 |

> 表里的"随后"指 `ui_sync()` 在下一个 20 ms 循环按内容变化补发——按键本身**不发任何屏指令**。

`do_reset()` 有意做成"任何状态都执行"（含待开始）：屏端 HMI 有自己的页面状态，K2 时即使 MCU 已在待开始也要把状态栏刷成 `reset`，让屏与 MCU 停在同一个界面。`app_monitor_reset()` 与 K2 完全同义，供外部复用。

文本控件的写者划分（避免同一控件两个写者互相覆盖）：

- 状态栏 `data.t4`（命令字 `reset` / `start` / `stop`）—— `monitor` 唯一写者，**内容变化才发**；
- 报警栏 `data.t5`（单字母 `A`~`E`；HMI 尚未制作，id 为**占位**）—— 也由 `monitor` 写，但**每秒发一次**（`alert_tick()`），不按变化发；
- 4 个数值/时间控件 `data.t0`（尿量）、`data.t1`（速率）、`data.t2`（滴数）、`data.t3`（会话计时）—— `S1`/`S2` 通过 `S1_push_values()` 写。

状态栏文本：`status_text()` 按状态返回 `reset`（待开始）/ `start`（测量中）/ `stop`（暂停）。报警栏用**单字母**，由 `alert_letter()` 给出当前该显示的字母、`alert_tick()` 每秒维护并发送：

| 字母 | 含义 | 时机 |
|---|---|---|
| `A` | 正常 | 一直发；待开始（含复位后）固定 `A` |
| `B` / `C` / `D` | 无尿 / 少尿 / 多尿 | `S1_day_alert()` 的 24 小时结论，锁定后持续有效 |
| `E` | 尿袋满 | 只在累计尿量**首次达到 2000 ml** 时插播 1 分钟（`ALERT_BAG_FULL_MS = 60000`），随后回到本该显示的字母 |

`E` 用 `s_bag_prev` 抓 `flag_bag_full` 的 **0→1 上升沿**触发（该标志只增不落，不做边沿检测会一直刷 `E`）；`do_reset()` 会清掉 `s_bag_until_ms`（取消进行中的窗口）与 `s_bag_prev`（重新起算）。满袋触发时打一条 INFO 日志（串口日志，不是屏指令）。U 盘异常不上屏。

## S1：测量中

- 会话计时用 `clock_ms()` 累加：`S1_enter()` 把记账基准挪到此刻（开始与继续都适用），`S1_settle()` 结算到暂停瞬间（暂停期间不计时），`S1_reset()` 归零。
- 24 小时结论：`s_elapsed_ms ≥ 86400000` 时首次判定并锁定 —— `<100 ml` 无尿（`B`）、`<400 ml` 少尿（`C`）、`>2500 ml` 多尿（`D`），其余正常（`A`）；字母由 `S1_day_alert()` 给出。
- 每分钟（`s_elapsed_ms / 60000`）写一行到 U 盘：`时间(hh:mm:ss) + 本分钟尿量(ml) + 本分钟平均流速(ml/h)`，平均流速 = 本分钟尿量 × 60。
- 写盘临界区（`s1_crit_enter()` / `s1_crit_exit()`）：拿 CH376 的**递归** SPI 互斥锁，`take`/`give`每层配对；仅最外层保存/恢复原采集状态、推送与日志等级。事务期间**临时关闭液滴中断**、暂停串口屏及手机状态推送；Wi-Fi连接、握手与命令ACK保持。`storage`保留INFO阶段日志，`CH376T`保留WARN/ERROR，退出后恢复原日志等级。完成后明确打印“第N分钟已写入U盘”，失败指出打开/写入/关闭阶段。临时恢复不清滴间隔/缓冲，不误开启待开始或暂停状态的采集。这里使用互斥量，不关闭RTOS调度；中断关闭期间落滴可能丢失。
- 掉盘容错：写盘失败或盘未就绪时置 `s_storage_ok=false`，每 `STORAGE_RETRY_MS = 5000 ms` 重试一次（有会话名就 `storage_ensure_session()` 恢复，没有就 `storage_create_session()` 新分配），期间照常计量；重试失败同样在区外补报 `恢复会话/新建文件失败(err=<码> <文本>)`。
- 推屏节流 `S1_UI_PERIOD_MS = 200u`（20 ms 周期下约 5 Hz）。`S1_push_values()` 只推 4 个数值/时间控件，暂停冻结、复位归零、进入测量时对齐都复用它。
- 对外接口（`S1.h`）：`S1_enter()`、`S1_run()`、`S1_reset()`、`S1_settle()`、`S1_push_values()`、`S1_storage_start()`、`S1_storage_stop()`、`S1_session_file()`、`S1_day_alert()`。

## S2：暂停

`S2_enter()` 只做两件事：`S1_settle()`（计时结算 + 实时速率清零）与 `S1_push_values()`（4 个数值冻结在屏上，尤其是实时速率，否则屏上会一直挂着暂停前的最后一个值）。之后由 `monitor` 关掉滴落中断，暂停期间落下的滴不再计入。

## 依赖关系

按键走 `service/input`，屏指令走 `service/screen`，尿量结果读 `service/urine`（`urine_get()` 返回只读指针），时间走 `service/clock`，电量走 `service/power`，存储走 `service/storage`。本层不 include 任何 `driver/*` 头文件。
