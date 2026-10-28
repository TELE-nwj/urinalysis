"""
串口日志解析器 —— 把 ESP-IDF 格式的串口输出解析成 models.py 里的结构化状态。

设计要点：
  · 纯逻辑：不依赖 Qt / 串口 / 线程；串口线程只要把「整行文本」交给 feed_line()。
  · 一行可以同时命中多条业务规则（例如 S1 的临界区行既是通用日志，又是 U 盘记录行），
    所以各规则之间是并列匹配，不是 if/elif 的互斥关系。
  · feed_line() 对「空行 / 纯空白行」返回 None（没有可显示的内容）；
    对非空但解析不出头部的行（半行、乱码）仍然返回 LogLine
    （level/ts_ms 为 None、tag 为空串、msg 为整行），保证任何输入都不抛异常。
  · 状态字段一律「日志里还没出现就是 None」，绝不用 0 / 空串冒充（见 models.py 约定）。

只对外暴露 LogParser 一个类。
"""

from __future__ import annotations

import re
from datetime import datetime
from typing import Optional

from models import (
    MAX_LOG_LINES,
    AlertEvent,
    Level,
    LogLine,
    NetworkState,
    RecordRow,
    StorageState,
    UrineState,
)

__all__ = ["LogParser"]


# ============================================================
# 正则表
# ============================================================

# 通用行： I (763) wifi: 消息体
#   · 用非贪婪的 (.*?) 而不是 [^:]+：真实固件会打出
#     "I (773) wifi:wifi firmware version: b9f67df" 这种 TAG 与消息之间没有空格的行，
#     非贪婪匹配能正确切成 tag="wifi"、msg="wifi firmware version: b9f67df"；
#     TAG 本身不含冒号时，结果与 [^:]+ 完全一致。
#   · 冒号后的 \s* 同时容忍 "tag: msg" 与 "tag:msg" 两种写法。
_RE_LOG = re.compile(r"^([IWEDV])\s*\((\d+)\)\s*(.*?):\s*(.*)$")

# 串口屏屏指令：[串口屏] data.tN.txt="值"
#   · 必须带 [串口屏] 前缀：E 级的「数据帧发送失败: data.t0.txt="1.0"」表示这一帧
#     根本没发到屏上，屏上数字没变，不能拿它污染尿量状态。
#   · 值转不成数字时保持原值（不写 None、不抛异常）。
_RE_T0 = re.compile(r'\[串口屏\]\s*data\.t0\.txt="([^"]*)"')
_RE_T1 = re.compile(r'\[串口屏\]\s*data\.t1\.txt="([^"]*)"')
_RE_T2 = re.compile(r'\[串口屏\]\s*data\.t2\.txt="([^"]*)"')
_RE_T3 = re.compile(r'\[串口屏\]\s*data\.t3\.txt="([^"]*)"')
_RE_T4 = re.compile(r'\[串口屏\]\s*data\.t4\.txt="([^"]*)"')
_RE_T5 = re.compile(r'\[串口屏\]\s*data\.t5\.txt="([^"]*)"')
_RE_T6 = re.compile(r'\[串口屏\]\s*data\.t6\.txt="([^"]*)"')
_RE_T7 = re.compile(r'\[串口屏\]\s*data\.t7\.txt="([^"]*)"')

# (正则, UrineState 字段, 目标类型)：data.t0 ~ data.t7 与模型字段的对应表
_SCREEN_TABLE: tuple[tuple[re.Pattern, str, type], ...] = (
    (_RE_T0, "total_ml", float),
    (_RE_T1, "rate_mlh", float),
    (_RE_T2, "drops", int),
    (_RE_T3, "elapsed", str),
    (_RE_T4, "state", str),
    (_RE_T5, "alert", str),
    (_RE_T6, "clock_hms", str),
    (_RE_T7, "battery", str),
)

# U 盘记录行：S1 进入临界区时把本分钟的数据打出来
#   · 列之间是多个空格（对齐用），必须用 \s+，不能用单空格。
_RE_RECORD = re.compile(
    r"进入临界区.*第(\d+)分钟\s+(\d{2}:\d{2}:\d{2})\s+([\d.]+ml)\s+([\d.]+ml/h)"
)

# 写盘成功 / 写盘失败
_RE_WRITE_OK = re.compile(r"退出临界区: 第(\d+)分钟已写入U盘: /(\S+)\.TXT")
_RE_WRITE_FAIL = re.compile(
    r"写盘失败\((.+?)阶段 错误码=(-?\d+) (.+?)\).*?(\d+)秒后重试"
)

# 状态转换 / 24h 结论 / 尿袋满
_RE_STATE = re.compile(r"状态 (\S+) -> (\S+)")
_RE_24H = re.compile(r"首个24小时尿量=([\d.]+)ml，结论=([A-E])")
_RE_BAG_FULL = re.compile(r"尿袋满\(累计([\d.]+)ml\)")

# 网络
_RE_AP_MAC = re.compile(r"mode : softAP \(([0-9a-fA-F:]{17})\)")
_RE_AP_IP = re.compile(r"DHCP server started on interface \S+ with IP: ([\d.]+)")
_RE_STA_JOIN = re.compile(r"station: ([0-9a-fA-F:]{17}) join")
_RE_STA_LEAVE = re.compile(r"station: ([0-9a-fA-F:]{17}) leave")
_RE_STA_IP = re.compile(r"DHCP server assigned IP to a client, IP is: ([\d.]+)")

# 存储：会话文件
#   · 老样本（samples/real_sample.log）用的是「已创建 / 已存在」两种说法；
#   · 真机（samples/real_device.log）用的是「新测量文件 / 已就绪 / 打开用户文件」。
#     两套格式都要兼容。
_RE_SESSION_CREATED = re.compile(r"已创建测量文件并写入表头: (\S+)\.TXT")
_RE_SESSION_EXISTING = re.compile(r"测量文件已存在.*?: (\S+)\.TXT")
_RE_SESSION_NEW = re.compile(r"新测量文件: /(\S+)\.TXT")
_RE_SESSION_READY = re.compile(r"测量文件 /(\S+)\.TXT 已就绪")
_RE_SESSION_OPEN = re.compile(r"打开用户文件\(追加模式[^)]*\): /(\S+)\.TXT")
# 会话建立失败（真机）：`<<<<< 退出临界区: 新建文件失败(错误码=-1 一般失败)，U盘仍不可用…`
#   注意与「写盘失败」区分：那条是 `退出临界区: 第N分钟写盘失败(写入阶段 错误码=…`。
_RE_SESSION_FAIL = re.compile(r"退出临界区: (新建文件|恢复会话)失败\(错误码=(-?\d+) (.+?)\)")

# 能用来确定「当前会话名」的正则，按可信度从高到低。
#   刻意**不收**真机的 `用户名 'U0000003' → 文件名 '/U0000003.TXT'`：那是 CH376
#   在逐个试探可用名字（真机同一秒会连打 U0000001 / U0000002 / U0000003），
#   拿它定会话会把名字定成第一个试探的 U0000001，比空着更糟。
_SESSION_PATTERNS: tuple[re.Pattern, ...] = (
    _RE_SESSION_NEW,        # 「新测量文件: /X.TXT」—— 设备自己宣布的新文件，最权威
    _RE_SESSION_READY,      # 「测量文件 /X.TXT 已就绪」—— S1 确认这个文件能用
    _RE_SESSION_OPEN,       # 「打开用户文件(追加模式...): /X.TXT」—— 当前正在追加的文件
    _RE_SESSION_CREATED,    # 老样本：已创建测量文件并写入表头
    _RE_SESSION_EXISTING,   # 老样本：测量文件已存在
)

# 设备启动（每次启动算一个新会话）
_RE_BOOT = re.compile(r"尿量计启动.*编译于 (.*)")

# 级别字母 -> Level 枚举；用查表而不是 Level()，避免任何输入导致异常
_LEVEL_BY_VALUE: dict[str, Level] = {lv.value: lv for lv in Level}

# 「详情」弹窗的分类白名单：类别 -> 该类别的 TAG。
#   storage 是 U 盘那条链路（storage 模块 / CH376 芯片驱动 / S1 每分钟写盘），
#   network 是 WiFi 协议栈与网络服务那一段，用来给操作者看初始化码与版本号。
_CATEGORY_TAGS: dict[str, frozenset[str]] = {
    "storage": frozenset({"storage", "CH376T", "S1"}),
    "network": frozenset({
        "wifi", "wifi_init", "esp_netif_lwip", "pp", "net80211",
        "phy_init", "httpd", "httpd_ws", "esp_netif", "nvs", "nvs_flash",
    }),
}


# ============================================================
# 小工具
# ============================================================

def _cast(caster: type, text: str):
    """把屏指令里的文本转成目标类型；空串 / 非法值返回 None（保持原值，不抛异常）。"""
    if caster is str:
        return text or None
    try:
        return caster(text)
    except (TypeError, ValueError):
        return None


def _norm_mac(mac: str) -> str:
    """MAC 统一成大写冒号形式：44:b1:76:c5:15:b5 → 44:B1:76:C5:15:B5。"""
    return mac.strip().upper()


def _mac_to_device_id(mac: str) -> Optional[str]:
    """由热点 MAC 的后三字节反推设备号：44:B1:76:C5:15:B5 → NIAIO-C515B5。"""
    parts = mac.strip().upper().split(":")
    if len(parts) != 6:
        return None
    return "NIAIO-" + "".join(parts[3:])


# ============================================================
# 解析器
# ============================================================

class LogParser:
    """
    日志解析器。构造无参；把每一行原始文本喂给 feed_line()，再随时取状态。

    解析结果都是内部状态的「当前值」：同名字段后来居上，只有 U 盘记录表和
    报警时间线是累积的。
    """

    def __init__(self) -> None:
        self._init_state()

    def _init_state(self) -> None:
        """把全部内部状态置成「刚构造完」的样子。

        __init__ 与 reset() 共用同一份初始化，这样以后新增字段也不会漏清。
        """
        self._urine = UrineState()
        self._storage = StorageState()
        self._network = NetworkState()
        self._records: list[RecordRow] = []
        self._alerts: list[AlertEvent] = []
        self._boot_text: str = ""
        self._session_index: int = 0
        # 还没有会话名的记录条数：用来判断要不要回填（避免每次都扫全表）
        self._records_without_session: int = 0
        # 详情弹窗的数据源：每一行的 LogLine（原始字符串由 main.py 单独留档）
        self._raw_lines: list[LogLine] = []

    # --------------------------------------------------------
    # 对外接口
    # --------------------------------------------------------

    def feed_line(self, raw: str) -> Optional[LogLine]:
        """
        喂入一行原始文本：解析出 LogLine，同时更新业务状态。

        返回 None 仅限两种情况：raw 不是字符串、或整行只有空白（没有可显示的内容）。
        其余任何输入都返回 LogLine —— 解析不出头部时 level/ts_ms 为 None、tag 为空、
        msg 为整行；解析过程中绝不会抛异常（解析器跑在串口线程里）。

        返回的 LogLine 同时被记进内部详情数据源（lines_for() 用），
        上限 models.MAX_LOG_LINES，超出丢最旧。
        """
        if not isinstance(raw, str) or not raw.strip():
            return None

        text = raw.rstrip("\r\n")          # 只去掉行尾换行，行内空格原样保留
        now = datetime.now()
        try:
            line = self._parse_head(text, now)
            self._apply_rules(line)
        except Exception:
            # 最后一道保险：宁可退化成「原始行」，也不能让串口线程挂掉、丢日志。
            line = LogLine(
                ts_ms=None, level=None, tag="", msg=text, raw=text, received_at=now
            )
        self._remember(line)
        return line

    def urine(self) -> UrineState:
        """尿液监测区状态。"""
        return self._urine

    def storage(self) -> StorageState:
        """存储 / U 盘区状态。"""
        return self._storage

    def network(self) -> NetworkState:
        """设备与网络区状态。"""
        return self._network

    def records(self) -> list[RecordRow]:
        """U 盘记录表（累积，按日志出现顺序）。"""
        return list(self._records)

    def alerts(self) -> list[AlertEvent]:
        """报警 / 结论时间线（累积，按日志出现顺序）。"""
        return list(self._alerts)

    def reset(self) -> None:
        """
        清空全部内部状态，回到「刚构造完」的样子。

        断开串口 / 点界面上的「清除」时由 main.py 调用：上一段会话的残留数据
        （例如 U 盘「就绪」、网络「已连接」）不能继续显示在界面上。
        可反复调用；清空后继续 feed_line() 一切照常。
        """
        self._init_state()

    def lines_for(self, category: str) -> list[LogLine]:
        """
        返回指定类别的原始日志行（保持原始先后顺序），供界面做「详情」弹窗。

        category 取 "storage"（U 盘链路）或 "network"（WiFi / 网络服务），
        其它值一律返回空列表，不抛异常。返回的是副本，调用方改动不影响内部状态。
        """
        if not isinstance(category, str):
            return []
        tags = _CATEGORY_TAGS.get(category)
        if tags is None:
            return []
        return [line for line in self._raw_lines if line.tag in tags]

    # 以下两个只读属性是给 UI 分段用的补充信息，不影响上面五个接口
    @property
    def boot_text(self) -> str:
        """最近一次启动行里「编译于」之后的文本。"""
        return self._boot_text

    @property
    def session_index(self) -> int:
        """见到过几次启动行（第几个会话，从 1 开始）。"""
        return self._session_index

    # --------------------------------------------------------
    # 通用行
    # --------------------------------------------------------

    def _remember(self, line: LogLine) -> None:
        """把这一行记进详情数据源；超过 models.MAX_LOG_LINES 丢最旧。"""
        self._raw_lines.append(line)
        overflow = len(self._raw_lines) - MAX_LOG_LINES
        if overflow > 0:
            del self._raw_lines[:overflow]

    def _parse_head(self, text: str, now: datetime) -> LogLine:
        """拆出 level / ts_ms / tag / msg；拆不出来就整行当消息，字段置 None。"""
        m = _RE_LOG.match(text)
        if not m:
            return LogLine(
                ts_ms=None, level=None, tag="", msg=text, raw=text, received_at=now
            )
        level_s, ts_s, tag, msg = m.groups()
        return LogLine(
            ts_ms=int(ts_s),
            level=_LEVEL_BY_VALUE.get(level_s),
            tag=tag.strip(),
            msg=msg.rstrip(),
            raw=text,
            received_at=now,
        )

    # --------------------------------------------------------
    # 业务规则（并列匹配，一行可命中多条）
    # --------------------------------------------------------

    def _apply_rules(self, line: LogLine) -> None:
        msg, at = line.msg, line.received_at
        self._rule_screen(msg)
        self._rule_record(msg, at)
        self._rule_write(msg, at)
        self._rule_session_fail(msg)
        self._rule_status(msg, at)
        self._rule_network(msg)
        self._rule_storage(msg, at)
        self._rule_boot(msg, at)

    def _rule_screen(self, msg: str) -> None:
        """尿量 / 速率 / 滴数 / 时长 / 状态 / 报警 / 时钟 / 电量。"""
        for pattern, field, caster in _SCREEN_TABLE:
            m = pattern.search(msg)
            if not m:
                continue
            value = _cast(caster, m.group(1))
            if value is not None:
                setattr(self._urine, field, value)

    def _rule_record(self, msg: str, at: datetime) -> None:
        """S1 临界区行 → U 盘记录表的一行。"""
        m = _RE_RECORD.search(msg)
        if not m:
            return
        minute, time_hms, ml, rate = m.groups()
        self._records.append(
            RecordRow(
                minute=int(minute),
                time_hms=time_hms,
                ml=ml,
                rate=rate,
                received_at=at,
                session=self._storage.session,
            )
        )
        if self._storage.session is None:
            # 会话名还没出现（日志可能从中间开始）：记下来，等 _set_session 回填
            self._records_without_session += 1

    def _rule_write(self, msg: str, at: datetime) -> None:
        """写盘成功 / 失败。"""
        m = _RE_WRITE_OK.search(msg)
        if m:
            minute, name = int(m.group(1)), m.group(2)
            self._storage.lines_written += 1
            self._storage.last_result_ok = True
            self._storage.last_result = f"第{minute}分钟已写入U盘"
            if not self._storage.session:
                self._set_session(name)           # 日志从中间开始时的兜底
            return

        m = _RE_WRITE_FAIL.search(msg)
        if m:
            stage, code, desc, retry = m.groups()
            detail = f"{stage}阶段 错误码={code} {desc}"
            self._storage.write_failures += 1
            self._storage.last_result_ok = False
            self._storage.last_result = "写盘失败"
            self._storage.last_error = detail
            self._alerts.append(
                AlertEvent(
                    at=at,
                    kind="写盘失败",
                    text=f"{detail}，{retry}秒后重试",
                )
            )

    def _rule_session_fail(self, msg: str) -> None:
        """会话建立失败（新建文件失败 / 恢复会话失败）：让界面看得到失败原因。

        U 盘没插好或卡坏时，S1 会每 5 秒重试一次，界面上「存储 / U盘」区
        只显示「不可用」看不出原因，所以这里把「最近一次结果 + 失败详情」写上。
        刻意**不计入** write_failures —— 那是「写盘失败」的计数，两者语义不同；
        也**不进 alerts** —— 每 5 秒重试一次会刷屏。
        """
        m = _RE_SESSION_FAIL.search(msg)
        if not m:
            return
        stage, code, desc = m.groups()
        self._storage.last_result = "会话建立失败"
        self._storage.last_result_ok = False
        self._storage.last_error = f"{stage}失败 错误码={code} {desc}"

    def _rule_status(self, msg: str, at: datetime) -> None:
        """状态转换 / 24h 结论 / 尿袋满 三个事件。"""
        m = _RE_STATE.search(msg)
        if m:
            self._alerts.append(
                AlertEvent(at=at, kind="状态", text=f"{m.group(1)} -> {m.group(2)}")
            )

        m = _RE_24H.search(msg)
        if m:
            self._alerts.append(
                AlertEvent(
                    at=at,
                    kind="24h结论",
                    text=f"首个24小时尿量={m.group(1)}ml，结论={m.group(2)}",
                )
            )

        m = _RE_BAG_FULL.search(msg)
        if m:
            self._alerts.append(
                AlertEvent(at=at, kind="尿袋满", text=f"尿袋满(累计{m.group(1)}ml)")
            )

    def _rule_network(self, msg: str) -> None:
        """热点 MAC / 热点 IP / 手机连上 / 手机断开 / 手机 IP。"""
        m = _RE_AP_MAC.search(msg)
        if m:
            mac = _norm_mac(m.group(1))
            self._network.ap_mac = mac
            self._network.device_id = _mac_to_device_id(mac)

        m = _RE_AP_IP.search(msg)
        if m:
            self._network.ap_ip = m.group(1)

        m = _RE_STA_JOIN.search(msg)
        if m:
            self._network.sta_mac = _norm_mac(m.group(1))
            self._network.connected = True

        m = _RE_STA_LEAVE.search(msg)
        if m and _norm_mac(m.group(1)) == self._network.sta_mac:
            # 只有断开的是当前这台手机，才算「手机断开」；同时清掉它的 IP
            self._network.connected = False
            self._network.sta_ip = None

        m = _RE_STA_IP.search(msg)
        if m:
            self._network.sta_ip = m.group(1)

    def _rule_storage(self, msg: str, at: datetime) -> None:
        """U 盘就绪 / 拔出 / 会话文件名。"""
        if "存储服务就绪" in msg:
            self._storage.ready = True
            self._storage.checked_at = at

        if "检测到U盘已拔出" in msg:
            self._storage.ready = False
            self._storage.checked_at = at

        for pattern in _SESSION_PATTERNS:
            m = pattern.search(msg)
            if m:
                self._set_session(m.group(1))
                break

    def _set_session(self, name: str) -> None:
        """确定当前会话文件名（去掉 .TXT 后缀），并回填此前没有会话名的记录。

        日志很可能从中间开始（用户不会每次都从开机抓），会话名往往比记录行晚到
        （真机上「已写入U盘」比记录行晚 20ms，「新测量文件」更晚）。回填一次，
        导出 CSV 时「会话」列就不会整列空着。
        """
        self._storage.session = name
        if self._records_without_session <= 0:
            return
        for row in self._records:
            if row.session is None:
                row.session = name
        self._records_without_session = 0

    def _rule_boot(self, msg: str, at: datetime) -> None:
        """设备启动：记一条「启动」事件，并推进会话序号。"""
        m = _RE_BOOT.search(msg)
        if not m:
            return
        self._boot_text = m.group(1).strip()
        self._session_index += 1
        self._alerts.append(AlertEvent(at=at, kind="启动", text=msg))
