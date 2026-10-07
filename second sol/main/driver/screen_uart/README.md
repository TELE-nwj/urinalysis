# driver/screen_uart — 串口屏 UART 底层收发

> 两键版只使用发送接口。屏幕命令解析和 `uart_rx` 任务不再启用；以下涉及 RX 的内容为旧版实现参考。

> 只做"发字节 / 收字节"，**不解析任何屏幕协议**。
> 组帧、页面、控件刷新、命令解析都在上层 `service/screen`。
> 引脚见 `bsp/pin_map.h`：**UART1，TX=GPIO17 → 屏 RX，RX=GPIO18 ← 屏 TX，115200 8N1**。

---

## 1. 配置

| 项 | 值 |
|---|---|
| 串口 | `UART_NUM_1`（避开 UART0 日志口） |
| 波特率 | `BSP_UART_SCREEN_BAUD` = 115200 |
| 帧格式 | 8N1，无流控 |
| RX/TX 缓冲 | 1024 / 256 字节 |
| RX 任务 | `uart_rx`，**栈 4096**，优先级 6 |

> ⚠️ **栈必须是 4096**：这个任务的回调链会一路走到
> `service/screen` 解析 → `app/monitor.on_cmd()` → 状态机转移 →
> S3 打开/写入/关闭 U 盘文件（含 `snprintf`、多条日志、SPI 阻塞）。
> 曾用 2048 导致命令处理时静默死掉/重启。

---

## 2. 接收模型

```
uart_read_bytes(..., 50ms超时)
   → len > 0 时回调 s_rx_cb(data, len)      ← 在 uart_rx 任务上下文里执行
   → service/screen 的 on_uart_rx() 逐字节累积、按行解析
```

**注意：回调是在 `uart_rx` 任务里同步执行的**，所以：

- 回调里不要做耗时/阻塞操作（UI 刷新、整块写盘都应放别处）
- 本工程里"命令处理 + U 盘 SPI"就是在回调里同步完成的 —— 因此
  `service/screen` 与 `app/S3` 用一个 **SPI 互斥锁**串行化，
  避免与 `monitor` 任务的"每分钟写盘"并发访问 CH376

---

## 3. 接口（`screen_uart.h`）

| 接口 | 说明 |
|---|---|
| `screen_uart_init()` | 安装 UART 驱动、参数、绑定引脚 |
| `screen_uart_send(data, len)` | 阻塞发送 len 字节 |
| `screen_uart_send_str(str)` | 发送 C 字符串（不含结尾 `\0`） |
| `screen_uart_read(buf, size, timeout)` | **同步读取**（本工程只在调试时用），返回实际字节数 |
| `screen_uart_set_rx_cb(cb)` | 注册接收回调，**注册时内部创建 `uart_rx` 任务**；传 NULL 表示不建任务 |

```c
typedef void (*screen_uart_rx_cb_t)(const uint8_t *data, size_t len);
```

---

## 4. ⚠️ 调试注意：`screen_uart_set_rx_cb()` 会独占 UART

`service/screen` 的 `screen_init()` 里会调 `screen_uart_set_rx_cb(on_uart_rx)`，
**一旦注册，`uart_rx` 任务就把 UART 数据读走了**。

所以要"在 `main` 主循环里直接读串口看原始字节"做调试时，
**必须不调用 `screen_init()`**，只做显示初始化：

```c
screen_set_page(0);
screen_show_status("READY");
screen_show_urine(0.0f);
// screen_init();      /* ← 调试期间注释掉 */
```

（2026-09-14 的收发测试就是这么做的，见 `测试文档/串口屏收发测试记录.md`）
