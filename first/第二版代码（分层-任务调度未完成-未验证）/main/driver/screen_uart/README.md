# driver/screen_uart — 串口屏 UART 收发

> 只管串口屏的**底层收发**，**不解析协议**（协议解析在 `service/screen/screen.c`）。
> 外设：UART1；引脚 TX=GPIO17、RX=GPIO18；波特率 115200（见 `bsp/pin_map.h`）。
> 上层调用方：`service/screen/screen.c`。

## 初始化 `screen_uart_init()`
1. 串口参数：115200 / 8N1 / 无流控。
2. `uart_driver_install(UART1, RX_BUF=1024, TX_BUF=256, ...)`。
3. `uart_set_pin` 绑定 TX/RX。

## 收发接口
| 接口 | 说明 |
|---|---|
| `screen_uart_send(data,len)` / `send_str(str)` | 发送原始字节 / 字符串 |
| `screen_uart_read(buf,size,timeout)` | 读取一帧（返回字节数） |
| `screen_uart_set_rx_cb(cb)` | 注册接收回调；注册后创建 `uart_rx` 任务 |

## 接收任务 `uart_rx_task`（栈 2048 / 优先级 6）
- 阻塞 `uart_read_bytes(..., 50ms)`；
- 收到数据即调 `s_rx_cb(buf,len)` 交给上层（`service/screen/screen.c` 的 `on_uart_rx` 负责按帧尾 `FF FF FF` 组帧）。

## 设计要点
该层是**纯字节通道**，让“如何切帧、如何解释指令/回传”完全留给 service 层，职责单一。
