# app 层 — 状态机

> 依赖方向：app 依赖 service / driver，自身不碰寄存器；**service 不依赖 app**。

| 目录 | 文件 | 职责 |
|---|---|---|
| `monitor/` | `monitor.c/h` | **命令 → 状态**的唯一判断处；周期调度；调试快照 |
| `S1/` | `S1.c/h` | 复位待登录：屏 `LOGIN` |
| `S2/` | `S2.c/h` | 已登录未开始：屏 `READY` |
| `S3/` | `S3.c/h` | 测量中：解算 + 推屏 + 每分钟写 U 盘 |
| `S4/` | `S4.c/h` | 暂停：屏 `PAUSE` |

---

## 1. 分工原则

```
monitor.c   = 状态变量 + 【一个 switch(cmd) 决定去哪个状态】 + 周期调度
S1/S2/S4.c  = 只有 Sx_enter()  —— 进入本状态时把屏刷成对应的状态文本
S3.c        = S3_enter/run + 测量接口（清数据/继续/暂停/推屏/写盘）
```

**所有"命令 → 状态转移"的判断只在 `monitor.c` 的 `on_cmd()` 一处**，
S1~S4 里没有命令处理函数（没有 `Sx_on_cmd`）。

---

## 2. `monitor.c`

### 状态变量

```c
static app_state_t s_state = APP_S1;                       /* 当前状态 */
static char s_user[SCREEN_USERNAME_MAX + 1] = "";          /* 当前登录用户名 */
```

### `on_cmd()` —— 唯一的转移函数

由 `service/screen` 解析出 `(cmd, username)` 后回调进来。

```c
static void on_cmd(screen_cmd_t cmd, const char *user)
{
    const char *u   = (user && *user) ? user : "";
    uint64_t    now = timesvc_now_ms();

    ESP_LOGI(TAG, ">>>>>>>>>> 收到屏幕命令: %s  用户='%s' <<<<<<<<<<", cmd_name(cmd), u);
    app_monitor_dump("收到命令时");

    /* login / reset 不校验用户名：
       · login → 允许换用户
       · reset → 不管当前登录的是谁，收到就复位 */
    if (cmd != SCREEN_CMD_LOGIN && cmd != SCREEN_CMD_RESET &&
        strcmp(u, s_user) != 0) {
        ESP_LOGW(TAG, "用户名不匹配(当前='%s' 收到='%s'), 忽略 %s", s_user, u, cmd_name(cmd));
        return;
    }

    switch (cmd) {
    case SCREEN_CMD_LOGIN:                              /* 仅 S1 有效 */
        if (s_state != APP_S1) { warn; break; }
        if (!*u) { warn; break; }
        if (storage_create_user(u) != ERR_OK) { warn; }  /* 只建文件+表头 */
        snprintf(s_user, sizeof(s_user), "%s", u);
        goto_state(APP_S2);
        break;

    case SCREEN_CMD_START:
        if (s_state == APP_S2) {                 /* 全新测量 */
            S3_reset_data(now);                  /* 清数据/计时归零/记第0分钟 */
            S3_file_open(s_user);                /* 记住用户名 + 介质检查 */
            goto_state(APP_S3);
        } else if (s_state == APP_S4) {          /* 续测 */
            S3_resume(now);
            S3_file_open(s_user);
            goto_state(APP_S3);
        } else { warn; }
        break;

    case SCREEN_CMD_STOP:
        if (s_state == APP_S3) {
            S3_pause(now);                       /* 结算计时 + 数值冻结到屏 */
            S3_file_close();                     /* 不再写盘 */
            goto_state(APP_S4);
        } else { warn; }
        break;

    case SCREEN_CMD_RESET:                       /* 任意状态都能复位 */
        S3_file_close();
        S3_clear(now);
        S3_push_values();                        /* 屏上 4 个数值归零 */
        goto_state(APP_S1);                      /* 内部会清空用户名 */
        break;
    }
}
```

### `goto_state()` —— 唯一的"进状态"入口

```c
static void goto_state(app_state_t s)
{
    if (s == s_state) return;
    ESP_LOGI(TAG, "状态切换 %s -> %s", state_name(s_state), state_name(s));
    s_state = s;
    if (s_state == APP_S1) s_user[0] = '\0';     /* 回待登录: 清当前用户 */
    switch (s_state) {
    case APP_S1: S1_enter(); break;
    case APP_S2: S2_enter(); break;
    case APP_S3: S3_enter(); break;
    case APP_S4: S4_enter(); break;
    }
}
```

### `monitor_task()` —— 周期调度（20ms）

```c
for (;;) {
    switch (s_state) {
    case APP_S3: S3_run(); break;   /* 只有测量中需要周期动作 */
    default:     break;             /* S1/S2/S4 无周期动作 */
    }
    vTaskDelay(pdMS_TO_TICKS(LOOP_MS));   /* LOOP_MS = 20 */
}
```

### 对外接口（`monitor.h`）

| 接口 | 说明 |
|---|---|
| `app_monitor_start()` | 注册命令回调 + 进 S1 + 建 `monitor` 任务 |
| `app_monitor_reset()` | 复位：关文件 + 清数据 + 屏归零 → S1 |
| `app_monitor_get()` | 转发 `S3_meas()`，读当前测量结果 |
| `app_monitor_dump(note)` | 【调试】把当前信息快照打到串口日志 |

---

## 3. S1 / S2 / S4 —— 各只有一个函数

```c
/* S1.c：复位待登录 */
void S1_enter(void) { ESP_LOGI(TAG, "进入 S1 复位待登录"); screen_show_status("LOGIN"); }

/* S2.c：已登录未开始 */
void S2_enter(void) { ESP_LOGI(TAG, "进入 S2 已登录未开始"); screen_show_status("READY"); }

/* S4.c：测量暂停 */
void S4_enter(void) { ESP_LOGI(TAG, "进入 S4 测量暂停"); screen_show_status("PAUSE"); }
```

> 暂停时的"数值冻结"由 `S3_pause()` 完成（推 4 个数值到屏），S4 只负责把状态文本改成 `PAUSE`，避免闪烁。

---

## 4. S3 —— 测量中（本层最重的状态）

### 对外接口（`S3.h`）

| 接口 | 说明 |
|---|---|
| `S3_enter()` | 进测量中：强制整体刷一次屏（清状态缓存 + 推 5 个量） |
| `S3_run()` | 每 20ms：`drop_poll()` → `S3_update()` → 推屏（200ms 节流） |
| `S3_reset_data(now)` | 开始全新测量：清数据、计时归零 |
| `S3_clear(now)` | 复位：清数据/计时 |
| `S3_resume(now)` | 暂停后继续：只重置计时基准 |
| `S3_update(now)` | 推进计时 + `urine_update()` + 满整分钟时写盘 |
| `S3_pause(now)` | 暂停：结算计时 + 把 4 个数值冻结到屏 |
| `S3_file_open(user)` | 记住本次测量的用户名 + 介质检查 + 确保文件存在 |
| `S3_file_close()` | 清掉用户名（此后不再写盘） |
| `S3_push_values()` | 推 4 个数值/时间控件（不带状态） |
| `S3_push_ui()` | 推 5 个量（调试用，临界区内自动跳过） |
| `S3_meas()` | 读测量结果 |

### 每分钟写盘的临界区

```c
static void S3_record_write(uint32_t minute)
{
    /* 1) 组数据行: 时间 / 本分钟尿量 / 本分钟平均流速（列宽宏在 storage.h） */
    delta = s_meas.total_ml - s_last_ml;     /* 本分钟尿量增量 */
    rate  = delta * 60.0f;                   /* 本分钟平均流速 ml/h */
    snprintf(c1/c2/c3 ...);
    snprintf(line, ..., REC_ROW_FMT, c1, c2, c3);

    ESP_LOGI(TAG, ">>>>> 进入临界区(停液滴中断/停推屏): 第%lu分钟 %s", minute, line);

    S3_crit_enter();          /* 见下 */
    /* ============ 临界区内: 只有 SPI 通讯，无任何日志 ============ */
    e = storage_open_user(s_user);    /* 打开 + 定位到文件末尾(追加) */
    if (e == ERR_OK) {
        e = storage_write_open(line); /* 写这一行 */
        storage_close_user();         /* ★ 立刻关闭: 目录项落盘 */
    }
    /* =========================================================== */
    S3_crit_exit();

    ESP_LOGI(TAG, "<<<<< 退出临界区: 第%lu分钟已写入U盘");
}
```

**临界区进出做的事**：

```c
S3_crit_enter(): 拿 SPI 互斥锁 → esp_log_level_set("CH376T", NONE) → drop_irq_disable() → s_ui_pause = true
S3_crit_exit() : s_ui_pause = false → drop_irq_enable() → 恢复日志级别 → 放 SPI 锁
```

- 屏蔽 `CH376T` 这个 TAG，保证**驱动内部日志也不会在区内冒出来**
- `S3_push_ui()` 开头 `if (s_ui_pause) return;`，区内绝不发串口
- 互斥锁串行化两个任务（`monitor_task` 写记录 / `uart_rx` 收命令开关文件）

### 计时与"整分钟"

- `s_elapsed_ms`：会话计时，测量中累加、暂停冻结、复位归零
- `s_acc_tick`：上次记账时刻（暂停/继续时重设基准）
- `s_last_min`：已写过的最大分钟号（第 0 分钟不写盘）
- `s_last_ml`：上一分钟标记时的累计尿量（用于算本分钟增量）

---

## 5. 一次完整流程的日志

```
@abc@login   → S1→S2   storage: 用户名 'abc' → 文件名 '/ABC.TXT'
                        storage: 已创建用户文件并写入表头: /ABC.TXT
                        storage: 落盘验证通过: /ABC.TXT 确实在U盘上, 长度 48 字节
@abc@start   → S2→S3   S3: 本次测量将写入 '/ABC.TXT'(每分钟写完即关)
   ...每分钟...
              S3: >>>>> 进入临界区(停液滴中断/停推屏): 第1分钟 00:01:00 1.6ml 96.0ml/h
              S3: <<<<< 退出临界区: 第1分钟已写入U盘
@abc@stop    → S3→S4   S3: 本次测量结束，不再写入: '/ABC.TXT'
@abc@reset   → S4→S1   屏上 4 个数值归零，状态文本 LOGIN
```
