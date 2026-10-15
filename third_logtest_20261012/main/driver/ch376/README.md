# driver/ch376 — CH376 U盘 SPI 驱动

> CH376T 芯片，**软件模拟 SPI**（纯 GPIO 翻转时序，未用 IDF SPI 驱动）。
> 引脚见 `bsp/pin_map.h`：**SCS=GPIO2、SDI(MOSI)=GPIO41、SDO(MISO)=GPIO40、SCK=GPIO42、INT=GPIO1**。
> 唯一上层调用方：`service/storage/storage.c`。

> 本文件基本沿用参考工程 `F:\ESP32\niaio\project\main\ch376_spi.c`（**已验证可跑**），
> 只在其基础上**新增**了下面 §3 标注 `【新】` 的几个函数。
> ⚠️ **改这个文件前请先看 §4 踩坑记录** —— 有几次"顺手改规范"反而把它改坏。

---

## 1. 实现分三段

### 1) GPIO 软 SPI 底层原语

| 函数 | 说明 |
|---|---|
| `CH376_PORT_INIT()` | SCS/SCK/SDI 配为输出、SDO/INT 配为输入（内部上拉） |
| `ch376_spi_OutByte(d)` / `ch376_spi_InByte()` | 按位翻转 SCK 逐 bit 收发（MSB 先行，每 bit 延时 `mDelay_uS=2µs`） |
| `CH376_Cmd()` / `CH376_Write()` / `CH376_Read()` / `CH376_Cmd_End()` | 控制片选 SCS 的命令/数据相位 |

### 2) 中断握手 `wait()`

USB/文件操作都是"发命令 → 等 INT# 拉低 → 读状态码"的阻塞握手：

- 轮询 INT 脚（`gpio_get_level == 1` 时继续等），内置超时 **500000 次**（≈7s，挂载大容量 U 盘够用）
- 每 256 次 `vTaskDelay(1ms)` 让出 CPU（避免饿死空闲任务/看门狗）
- 超时返回 `0xff`，否则调 `CH376_GetStatus()` 读状态码（该命令同时**取消中断请求**）

### 3) 命令级封装

**基础/磁盘**

| 函数 | 命令 | 说明 |
|---|---|---|
| `CH376T_Get_IC_Ver()` | 0x01 | 读芯片版本（SPI 通不通的自检，正常值 0x45） |
| `CH376T_SetUsbMode()` | 0x15 | 进 USB 主机模式（mode 0x06，**延时 50ms** 再读状态） |
| `CH376T_CMD_DISK_CONNECT()` | 0x30 | 检测 U 盘（返回 0/1） |
| `CH376T_DiskCheckRaw()` | 0x30 | **【新】返回原始状态码**，用于识别介质拔插（见 §2） |
| `CH376T_DISK_MOUNT()` | 0x31 | 挂载/初始化磁盘（读 FAT） |

**文件操作**

| 函数 | 命令 | 说明 |
|---|---|---|
| `CH376T_FILE_OPEN_STATUS(name)` | 0x2F+0x32 | 【新】按名字打开文件并**返回原始状态码**（`0x14` 存在 / `0x42` 不存在），不打日志；`storage` 用它区分"文件在不在" |
| `CH376T_FILE_OPEN_NAME(name)` | 0x2F+0x32 | 【新】`FILE_OPEN_STATUS` 的包装：成功打 INFO、失败打 ERROR，返回 1/0 |
| `CH376T_FILE_CREATE_NAME(name)` | 0x2F+0x34 | 【新】按名字创建文件 |
| `CH376T_BYTE_WRITE_DATA(data,len)` | 0x3C/0x2D/0x3D | 【新】写任意长度数据（内部分批循环到写完） |
| `CH376T_BYTE_LOCATE(offset)` | 0x39 | 【新】移动文件指针（**追加写必须**：打开后指针在开头，要先定位到文件长度处） |
| `CH376T_FILE_CLOSE()` | 0x36 | 关闭文件（参数 0x01 = **允许更新文件长度**） |
| `CH376T_GetFileSize()` | 0x0C | 读当前文件长度（**注意：该函数没有 `wait()`**，手册也未验证） |
| `CH376T_BYTE_READ()` | 0x3A/0x27/0x3B | 读文件（调试用，写死读 64 字节） |

**清理候选（写死内容、已无人调用）**

`CH376T_FILE_CREATE()`、`CH376T_BYTE_WRITE()`、`CH376T_SET_FILE_NAME()`、
`CH376T_CheckExist()`、`CH376T_SetBaudrate()`、`CH376T_EnterSleep()`、`CH376T_SetSdoInt()`

---

## 2. 状态码速查

| 值 | 宏 | 含义 |
|---|---|---|
| `0x14` | `USB_INT_SUCCESS` | 成功 |
| `0x15` | `USB_INT_CONNECT` | 检测到 USB 设备连接（**介质刚插回**） |
| `0x16` | `USB_INT_DISCONNECT` | 检测到 USB 设备断开（**介质拔出**） |
| `0x1E` | `USB_INT_DISK_WRITE` | 请求写入数据（`BYTE_WRITE` 第一步的正常返回） |
| `0x1F` | `USB_INT_DISK_ERR` | 存储设备操作失败 |
| `0x42` | `ERR_MISS_FILE` | **文件未找到** |
| `0x51` | `CMD_RET_SUCCESS` | "操作状态"类命令（如 `SET_USB_MODE`）的成功码 |
| `0xFF` | — | `wait()` 等中断超时 |

---

## 3. 磁盘操作的正确顺序

```c
CH376_PORT_INIT();
CH376T_SetUsbMode();        /* 0x15，内部延时 50ms */
CH376T_CMD_DISK_CONNECT();  /* 0x30 检测 */
CH376T_DISK_MOUNT();        /* 0x31 挂载（读 FAT） */
... 文件操作 ...
```

**追加写文件的完整流程**：

```c
CH376T_FILE_OPEN_NAME("/ABC.TXT");        /* 0x2F 设名 + 0x32 打开 */
uint32_t len = CH376T_GetFileSize();      /* 0x0C 取长度 */
CH376T_BYTE_LOCATE(len);                  /* 0x39 定位到末尾 ← 不加这步会从头覆盖! */
CH376T_BYTE_WRITE_DATA(buf, n);           /* 0x3C 请求 → 0x2D 写 → 0x3D 确认 */
CH376T_FILE_CLOSE();                      /* 0x36 参数 0x01 → 更新长度并落盘 */
```

---

## 4. ⚠️ 踩坑记录（都真实发生过）

### 4.1 `SET_FILE_NAME` 只认**大写 8.3** 短文件名

传小写 `/hoht.txt` 时，芯片**能用同样字符串 `OPEN` 回来**（日志显示"创建成功、长度正确"），
但写进目录项的名字是坏的 → **电脑上打不开 / 报"找不到文件"**。
参考工程验证过的是 `/ASD.TXT`（全大写）。

→ 上层 `service/storage` 统一把用户名转成 `/大写8.3.TXT`。

### 4.2 `0x30` 之后**必须**补 `0x31`

`CMD_DISK_CONNECT(0x30)` 会让芯片**重新检测/枚举 USB 设备**，从而
**作废已挂载的 FAT 上下文**。如果发完 0x30 不重新 `DISK_MOUNT(0x31)`，
后续文件操作会一律返回 **`0x42 ERR_MISS_FILE`（文件未找到）—— 哪怕文件就在盘上**。

→ 见 `service/storage` 的 `media_sync()`：`0x30` 与 `0x31` 永远成对出现。

### 4.3 `CH376T_CMD_DISK_CONNECT()` 的"不规范"写法**不要改**

它发完命令后会 `CH376_Write(0x00)`，且**不调用** `CH376_Cmd_End()` 就直接进 `wait()`。
看起来"不合规范"，但**参考工程就是这么写的且能跑**。
曾"顺手修正"成 `Cmd_End()` + 去掉那个 0x00 → 现场直接跑不通，已改回。

> 结论：这个文件的底层时序**以参考工程为准**，不要凭手册"优化"。

### 4.4 `wait()` 里必须有 `vTaskDelay`

纯忙等会饿死空闲任务/触发看门狗。当前每 256 次让出 1ms。

### 4.5 日志量

`wait()` / `CH376_GetStatus()` 每次都打 `ESP_LOGI`。写盘时会刷很多行。
`app/S1` 在**写盘临界区内**把 `CH376T`（本驱动）与 `storage`（`service/storage`）两个 TAG 的日志等级
用 `esp_log_level_get()` **先保存**、再 `esp_log_level_set(..., ESP_LOG_NONE)` 降级，出临界区按保存值恢复
（不硬编码写回 `ESP_LOG_INFO`，别人调过的等级不会被踩掉）。

---

## 5. 对外建议

- 文件名、追加模式、写入内容都应**由上层参数化传入**（现在已经是这样：`*_NAME` / `BYTE_WRITE_DATA` / `BYTE_LOCATE`）
- 若需支持中文/长文件名，要走 CH376 的 **LFN** 流程（`0x2F` 之外还要写 LFN 目录项），当前未实现
- `CH376T_GetFileSize()` 没有中断握手，属于"未验证"接口；若后续发现返回值异常，
  改用 `CMD_DIR_INFO_READ(0x37)` 直接读目录项里的文件长度字段
