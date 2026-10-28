# service 层 — app ↔ driver 桥接服务

> 每个可复用服务一个独立子目录。本层把底层硬件封装成"语义化"的服务供 app 层调用。
> 均为**被动服务**：不建任务、不主动轮询、**不依赖 app 层**。

| 子模块 | 目录 | 职责 | 文档 |
|---|---|---|---|
| 串口屏 | `screen/` | TJC 组帧发送 + 文本命令解析（桥接 `driver/screen_uart`） | [`screen/README.md`](screen/README.md) |
| U盘存储 | `storage/` | 按用户名建/开/写/关文件（桥接 `driver/ch376`） | [`storage/README.md`](storage/README.md) |
| 尿量解算 | `urine/` | 累计/速率/缺水/满袋（纯计算，读 `driver/drop`） | [`urine/README.md`](urine/README.md) |
| 记录缓存 | `record/` | 按分钟缓存的 RAM 数组（只用于调试快照） | 见下 |

依赖方向：`service → driver`；`app → service`；**service 不反向依赖 app**。

---

## 1. 应用层(app)可用的服务接口

### 1.1 `service/screen`

```c
err_t screen_init(void);                     /* 注册接收回调 + 初始界面 */
void  screen_set_cmd_cb(screen_cmd_cb_t cb); /* 文本命令回调: (cmd, username) */

typedef enum { SCREEN_CMD_NONE, SCREEN_CMD_LOGIN, SCREEN_CMD_START,
               SCREEN_CMD_STOP, SCREEN_CMD_RESET } screen_cmd_t;
#define SCREEN_USERNAME_MAX  31u

void screen_show_urine(float ml);        /* data.n0  累计尿量 */
void screen_show_rate(float mlh);        /* data.n1  实时速率 */
void screen_show_drops(uint32_t drops);  /* data.n2  累计滴数 */
void screen_show_time(const char *txt);  /* data.t1  会话计时 */
void screen_show_status(const char *st); /* data.t0  状态文本 */

void screen_set_page(uint8_t page);
void screen_set_text(const char *ctrl, const char *txt);
void screen_set_num(const char *ctrl, int32_t val);
void screen_set_float_text(const char *ctrl, double v, int dec);
void screen_send_raw(const char *cmd);
```

### 1.2 `service/storage`

```c
err_t storage_init(void);                     /* 主机模式 + 检测磁盘 + 挂载 */
err_t storage_create_user(const char *user);  /* 建文件+表头(不存在才建), 不保持打开 */
err_t storage_open_user(const char *user);    /* 打开并定位到末尾(追加), 保持打开 */
err_t storage_write_open(const char *text);   /* 往当前打开的文件续写 */
err_t storage_close_user(void);               /* 关闭 + 清状态 */
```

> 记录文件的表头/列宽宏（`STORAGE_HEADER` / `REC_ROW_FMT` / `REC_COL*_W` / `REC_GAP*`）
> 也定义在 `storage.h`，供 S3 拼数据行时复用。

### 1.3 `service/urine`

```c
typedef struct {
    uint32_t total_drops;  float total_ml;  float rate_mlh;
    uint8_t  flag_dry;     uint8_t flag_bag_full;
} urine_meas_t;

void urine_update(urine_meas_t *m);      /* 每周期解算(需先 drop_poll) */
void urine_meas_reset(urine_meas_t *m);  /* 复位/开始新测量 */
```

---

## 2. 各模块被谁使用

| 模块 | 调用方 |
|---|---|
| `screen` | `app/monitor.c`（注册命令回调 + 推状态文本）、`app/S3/S3.c`（推 5 个量） |
| `storage` | `app/monitor.c`（login 建文件）、`app/S3/S3.c`（每分钟 open/write/close） |
| `urine` | `app/S3/S3.c` |

---

## 3. app 层调用建议（对照当前实现）

| 时机 | 调用 |
|---|---|
| 开机 | `storage_init()` → `screen_init()` → `app_monitor_start()` |
| `login` | `storage_create_user(user)` |
| `start`(S2) | `S3_reset_data(now)` + `S3_file_open(user)` → 进 S3 |
| `start`(S4) | `S3_resume(now)` + `S3_file_open(user)` → 进 S3 |
| 测量中(20ms) | `drop_poll()` + `urine_update()` + 每 200ms `S3_push_ui()` |
| 每满 1 分钟 | 进临界区 → `storage_open_user()` + `storage_write_open()` + `storage_close_user()` |
| `stop` | `S3_pause(now)` + `S3_file_close()` → 进 S4 |
| `reset` | `S3_file_close()` + `S3_clear(now)` + `S3_push_values()` → 进 S1 |
