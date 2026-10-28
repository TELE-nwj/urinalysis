# service/screen — 串口屏服务

> 桥接 `driver/screen_uart`（只收发字节）与 app 层：
> **发送方向**组 TJC 帧；**接收方向**把 `@用户名@命令` 文本解析成 `(cmd, username)` 回调给 app。
> 被动服务：不建任务；接收任务由 `driver/screen_uart` 建。

---

## 1. 屏 → MCU：文本命令协议

```
@用户名@命令 + 行尾
```

| 命令 | 含义 |
|---|---|
| `login` | 登录（仅 S1 有效） |
| `start` | 开始 / 继续测量 |
| `stop` | 暂停 |
| `reset` | 复位（任意状态、任意用户） |

- **用户名**：第 1 个 `@` 与第 2 个 `@` 之间的**全部字符**
  - 可以含**空格**（含首尾空格），**不含 `@`** → 用两个 `@` 定界唯一
  - 原样保留、**不做 trim**
  - 最长 `SCREEN_USERNAME_MAX = 31` 字符
- **命令**：第 2 个 `@` 之后，到行尾为止

---

## 2. 接收解析（`on_uart_rx()` → `handle_cmd_line()`）

### 2.1 行尾判定：`\r` / `\n` / `/n` 三种都认

```c
if (b == '\r' || b == '\n') { ... 切行 ... }          /* 标准 CR/LF */
if (b == 'n' && s_rx_buf[s_rx_len-1] == '/') { ... }  /* 兼容屏端写成字面量 "/n" */
```

> 现场实测：**屏端发的不是 `0D 0A`，而是 `2F 6E`（字符 `/` `n`）**——
> 组态里把 `\r\n` 当成转义写了，TJC 不认这种转义，会原样发出去。
> MCU 端兼容后两种写法都能用；**屏端改对更干净**（见下）。

TJC 屏端正确写法二选一：
```
prints "@abc@login",0     ← 发完字符串
printh 0D 0A              ← 再补回车换行

或直接按字节发： printh 40 61 62 63 40 6C 6F 67 69 6E 0D 0A
```

### 2.2 丢弃行首杂字节（**踩过坑**）

屏在**上电/复位时会回传一帧 `1A FF FF FF`**。它既不是行尾也不是 `/n`，
会一直赖在行缓冲里，把随后第一帧命令顶成"非 `@` 开头" → **整帧被丢掉**，
表现为**上电/复位后第一次登录没反应**。

两道防护：

```c
/* ① 缓冲为空时，只接受 '@'，其它字节直接丢 */
if (s_rx_len == 0 && b != '@') continue;

/* ② 解析时不再要求首字符是 '@'，从行内第一个 '@' 开始 */
const char *p = strchr(line, '@');
if (!p) return;
if (p != line) ESP_LOGW(TAG, "行首有 %d 个杂字节，已跳过: '%s'", (int)(p - line), line);
line = p;
```

### 2.3 其他保护

| 情况 | 处理 |
|---|---|
| 用户名为空 / 超过 31 字符 | 丢弃 + `ESP_LOGW` |
| 命令关键字不认识 | 丢弃（不回调） |
| 行长超过缓冲（96 字节） | 丢弃整行 |

### 2.4 ⚠️ 调试日志（**正式版可关**）

`on_uart_rx()` 开头有两行调试输出，每收到一帧都会刷：

```c
ESP_LOGI(TAG, "RX %u 字节: %s", (unsigned)len, t);                 /* 文本形式 */
ESP_LOG_BUFFER_HEXDUMP(TAG, data, (uint16_t)len, ESP_LOG_INFO);   /* 原始 hex */
```

作用：区分"MCU 根本没收到字节"和"收到了但被解析器丢了"。
**上线前建议注释掉**。

---

## 3. MCU → 屏：显示

### 3.1 TJC 帧格式

```
控件.txt="值"        或   控件.val=数值
+ 0xFF 0xFF 0xFF      ← 帧尾（发送函数自动补）
```

### 3.2 5 个业务量（控件名宏在 `screen.c` 顶部）

| 显示函数 | 控件宏 | 控件名 | 内容 |
|---|---|---|---|
| `screen_show_urine(float ml)` | `CTRL_URINE` | `data.n0` | 累计尿量 ml（1 位小数） |
| `screen_show_rate(float mlh)` | `CTRL_RATE` | `data.n1` | 实时速率 ml/h（1 位小数，7s 无新滴归零） |
| `screen_show_drops(uint32_t)` | `CTRL_DROPS` | `data.n2` | 累计滴数（整数，也走 `.txt`） |
| `screen_show_time(const char*)` | `CTRL_TIME` | `data.t1` | 会话计时 `hh:mm:ss` |
| `screen_show_status(const char*)` | `CTRL_STATUS` | `data.t0` | 状态/报警文本 |

> 用的是**三级写法** `data.n0.txt="…"`（HMI 工程里控件在 `data` 页/组下）。
> 如果你的工程是单页 `n0`，把这几个宏改成 `"n0"` 等即可。

### 3.3 状态文本一览

| 文本 | 出现时机 |
|---|---|
| `LOGIN` | S1 复位待登录 |
| `READY` | S2 已登录未开始；S3 正常测量中 |
| `PAUSE` | S4 暂停 |
| `DRY` | 方式A 判定缺水/断流（优先级低于 BAG FULL） |
| `BAG FULL` | 累计尿量 ≥ 2000ml |

### 3.4 推屏节流由 app 控制

`screen.c` 本身**不等间隔推送**；节流在 `app/S3/S3.c`：

- `S3_run()` 每 20ms 调一次，但**每 200ms 才推一次**（`S3_UI_PERIOD_MS`）
- 状态文本**变化了才推**（避免反复刷同一个值）
- 写 U 盘临界区内 `s_ui_pause = true` → `S3_push_ui()` 直接 return，不发串口

---

## 4. 接口（`screen.h`）

```c
/* 初始化 */
err_t screen_init(void);                              /* 注册接收回调 + 初始界面 */

/* 命令接收 */
typedef enum { SCREEN_CMD_NONE, SCREEN_CMD_LOGIN, SCREEN_CMD_START,
               SCREEN_CMD_STOP, SCREEN_CMD_RESET } screen_cmd_t;
typedef void (*screen_cmd_cb_t)(screen_cmd_t cmd, const char *username);
void screen_set_cmd_cb(screen_cmd_cb_t cb);
void screen_parse_line(const char *line);              /* 测试用：喂一整行给解析器 */
#define SCREEN_USERNAME_MAX  31u

/* 显示 */
void screen_show_urine(float ml);
void screen_show_rate(float mlh);
void screen_show_drops(uint32_t drops);
void screen_show_time(const char *text);
void screen_show_status(const char *status);

/* 通用 */
void screen_set_page(uint8_t page);
void screen_set_text(const char *ctrl, const char *txt);
void screen_set_num(const char *ctrl, int32_t val);
void screen_set_float_text(const char *ctrl, double v, int dec);
void screen_send_raw(const char *cmd);                 /* 发已拼好的指令(自动补 FF FF FF) */

/* 旧的事件回调（保留兼容，未使用） */
typedef enum { SCREEN_EV_NONE, SCREEN_EV_START, SCREEN_EV_PAUSE,
               SCREEN_EV_RESET, SCREEN_EV_SAVE } screen_event_t;
void screen_set_event_cb(screen_event_cb_t cb);
```

---

## 5. 常见问题

| 现象 | 原因 |
|---|---|
| 上电后第一次登录没反应 | 行首杂字节把首帧顶掉 → 已修（§2.2） |
| 命令一直不生效 | 屏端发的行尾既不是 `\r\n` 也不是 `/n`；看 `RX n 字节` 的 hex |
| 屏上数字不跳 | 控件名对不上（`data.nX` vs `nX`）；或不在 S3 状态（只有 S3 推数据） |
| 日志刷满 | §2.4 的调试 dump，注释掉即可 |
