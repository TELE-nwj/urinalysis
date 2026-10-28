# third：ESP32-S3 尿量计固件

## 写盘保护更新（2026-10-12）

写盘期间暂停手机状态推送、串口屏刷新并临时关闭液滴中断；Wi-Fi/WebSocket连接及命令确认保持。退出后恢复原采集状态，不清间隔缓冲；storage阶段日志与写盘成功/失败提示保留。15项主机测试及实际编译通过，当前匹配镜像已更新到`firmware/`。详见[更新记录](reports/storage-write-guard-20261012.md)。未烧录、未执行实际U盘写入验证，已有FAT损坏尚未修复。

本目录是最终集成版本的独立 ESP-IDF 工程，可在本机编译，并通过显式指定的
ESP32-S3 下载串口烧录。源码来源于最终验收的 `mobile-app-integration/second sol`，
运行行为、GPIO、CH376、计量阈值和电量算法保持一致；只将工程名改为 `third`。

## 目录与层级

- `main/main.c`：入口及初始化编排，依次准备服务、手机适配器和 monitor。
- `main/app/`：monitor 唯一状态执行器、S1 测量、S2 暂停、手机适配。
- `main/service/`：计量、时钟、屏显、按键、电量、存储和手机协议。
- `main/driver/`：硬件、SoftAP 和 WebSocket 驱动。
- `main/bsp/`：公共错误码与板级引脚定义。
- `firmware/`：本工程真实构建产生的匹配镜像、偏移参数和 SHA256 校验清单。
- `build/`：本目录本地构建输出，不从其他工程复制 CMake 缓存。
- `reports/`：构建日志、源码一致性校验及交付验证记录。

## 本机编译

在 PowerShell 执行：

```powershell
Set-Location 'F:\ESP32\niaio\git\third'
.\build.ps1
```

脚本使用 `idf-env.ps1` 指定的已安装 ESP-IDF 5.5.5、Python 和 Xtensa 工具链，
显式指定本工程 `-C`、本目录 `build/` 和 `IDF_TARGET=esp32s3`。工程开启
`MINIMAL_BUILD`，避免构建无关 LCD 组件。主程序为 `build/third.bin`。
其他电脑须按实际安装位置调整 `idf-env.ps1`。

## 烧录

必须先识别板子的**实际 ESP32-S3 下载串口**，然后通过 `-Port` 明确传入。
不自动扫描、猜测或打开串口；COM21 是已知 CH340 日志口，不能据此认定下载口。

使用本目录最近一次构建的镜像：

```powershell
.\flash.ps1 -Port COM7  # COM7 仅为示例，请替换为已确认的下载串口
```

使用随本目录交付的已编译镜像，无需先重新构建：

```powershell
.\flash.ps1 -Port COM7 -UsePrebuilt  # COM7 仅为示例
```

上面的 COM7 需替换成实际串口名；仅在确认下载口为 COM7 后才传入 `-Port COM7`。
脚本先检查 ESP32-S3 构建元数据和所有镜像，再从相应目录读取实际 `flash_args`。
默认波特率 460800，可通过 `-Baud` 调整。交付期间未执行本脚本、未烧录、未占串口。

默认镜像参数为 DIO、80 MHz、2 MB Flash；偏移以实际 `firmware/flash_args` 为准：

| 偏移 | 镜像 |
| --- | --- |
| `0x0` | `bootloader/bootloader.bin` |
| `0x8000` | `partition_table/partition-table.bin` |
| `0x10000` | `third.bin` |

## 手机连接与控制

热点名：`NIAIO-Monitor`；开发测试密码：`niaio-v1-test`；信道：6；单控制会话。
WebSocket：`ws://192.168.4.1/ws`。热点配置位于 `sdkconfig` 和
`main/Kconfig.projbuild`，当前交付保持已验收配置。设备 ID 为 `NIAIO-` 加 AP MAC
后三字节。开发测试密码在出厂时应按产品配置替换。

手机命令为 start、pause、reset 和一次 HMS 对时；实体 K1 使用同一状态机进行
开始、暂停、继续，K2 复位。暂停冻结有效计时，对时不改变测量时长。每秒完整快照，
八条排队上限，执行前五秒截止，三十二条请求去重。已接受的操作不会因断线自动撤销。

## 验证边界

源码与最终集成版本逐文件一致，顶层 CMake 仅工程名不同。本目录完成真实
ESP32-S3 构建；详细结果及二进制 SHA256 见 `reports/delivery-verification.md`。
既有软件测试与终审结果由最终集成版本提供。

板上启动、下载串口识别、真实 Wi-Fi 配对、Android 手机连板、CH376 U 盘硬件、
实物按键以及 RTOS 栈高水位：**NOT_RUN**。可烧录镜像的交付不表示已经完成实机验证。
