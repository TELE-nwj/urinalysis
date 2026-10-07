# driver/screen_uart — 串口屏 UART（单向：只发不收）

> 只做"发字节"，**不解析任何屏幕协议**。组帧与控件刷新都在上层 `service/screen`。
> 引脚见 `bsp/pin_map.h`：**UART1，TX=GPIO17 → 屏 RX，RX=GPIO18 ← 屏 TX，115200 8N1**。

## 1. 配置

| 项 | 值 |
|---|---|
| 串口 | `UART_NUM_1`（`BSP_UART_SCREEN_NUM`，避开 UART0 日志口） |
| 波特率 | `BSP_UART_SCREEN_BAUD` = 115200 |
| 帧格式 | 8N1，无流控（`UART_SCLK_DEFAULT`） |
| RX 缓冲 | **256 字节**（`RX_BUF_SIZE`）—— 必须 > UART FIFO(128)，详见 §2 的警告 |
| TX 缓冲 | **256 字节**（`TX_BUF_SIZE`；屏指令都很短，够用） |
| RX 任务 / 接收回调 / `uart_read_bytes()` | **无**（固件从不读 RX 缓冲） |

安装调用：`uart_driver_install(BSP_UART_SCREEN_NUM, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0)`。

## 2. "只发不收"是怎么实现的 —— ⚠️ 别再把 `rx_buffer_size` 传 0

本工程屏端不再向 MCU 发任何命令，MCU 侧也没有解析屏数据的逻辑。真正实现"只发不收"的是**"分配了 RX 缓冲，但固件永远不读它"**：

- 不调用 `uart_read_bytes()`、不注册 RX 事件/回调、不创建接收任务；
- 屏端即使发数据，也只会把这 **256 字节** RX 缓冲填满，之后由驱动丢弃，固件侧完全无感；
- RX 引脚（GPIO18）照样绑定，保留只是为了将来排查硬件时不用改配置。

相对于旧版的接收任务，好处是省掉一个任务（含 4 KB 栈）与"RX 回调里发屏"同"其它模块发屏"之间的并发写屏竞争 —— 发送路径只剩一条。

### ⚠️ 硬性约束：`rx_buffer_size` 不能为 0（真机踩过的坑）

把 `rx_buffer_size` 传 0**不是**"省掉 RX 缓冲"，而是**直接让初始化失败**。真机上烧录后启动即挂：

```
E (311) uart: uart_driver_install(1942): uart rx buffer length error
E (321) screen_uart: uart_driver_install failed: ESP_FAIL   ← 串口屏初始化失败，停止启动
```

原因：ESP-IDF v5.5 在 `uart_driver_install()` 里有硬性校验
（`components/esp_driver_uart/src/uart.c`）：

```c
ESP_RETURN_ON_FALSE((rx_buffer_size > UART_HW_FIFO_LEN(uart_num)), ESP_FAIL,
                    TAG, "uart rx buffer length error");
```

即 **`rx_buffer_size` 必须严格大于该 UART 的硬件 FIFO 长度**，否则返回 `ESP_FAIL`；
**允许为 0 的是 `tx_buffer_size`**（TX 没有这条校验）。ESP32-S3 的 UART FIFO 为 **128 字节**
（`SOC_UART_FIFO_LEN = 128`），所以 RX 缓冲取 **256**（`RX_BUF_SIZE`）。

> 连锁反应：这一步失败 → `screen_uart_init()` 返回 `ERR_FAIL` → `screen_init()` 失败 →
> `main.c` 打印"串口屏初始化失败，停止启动"并 return，屏上什么都不显示。

## 3. 接口（`screen_uart.h`）

| 接口 | 说明 |
|---|---|
| `screen_uart_init()` | 安装 UART 驱动（`RX_BUF_SIZE` / `TX_BUF_SIZE`）、设参数、绑定引脚 |
| `screen_uart_send(data, len)` | 把 len 字节**写进 TX 环形缓冲即返回**（后台由驱动发出，不保证此刻已上线）；`data == NULL` 或 `len == 0` 返回 `ERR_PARAM` |
| `screen_uart_send_str(str)` | 同上，发送 C 字符串（不含结尾 `\0`） |

> 因为装了 TX 环形缓冲，`screen_uart_send()` 的"成功"只表示数据已进缓冲；
> 屏上实际收到的内容以日志串口那行 `[SCR] …` 为准（见 `service/screen`）。

> 接收侧的两个接口（回调注册 + 同步读）属于**历史实现**，本版已删除；
> 早先的收发测试记录见 `测试文档/串口屏收发测试记录.md`（T1/T2 为当时的验证过程）。

## 4. 初始化失败的处理（三步都判返回值）

`screen_uart_init()` 的三步都会检查返回值，任何一步失败都返回 `ERR_FAIL`，且**后两步先回滚再上报**：

| 步骤 | 失败时的处理 |
|---|---|
| `uart_driver_install()` | 直接返回 `ERR_FAIL`（此时没有已占用的资源需要回收） |
| `uart_param_config()` | `uart_driver_delete(BSP_UART_SCREEN_NUM)` 卸载驱动 → 返回 `ERR_FAIL` |
| `uart_set_pin()` | 同上：`uart_driver_delete(BSP_UART_SCREEN_NUM)` 回滚 → 返回 `ERR_FAIL` |

失败就地卸载，避免留下一个"半配置"的 UART：波特率/引脚配错了，屏上一个字都不会显示，日志却写着 ready，现场根本无从判断。

> ⚠️ **IDF v5.5 的卸载 API 是 `uart_driver_delete()`**（`driver/uart.h`）。
> 不要写成 `uart_driver_uninstall()` —— 该函数在 v5.5 并不存在，会导致全量构建失败。

`service/screen` 的 `screen_init()` 拿到 `ERR_FAIL` 会打错误日志并让 `main.c` 停止启动（不再发送任何屏指令）。
