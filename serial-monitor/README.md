# serial-monitor —— 尿量计串口上位机

读取 ESP32-S3 尿量计的串口日志，分区显示关键信息，并可导出 U 盘记录与原始日志。

配套固件：`F:\ESP32\niaio\git\third`（本工具只读日志，**不向设备发任何命令**）。

---

## 快速开始

### 方式一：直接用 exe（推荐）

双击 `dist\SerialMonitor.exe`。

- 选串口 → 点「连接」→ 开始接收
- 没看到串口就点「刷新」
- 想先看效果（没接硬件）：命令行跑
  ```
  SerialMonitor.exe --replay samples\real_sample.log
  ```

### 方式二：从源码运行

```powershell
cd F:\ESP32\niaio\git\serial-monitor
D:\python\python.exe main.py
```

需要 **Python 3.15 + pyserial + PyQt5**（本机已装）。

### 重新打包 exe

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

产物在 `dist\SerialMonitor.exe`（约 38 MB，单文件）。

> ⚠️ `build.ps1` 必须存为 **UTF-8 with BOM**。PowerShell 5.1 会把无 BOM 的 UTF-8 按 GBK 读，中文注释变乱码并引发语法错误。

---

## 界面分区

一屏看全，四个区域：

| 区域 | 内容 | 数据来源 |
|---|---|---|
| **尿液监测** | 累计尿量(大字) / 实时速率 / 累计滴数 / 运行时长 / 运行状态 / 报警 | 固件的 `[串口屏] data.t0~t5` 指令 |
| **存储 / U盘** | U 盘状态 / 当前文件 / 已写入行数 / 写盘失败次数 / 最近结果 / 失败详情 | `S1` 的临界区日志 |
| **设备与网络** | 设备 ID / 热点名 / 设备 IP / 手机连接 / 手机 IP / 手机 MAC | WiFi 驱动的 `softAP` 与 `station join` 日志 |
| **U 盘记录 + 日志** | 每分钟记录表（可排序）；带筛选的原始日志 | 临界区日志 / 全部原始行 |

**过滤**：日志区可按级别（全部/错误/警告/信息）、TAG（全部/仅业务/仅系统）和关键字搜索。
`wifi`、`wifi_init`、`pp`、`net80211`、`phy_init`、`esp_netif_lwip` 等被归为「系统」，默认收起，避免淹没业务日志。

**清除**：日志区右侧有「清除」按钮，一键清空日志与 U 盘记录表，四个分区同时回到未连接态。
**断开串口也会自动清空** —— 避免上一段会话的「U 盘就绪 / 手机已连接」被误当成当前状态。

## 详情视图

「存储 / U盘」和「设备与网络」两张卡片的右上角各有一个**「详情」**入口，点开是一个可同时多开的窗口：

| 部分 | 内容 |
|---|---|
| 上半 | 该类别的**结构化字段表**（键值对照，如 U 盘状态、会话文件、写入行数、失败详情） |
| 下半 | 该类别的**原始日志行** —— 这是排查时最有用的部分 |

**U 盘详情**会带出 `storage` / `CH376T` / `S1` 的全部日志，包括 CH376 版本号、SPI 引脚、落盘验证、进入/退出临界区、错误码等。
**网络详情**会带出 `wifi` / `wifi_init` / `esp_netif_lwip` / `phy_init` 等驱动的全部日志，包括 WiFi 固件版本、PHY 版本、DHCP 参数、station 连接/断开记录。

窗口内还带搜索框（在该类别日志里过滤）、行数计数、「复制全部」和「关闭」。

---

## 导出

| 按钮 | 产物 | 说明 |
|---|---|---|
| 导出 U盘记录 | CSV（UTF-8 **带 BOM**，Excel 双击不乱码） | 列：分钟 / 会话时间 / 本分钟尿量 / 平均流速 / 电脑收到时刻 / 会话文件 |
| 导出日志 | `.log` 纯文本 | 逐行原文，未做任何改动 |

**关于 U 盘记录的一点说明**：U 盘插在设备上时，电脑无法直接读盘。所以这里是**从日志重建**的 ——
固件在写盘前会把整行内容打到日志（`>>>>> 进入临界区(...): 第N分钟 ...`），
字段与 U 盘文件里的记录一一对应，等价于把 U 盘文件实时抄了一份。

---

## 文件结构

```
serial-monitor/
├─ main.py            入口：串口线程、窗口组装、导出、--replay / --selftest / --screenshot
├─ models.py          数据模型与常量（接口契约，改动会影响全部模块）
├─ parser.py          日志解析器（纯逻辑，可脱离界面单测）
├─ ui_main.py         界面布局（不含任何颜色/字体，视觉全交给 style.qss）
├─ style.qss          浅色科研风样式表
├─ tests/             单元测试
├─ samples/           真实日志样本（验收基准）
├─ build.ps1          打包脚本
└─ dist/              exe 产物
```

**分层约定**（改动时请遵守）：

- `parser.py` 只依赖 `models.py`，不依赖 PyQt5 —— 所以能脱离界面测试
- `ui_main.py` **不出现任何 setStyleSheet / 颜色 / 字体硬编码**，一律用 `setObjectName()` + `role`/`state` 动态属性，由 `style.qss` 命中
- `main.py` 只做"接线"（线程、信号、导出），不解析日志、不画界面

---

## 自检与验收

```powershell
# 单元测试（20 个用例）
D:\python\python.exe -m unittest discover -s tests -v

# 界面自检
D:\python\python.exe ui_main.py --selftest

# 端到端：回放真实日志并打印七个分区的解析结果
D:\python\python.exe main.py --replay samples\real_sample.log --selftest

# 截图（用于视觉验收）
D:\python\python.exe main.py --replay samples\real_sample.log --screenshot preview.png
```

`--selftest` 会逐项打印尿液/存储/网络/U盘记录/事件，便于和样本人工核对。

---

## 已知边界

| 项 | 说明 |
|---|---|
| **手机发来的命令看不到** | 固件的 `service/mobile` 没有任何日志，只有连接层（`station join` / DHCP）可见。要监控命令收发需先给固件加日志 |
| **日志时间戳是开机毫秒** | 不是墙上时间。界面显示的是**电脑收到该行的时刻**；设备时间请从 `data.t6` 读取 |
| **实时电量/尿量无周期日志** | 靠解析 `[串口屏] data.t7/t0` 指令获得（每秒 / 测量时 200ms） |
| **只读** | 本工具不向串口写入任何字节，不影响设备运行 |
| **日志上限 5 万行** | 超出丢最旧，长时间运行不会撑爆内存 |

---

## 技术栈

Python 3.15 · PyQt5 5.15.11 · pyserial 3.5 · PyInstaller 6.22.3
