# bsp 层 — 板级支持

> 把所有"板子相关"的东西收敛到这一层：**换板子/换接线只改这里**，上层不动。

| 文件 | 内容 |
|---|---|
| `pin_map.h` | 所有引脚、串口号、波特率 |
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
| `BSP_PIN_CH376_INT` | GPIO1 | 中断（低有效），`0` 表示不使用 |

### 液滴检测

| 宏 | 引脚 | 说明 |
|---|---|---|
| `BSP_PIN_DROP_DETECT` | GPIO10 | 每滴一个上升沿，内部下拉 |

### 串口屏（淘晶驰 TJC，UART1）

| 宏 | 值 | 说明 |
|---|---|---|
| `BSP_UART_SCREEN_NUM` | `UART_NUM_1` | 避开 UART0 日志口 |
| `BSP_PIN_UART_SCREEN_TX` | GPIO17 | MCU TX → 屏 RX |
| `BSP_PIN_UART_SCREEN_RX` | GPIO18 | MCU RX ← 屏 TX |
| `BSP_UART_SCREEN_BAUD` | 115200 | 按屏设置 |

### 按键（4 路，低有效/内部上拉）

| 宏 | 引脚 |
|---|---|
| `BSP_PIN_KEY1` | GPIO4 |
| `BSP_PIN_KEY2` | GPIO5 |
| `BSP_PIN_KEY3` | GPIO8 |
| `BSP_PIN_KEY4` | GPIO9 |

> 接口在 `driver/key`，**业务未接入**。

### 电池 ADC（占位）

| 宏 | 值 |
|---|---|
| `BSP_ADC_BATTERY_UNIT` | `ADC_UNIT_1` |
| `BSP_PIN_BATTERY_ADC` | GPIO7 |

> `driver/battery` 仍是 stub，未实现。

### 其他

| 端口 | 用途 |
|---|---|
| UART0 | 日志/监控（USB 串口，115200） |

---

## 2. `err.h` — 错误码

| 码 | 含义 |
|---|---|
| `ERR_OK` | 成功 |
| `ERR_FAIL` | 通用失败 |
| `ERR_PARAM` | 参数非法 |
| `ERR_NOT_READY` | 资源未就绪（如 U 盘未插入） |
| `ERR_TIMEOUT` | 超时 |
| `ERR_NO_MEM` | 内存不足 |

调用约定：**能失败的函数返回 `err_t`，调用方按需处理并打日志**。
app 层对"非关键失败"（如 U 盘未就绪）一般是告警后继续运行，不阻塞状态机。

---

## 3. 注意

- 上电时屏可能回传 `1A FF FF FF`，处理逻辑在 `service/screen`（不是 bsp 的事）
- CH376 的 INT 脚**必须接**（`wait()` 靠轮询它握手）；若某块板子没接，
  要把 `BSP_PIN_CH376_INT` 改成 `0` 并改用 SDO 查询方式（当前驱动未实现该分支）
