"""
数据模型与全局常量 —— 本文件是全工程的接口契约。

约定：
  · 所有字段一律允许为 None，表示"日志里还没出现该信息"。
    UI 遇到 None 时显示 "--"，不得显示 0 或空串（会误导操作者）。
  · 时间有两套：ts_ms 是设备开机毫秒（设备自己的相对时间，重启归零）；
    received_at 是电脑收到该行的本机时刻（墙上时间，用于导出与排序）。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Optional


# ============================================================
# 日志级别
# ============================================================

class Level(str, Enum):
    """ESP-IDF 的日志级别。值就是日志行里那个字母。"""

    ERROR = "E"
    WARN = "W"
    INFO = "I"
    DEBUG = "D"
    VERBOSE = "V"

    @property
    def cn(self) -> str:
        """级别的中文名，用于界面显示。"""
        return {
            "E": "错误",
            "W": "警告",
            "I": "信息",
            "D": "调试",
            "V": "详细",
        }[self.value]


# ============================================================
# TAG 分层 —— 决定日志区默认是否折叠
# ============================================================
# 系统层 TAG：ESP-IDF 协议栈/驱动自己打的，噪声大、对操作者价值低，
# 默认折叠。用户可展开查看。
SYSTEM_TAGS: frozenset[str] = frozenset({
    "wifi", "wifi_init", "pp", "net80211", "phy_init", "esp_netif_lwip",
    "esp_netif", "lwip", "boot", "bootloader", "esp_image", "spi_flash",
    "heap_init", "cpu_start", "esp_timer", "intr_alloc", "esp_rom",
    "esp_system", "nvs", "nvs_flash", "httpd", "httpd_ws", "esp_http_server",
    "system_api", "esp_event", "vfs", "newlib",
})

# 业务层 TAG：本项目自己的模块打的，是操作者真正关心的，默认展开。
BUSINESS_TAGS: frozenset[str] = frozenset({
    "main", "monitor", "S1", "S2", "screen", "storage", "power", "clock",
    "input", "drop", "key", "battery", "CH376T", "walltime", "screen_uart",
    "urine", "mobile",
})


def tag_group(tag: str) -> str:
    """把一个 TAG 归到 'system' 或 'business'（未知 TAG 算业务，宁可多显示）。"""
    return "system" if tag in SYSTEM_TAGS else "business"


# ============================================================
# 数据模型
# ============================================================

@dataclass
class LogLine:
    """一行解析后的日志。"""

    ts_ms: Optional[int]          # 设备开机毫秒；解析不出为 None
    level: Optional[Level]        # 级别；解析不出为 None
    tag: str                      # 模块名；解析不出为空串
    msg: str                      # 消息体（原始文本）
    raw: str                      # 原始整行，导出日志时用这个
    received_at: datetime         # 电脑收到该行的时刻


@dataclass
class UrineState:
    """尿液监测区。数据来源：[串口屏] data.t0 ~ data.t7 的屏指令。"""

    total_ml: Optional[float] = None    # 累计尿量 ml        (data.t0)
    rate_mlh: Optional[float] = None    # 实时速率 ml/h      (data.t1)
    drops: Optional[int] = None         # 累计滴数           (data.t2)
    elapsed: Optional[str] = None       # 运行时长 hh:mm:ss  (data.t3)
    state: Optional[str] = None         # reset/start/stop   (data.t4)
    alert: Optional[str] = None         # A/B/C/D/E          (data.t5)

    # 以下三项不在尿液区显示，但解析出来供其它区/导出使用
    clock_hms: Optional[str] = None     # 设备墙上时间       (data.t6)
    battery: Optional[str] = None       # 电量，如 "85%"     (data.t7)


@dataclass
class StorageState:
    """存储 / U 盘区。数据来源：S1 的临界区日志与 storage 的初始化日志。"""

    ready: Optional[bool] = None        # U 盘当前是否可用
    session: Optional[str] = None       # 当前会话文件主体，如 U0000001
    lines_written: int = 0              # 已成功写入 U 盘的行数（本机统计）
    write_failures: int = 0             # 写盘失败次数
    last_result: Optional[str] = None   # 最近一次写盘结果描述
    last_result_ok: Optional[bool] = None
    last_error: Optional[str] = None    # 最近一次失败详情（阶段 + 错误码 + 说明）
    checked_at: Optional[datetime] = None

    @property
    def health(self) -> str:
        """给界面用的健康度文字。"""
        if self.ready is None:
            return "未探测"
        return "就绪" if self.ready else "不可用"


@dataclass
class NetworkState:
    """设备与网络区。数据来源：WiFi 驱动的启动日志 + station join/leave。"""

    ap_mac: Optional[str] = None        # 设备热点 MAC，如 44:B1:76:C5:15:B5
    device_id: Optional[str] = None     # 由 ap_mac 后三字节反推，NIAIO-XXXXXX
    ap_ip: Optional[str] = None         # 设备热点 IP，通常 192.168.4.1
    ssid: str = "NIAIO-Monitor"         # 热点名（固件固定，这里给默认值）
    sta_mac: Optional[str] = None       # 已连上手机的 MAC
    sta_ip: Optional[str] = None        # 手机的 IP，如 192.168.4.2
    connected: bool = False             # 手机当前是否连着


@dataclass
class RecordRow:
    """U 盘记录表的一行 —— 从 S1 的临界区日志重建，与 U 盘文件内容等价。"""

    minute: int                         # 第几分钟
    time_hms: str                       # 会话时间 hh:mm:ss
    ml: str                             # 本分钟尿量，如 "1.6ml"（保留原文本）
    rate: str                           # 本分钟平均流速，如 "96.0ml/h"
    received_at: datetime               # 电脑收到该行的时刻
    session: Optional[str] = None       # 属于哪个会话文件（可空）


@dataclass
class AlertEvent:
    """报警/结论事件，用于时间线。"""

    at: datetime
    kind: str                           # "状态" / "24h结论" / "尿袋满" / "写盘失败" / ...
    text: str


@dataclass
class SessionInfo:
    """一个测量会话（设备每次重启或复位后算新会话）。"""

    started_at: datetime
    boot_text: str = ""                 # 启动那行的内容（含编译时间）
    records: list[RecordRow] = field(default_factory=list)


# ============================================================
# 便捷常量
# ============================================================

BAUD_CHOICES = (115200, 921600, 460800, 230400, 57600, 9600)
DEFAULT_BAUD = 115200

# 日志区保留上限：超出丢最旧，避免长时间运行内存膨胀
MAX_LOG_LINES = 50_000

# 报警字母含义（与固件 monitor.c 的约定一致）
ALERT_MEANING = {
    "A": "正常",
    "B": "无尿",
    "C": "少尿",
    "D": "多尿",
    "E": "尿袋满",
}

# 状态字含义（data.t4 发的是命令字，屏端既显示也判断）
STATE_MEANING = {
    "reset": "待开始",
    "start": "测量中",
    "stop": "暂停",
}
