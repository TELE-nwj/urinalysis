# driver/ch376 — CH376 U盘 SPI 驱动

> CH376T 芯片，**软件模拟 SPI** 与主机通讯（未用 IDF SPI 驱动，纯 GPIO 翻转时序）。
> 引脚见 `bsp/pin_map.h`：SCS=GPIO2、SDI(MOSI)=41、SDO(MISO)=40、SCK=42、INT=GPIO1。
> 唯一上层调用方：`service/storage/storage.c`。

## 实现分三段

### 1) GPIO 软 SPI 底层原语
- `CH376_PORT_INIT()`：配置 SCS/SCK/SDI 为输出、SDO/INT 为输入。
- `ch376_spi_OutByte(d)` / `ch376_spi_InByte()`：按位翻转 SCK 逐 bit 收发（MSB 先行）。
- `CH376_Cmd()/CH376_Write()/CH376_Read()/CH376_Cmd_End()`：控制片选 SCS 的命令/数据相位。

### 2) 中断握手 `wait()`
USB/文件操作多为“发命令 → 等 INT 拉低 → 读状态码”的阻塞握手：
- 轮询 INT 脚，内置超时（约 1e5 次）并 `vTaskDelay(1ms)` 让出 CPU；
- 超时返回 `0xff`，成功则 `CH376_GetStatus()` 读状态码。
- ⚠️ **隐患**：`wait()` 内大量 `ESP_LOGI`，每次写盘会刷屏并拖慢。

### 3) 命令级封装
按 CH376 手册命令码逐条封装（头文件对每个 `CMD_*` 有注释）。storage 实际只用到：
- `CH376T_SetUsbMode()` 进 USB 主机模式
- `CH376T_CMD_DISK_CONNECT()` 检测磁盘连接
- `CH376T_FILE_CREATE()` 创建/打开文件
- `CH376T_BYTE_WRITE_DATA(data,len)` 分批写入（等就绪 → 读可写字节 → 写 → 确认，循环至写完）
- `CH376T_FILE_CLOSE()` 关闭文件

另含若干“已定义但暂未使用/未验证”的封装：`CH376T_BYTE_READ()`、`CH376T_DISK_MOUNT()`、`CH376T_GetFileSize()` 等（为调试/后续扩展预留）。

## ⚠️ 设计问题（代码遗留）
1. **文件名硬编码**：`/ASD.TXT` 与调试串 `"hello world"` 写死在驱动内，storage 无法更换文件名。
2. **状态码不统一**：如 `FILE_CLOSE` 直接判 `0x14` 而未用 `USB_INT_SUCCESS` 宏。
3. `wait()` 日志刷屏。

## 对外建议
后续如需“多文件 / 追加日志 / 可配置文件名”，应重构该层或 storage，把文件名、写模式参数化暴露出来。
