# bsp 层 — 板级支持

> 把所有"板子相关"的东西收敛到这一层：**换板子/换接线只改这里**，上层驱动不动。

| 文件 | 内容 |
|---|---|
| `pin_map.h` | 所有引脚、串口号、波特率、ADC 单元/通道、分压比 |
| `err.h` | 通用错误码 `err_t` |

---

## 1. `pin_map.h` — 引脚表

### CH376 U盘（软件 SPI）

| 宏 | 引脚 | 说明 |
|---|---|---|
| `BSP_PIN_CH376_SCS` | GPIO2 | 片选 SCS |
| `BSP_PIN_CH376_SDI` | GPIO41 | MOSI → CH376 SDI |
| `BSP_PIN_CH376_SDO` | GPIO40 | MISO ← CH376 SDO |
| `BSP_PIN_CH376_SCK` | GPIO42 | 时钟 |
| `BSP_PIN_CH376_INT` | GPIO1 | 中断（低有效）；驱动靠轮询它握手，**必须接**（`0` 表示不使用） |

> 驱动是 GPIO 位翻转的软件 SPI，任意可用 GPIO 都行；本板沿用旧方案，与「正式版1nwj / 主控」原理图一致。

### 液滴检测

| 宏 | 引脚 | 说明 |
|---|---|---|
| `BSP_PIN_DROP_DETECT` | GPIO10 | 每滴一个上升沿；**内部下拉**、上拉禁用，`GPIO_INTR_POSEDGE` |

### 按键（2 路，低有效/内部上拉）

| 宏 | 引脚 | 说明 |
|---|---|---|
| `BSP_PIN_KEY1` | GPIO8 | 按键 1：开始 / 暂停 / 继续 |
| `BSP_PIN_KEY2` | GPIO9 | 按键 2：复位（任何状态） |

> 接口在 `driver/key`，服务在 `service/input`，业务在 `app/monitor`：软件消抖（连续 3 次采样一致）、**只出短按事件**（无长按/连发）。
> 注：KEY1/KEY2 沿用原 KEY3/KEY4 的引脚；GPIO4 / GPIO5 / GPIO6 现空闲（原 INA228 电量计方案已取消，见下方电池检测段）。

### 串口屏（淘晶驰 TJC，UART1）

| 宏 | 值 | 说明 |
|---|---|---|
| `BSP_UART_SCREEN_NUM` | `UART_NUM_1` | 避开 UART0 日志口 |
| `BSP_PIN_UART_SCREEN_TX` | GPIO17 | MCU TX → 屏 RX |
| `BSP_PIN_UART_SCREEN_RX` | GPIO18 | MCU RX ← 屏 TX（固件不读，仅绑定） |
| `BSP_UART_SCREEN_BAUD` | 115200 | 按屏设置，8N1 无流控 |

### 电池电压检测（ADC 分压）

| 宏 | 值 | 说明 |
|---|---|---|
| `BSP_ADC_BATTERY_UNIT` | `ADC_UNIT_1` | ADC 单元（ADC1） |
| `BSP_PIN_BATTERY_ADC` | GPIO7 | 分压采样脚（ESP32-S3 对应 `ADC1_CH6`） |
| `BSP_BATTERY_ADC_CHANNEL` | `ADC_CHANNEL_6` | 与 GPIO7 对应 |
| `BSP_BATTERY_DIV_NUM` | `400u` | 分压分子：`R59 + R60` |
| `BSP_BATTERY_DIV_DEN` | `200u` | 分压分母：`R60` |

```text
BAT ──R59(200k)──┬──R61(1k)── ADC(GPIO7)
                 │
               R60(200k)      C13(100nF)
                 │               │
                GND             GND
```

采样节点电压 = `V_bat × R60/(R59+R60)` = `V_bat / 2`，所以 **`V_bat = V_adc × DIV_NUM / DIV_DEN`（本板 ×2）**。
`R61(1k)` 是限流电阻（ADC 高阻输入，不参与分压）；`C13(100nF)` 为滤波电容。满电 4.2 V → 2.1 V 入 ADC，放空 2.7 V → 1.35 V，均在 12 dB 量程内。

> **INA228 电量计方案已取消**（原 I2C 占用的 GPIO4/5/6 现空闲）。
> 采集实现见 `driver/battery`（已编入构建），电压→电量见 `service/power`（**必须实机标定**）。

### 其他

| 端口/引脚 | 用途 |
|---|---|
| UART0 | 日志/监控（USB 串口，115200） |
| IO19 / IO20 | 原生 USB（可用 USB-Serial-JTAG 调试） |

> 本板模块 = **ESP32-S3-WROOM-1-N16R8**（ESP32-S3R8，八线 PSRAM），以下不可用：
> IO35 / IO36 / IO37 已连到八线 PSRAM；IO26~IO32 为模块内部 flash/PSRAM，未引出。
> IO0 / IO3 / IO45 / IO46 是 strapping 脚，IO43 / IO44 是日志串口，均已避开。
> IO40~IO42 同时是 JTAG 脚，占用后没有引脚 JTAG（需要时用 IO19/IO20 的 USB-Serial-JTAG）。

---

## 2. `err.h` — 错误码

| 码 | 值 | 含义 |
|---|---|---|
| `ERR_OK` | 0 | 成功 |
| `ERR_FAIL` | -1 | 一般失败 |
| `ERR_TIMEOUT` | -2 | 超时 |
| `ERR_NOT_READY` | -3 | 未就绪（如 U 盘未插入） |
| `ERR_BUSY` | -4 | 忙 |
| `ERR_PARAM` | -5 | 参数错误 |
| `ERR_NO_MEM` | -6 | 内存不足 |
| `ERR_NOT_SUP` | -7 | 不支持/未实现 |

调用约定：**能失败的函数返回 `err_t`，调用方按需处理并打日志**。
app 层对"非关键失败"（如 U 盘未就绪、ADC 初始化失败）一般是告警后继续运行，不阻塞状态机。

---

## 3. 注意

- 固件**不接收屏的数据**（`service/screen` 不再注册接收回调，`driver/screen_uart` 分配了 256 字节 RX 缓冲但固件从不读取它）。屏端上电时可能回传一帧 `1A FF FF FF`，由驱动丢弃，固件不做处理。
- CH376 的 INT 脚**必须接**（`wait()` 靠轮询它握手）；若某块板子没接，要把 `BSP_PIN_CH376_INT` 改成 `0` 并改用 SDO 查询方式（当前驱动未实现该分支）。
