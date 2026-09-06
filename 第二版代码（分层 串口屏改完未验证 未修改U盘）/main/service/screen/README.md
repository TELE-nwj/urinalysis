# service/screen — 串口屏服务（淘晶驰 TJC / Nextion）

> 在 `driver/screen_uart`（纯字节收发）之上，负责**按淘晶驰协议组帧发送**、
> **接收回传并解码**。由 app 层(monitor)调用其显示函数并注册事件回调。不建任务。

---

## 1. 帧尾后缀（最重要的一条规则）

**无论"单片机 → 屏"的指令，还是"屏 → 单片机"的回传，每一条都必须以 3 个字节的帧尾结束：**

```
0xFF  0xFF  0xFF
```

- 发：`send_cmd()` 会在每条指令末尾自动补上这 3 个字节再交给 UART 发送。
- 收：`on_uart_rx()` 靠检测这 3 个字节来切分一帧。

> 举例：让 `n0` 显示 "123.5"，实际从串口发出的字节是：
> `n0.txt="123.5"`  `FF FF FF`

---

## 2. 单片机 → 屏（发送指令集）

所有发送最终都带帧尾 `FF FF FF`。以下指令均由 `screen.c` 产生，控件名/页面号集中定义在顶部 `CTRL_*` 宏，可按你的 `.HMI` 工程修改。

### 2.1 切换页面
```c
screen_set_page(page);
```
| 字段 | 说明 |
|---|---|
| 指令 | `page <页号>` |
| 示例 | 页面号 0 → `page 0` + `FF FF FF` |
| 页面宏 | `SCREEN_PAGE_MAIN=0`(主界面) / `SCREEN_PAGE_MENU=1` |

### 2.2 文本控件（任意）
```c
screen_set_text(ctrl, "文字");
```
| 字段 | 说明 |
|---|---|
| 指令 | `<控件名>.txt="<文字>"` |
| 示例 | `t0.txt="READY"` + `FF FF FF` |
| 适用 | 文本框 t*、或把数字当文本显示 |

### 2.3 数值控件（整数，任意）
```c
screen_set_num(ctrl, val);
```
| 字段 | 说明 |
|---|---|
| 指令 | `<控件名>.val=<数值>` |
| 示例 | `n2.val=123` + `FF FF FF` |
| 适用 | 数值框 n*（直接显示数值） |

### 2.4 数值控件（带小数，任意）
```c
screen_set_float_text(ctrl, v, dec);
```
| 字段 | 说明 |
|---|---|
| 指令 | `<控件名>.txt="<格式化小数>"`（`%.*f` 生成，`dec` 位小数） |
| 示例 | `n0.txt="123.5"` + `FF FF FF`（dec=1） |
| 说明 | 用 `.txt=` 把小数"当作文本"显示，TJC 数值框小数处理简单 |

### 2.5 发送任意已拼好的指令
```c
screen_send_raw("n0.picc=1");   // 例：切 n0 的图片索引
```
自动补帧尾 `FF FF FF` 发送，用于协议里没单独封装的指令。

### 2.6 便捷封装（monitor 实际用的 5 个数据，汇总表）

| # | 显示内容 | 控件 | 函数 | 内部实际指令 | 示例(含帧尾) |
|---|---|---|---|---|---|
| 1 | 累计尿量 ml | `n0` | `screen_show_urine()` | `txt="%.1f"` | `n0.txt="123.5"` `FF FF FF` |
| 2 | 实时速率 ml/h | `n1` | `screen_show_rate()` | `txt="%.1f"` | `n1.txt="17.0"` `FF FF FF` |
| 3 | 累计滴数 | `n2` | `screen_show_drops()` | `val=<整数>` | `n2.val=123` `FF FF FF` |
| 4 | 计时/运行时间 | `t1` | `screen_show_time()` | `txt="..."` | `t1.txt="T+123s"` `FF FF FF` |
| 5 | 状态/报警 | `t0` | `screen_show_status()` | `txt="..."` | `t0.txt="READY"` `FF FF FF` |

> 说明：
> - 控件名（`n0/n1/n2/t1/t0`）对应顶部 `CTRL_*` 宏，按 `.HMI` 工程改 `screen.c` 顶部即可，不要动 monitor。
> - 尿量/速率用 `txt="小数"`（1 位小数）；滴数用 `val=整数`；计时/状态为纯文字。
> - 这 5 条都在 `monitor.c` 的 `push_ui()` 里每 20ms 调用一次，一次性刷上屏。

---

## 3. 屏 → 单片机（接收/回传）

### 3.1 帧格式
屏（淘晶驰 TJC）回传数据通常同样以 `FF FF FF` 结尾：
```
<回传数据...>  FF FF FF
```
`driver/screen_uart` 的 `uart_rx` 任务把收到的字节回调给 `screen.c` 的 `on_uart_rx()`；
`on_uart_rx()` 按 `FF FF FF` 切帧，把**去掉帧尾后的数据**交给 `handle_rx_frame()` 解码。

### 3.2 解码 → 事件
`handle_rx_frame()` 是解码入口。目前为**占位(仅打日志)**，待你按真实 `.HMI` 回传协议填入：
```c
// 目标：把"收到的 keycode/控件值"翻译成 screen_event_t，
//       然后回调 s_ev_cb() 通知 app 层(monitor)状态机。
// 常见 TJC 回传(举例，按你的 HMI 为准)：
//   按钮按下   → 一串字节 + FF FF FF
//   return 控件 → 若干字节 + FF FF FF
```
目前定义好的语义事件：
```c
typedef enum {
    SCREEN_EV_NONE = 0,
    SCREEN_EV_CLEAR_REQUEST,   /* 屏上"清零"被触发(示例) */
    SCREEN_EV_PAUSE_REQUEST,   /* 屏上"暂停"被触发(示例) */
} screen_event_t;
```

### 3.3 注册方式
app 层(monitor) 在启动时调用 `screen_set_event_cb(on_screen_event)`；
一旦 `handle_rx_frame` 解码出事件并调用 `s_ev_cb(ev)`，就会进到 monitor 的状态机处理。

---

## 4. 一句话速查
- **每条指令/回传都以 `0xFF 0xFF 0xFF` 结尾**。
- 发：`screen_show_*`(固定5个) 或 `screen_set_text/num/float_text/page/send_raw`(通用)。
- 收：`FF FF FF` 切帧 → `handle_rx_frame` 解码成 `screen_event_t` → 回调 `s_ev_cb`(monitor)。
