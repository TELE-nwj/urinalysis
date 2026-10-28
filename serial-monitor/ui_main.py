#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial-monitor 上位机界面（PyQt5）—— 只负责结构与数据绑定。

分工约定（务必遵守，否则样式表会失效）：
  · 本文件**不出现任何 setStyleSheet / 颜色 / 字体 / 边框**。
    所有视觉一律由 style.qss 提供。本文件按 style.qss 文件头的
    【给 ui-dev 的接口约定】命名与标注：
        A. objectName —— 数值标签用 #total_ml_value / #health_value …
        B. 动态属性   —— role="value|value-sm|label|unit|hint|text|status|sectionTitle"
                        state="ok|warn|error|idle|run"（只认 state，没设就中性灰）
    分区骨架用 QGroupBox（style.qss 的主机制：白底 + 1px 边框 + 28px 标题条）。
  · 数值只从 parser.urine() / storage() / network() / records() 读取；
    字段为 None 时显示 "--"（绝不显示 0 或空串），状态色也不设（保持中性）。
  · 顶部操作条的控件只发信号，不做动作；连接/导出逻辑在 main.py。

对外契约（main.py 依赖）：
    MainWindow(parser, parent=None)
        .append_log(models.LogLine) -> None
        .refresh() -> None
        .set_status(str) -> None
        .set_connected(bool) -> None
        .current_port() -> str | None
        .current_baud() -> int
        .connect_requested / .export_records_requested / .export_log_requested

独立自检（无串口、无需 parser.py）：
    D:\\python\\python.exe ui_main.py --mock                  # 假数据窗口，人工看布局
    D:\\python\\python.exe ui_main.py --mock --empty          # 全空数据，检查 "--" 占位
    D:\\python\\python.exe ui_main.py --selftest              # 1.5 秒后自动关闭并打印 OK
    D:\\python\\python.exe ui_main.py --selftest-clear        # 清除流程断言，打印 CLEAR OK
    D:\\python\\python.exe ui_main.py --mock --screenshot a.png
    D:\\python\\python.exe ui_main.py --mock --screenshot-detail 存储 out_storage.png

迭代 2 新增的对外接口：
    .clear_all() -> None          # 清空日志 + 记录表 + 四分区复位为 "--"（main.py 断开时调用）
    .clear_requested              # pyqtSignal()，界面「清除」按钮发出，main.py 清解析器
    .show_detail("storage"|"network") -> DetailDialog   # 详情窗（非模态，可同时开两个）
"""

from __future__ import annotations

import argparse
import html
import sys
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional, Sequence

from PyQt5 import QtCore, QtGui, QtWidgets

import models


# ============================================================
# 日志文本颜色 —— 本文件唯一的"颜色"例外
# ============================================================
# 原因：日志正文是 QTextDocument 富文本/文档块格式，QSS 命中不了其中的字符与行底。
# style.qss 文件头【需要 Python 侧配合】第 1 条明确要求这里用 QTextCharFormat /
# QTextBlockFormat 上色，色值直接取自 style.qss 的调色表，不要另造色。
LOG_TEXT_COLORS: dict[str, str] = {
    # 文字色
    "ts": "#5A6674",     # 电脑接收时刻（次文字）
    "tag": "#5A6674",    # TAG（次文字）
    "msg": "#1A2129",    # 消息正文（主文字）
    "E": "#CF222E",      # 错误
    "W": "#9A6700",      # 警告
    "I": "#1A2129",      # 信息
    "D": "#5A6674",      # 调试
    "V": "#5A6674",      # 详细
    "-": "#1A2129",      # 级别解析不出
    # 整行底色（仅错误/警告）
    "bg_E": "#FFF5F5",
    "bg_W": "#FFFBEB",
}
# 详情弹窗字段表两列的文字色 —— 为什么必须写在 Python 侧：
# QSS 只能命中整个 item，无法区分"第几列"，键列灰/值列深的层次只能由 setForeground 给。
# 色值取自 style.qss 的配色表（次文字 / 主文字），不是自创色；字号与行高仍归 QSS 管。
DETAIL_KEY_COLOR = "#5A6674"      # 键列 —— 次文字灰
DETAIL_VALUE_COLOR = "#1A2129"    # 值列 —— 主文字深

# 日志区下拉框选项（文字 → 内部值）
LEVEL_FILTER_CHOICES = (("全部", None), ("错误", "E"), ("警告", "W"), ("信息", "I"))
TAG_FILTER_CHOICES = (("全部", None), ("仅业务", "business"), ("仅系统", "system"))

# 切换筛选时最多重绘多少行（防止几万行一次性重排卡住界面）
MAX_RENDER_LINES = 5000

# 状态字 → style.qss 的 state 取值（reset 待开始 / start 测量中 / stop 暂停）
URINE_STATE_PROP = {"reset": "idle", "start": "run", "stop": "warn"}
# 报警字母 → state 取值（A 正常 / B 无尿 / C 少尿 / D 多尿 / E 尿袋满）
ALERT_STATE_PROP = {"A": "ok", "B": "warn", "C": "warn", "D": "warn", "E": "error"}


# 详情对话框里"还没有数据"时的占位文字
DETAIL_EMPTY_HINT = "暂无数据（未连接或该设备未上报）"
# 详情对话框的实时刷新间隔（主日志 200ms 刷一次，弹窗没必要跟那么紧）
DETAIL_REFRESH_MS = 1000


def log_line_html(line: "models.LogLine", colors: Optional[dict] = None) -> str:
    """一行日志的富文本：时刻 + 级别字母 + TAG + 消息（主日志区与详情弹窗共用）。"""
    c = colors or LOG_TEXT_COLORS
    ts = line.received_at.strftime("%H:%M:%S") if line.received_at else "--:--:--"
    letter = line.level.value if line.level else "-"
    tag = line.tag or "-"
    msg = line.msg or ""
    return " ".join((
        f'<span style="color:{c.get("ts", c["msg"])};">{html.escape(ts)}</span>',
        f'<span style="color:{c.get(letter, c["msg"])};">{html.escape(letter)}</span>',
        f'<span style="color:{c.get("tag", c["msg"])};">{html.escape(tag)}</span>',
        f'<span style="color:{c.get("msg", "#1A2129")};">{html.escape(msg)}</span>',
    ))


def log_line_bg(line: "models.LogLine", colors: Optional[dict] = None) -> Optional[str]:
    """错误/警告行的整行底色（QSS 管不到文档块，见文件头配合说明）。"""
    if line.level is None:
        return None
    return (colors or LOG_TEXT_COLORS).get(f"bg_{line.level.value}")


def append_log_line(view: QtWidgets.QPlainTextEdit, line: "models.LogLine",
                    colors: Optional[dict] = None) -> None:
    """把一行日志（带颜色与错误/警告行底）追加进 QPlainTextEdit。"""
    view.appendHtml(log_line_html(line, colors))
    bg = log_line_bg(line, colors)
    if bg:
        cursor = view.textCursor()
        cursor.movePosition(QtGui.QTextCursor.End)
        fmt = QtGui.QTextBlockFormat()
        fmt.setBackground(QtGui.QColor(bg))
        cursor.mergeBlockFormat(fmt)


def _num(text) -> float:
    """从 "1.6ml" / "96.0ml/h" 这类文本里抠出数字，用于排序。"""
    if text is None:
        return float("-inf")
    digits = ""
    for ch in str(text):
        if ch.isdigit() or ch == ".":
            digits += ch
        elif digits:
            break
    try:
        return float(digits)
    except ValueError:
        return float("-inf")


# ============================================================
# U 盘记录表格模型
# ============================================================

class RecordsTableModel(QtCore.QAbstractTableModel):
    """U 盘记录表：支持高效追加（只 insert 新行，不重建全表）。"""

    HEADERS = ("分钟", "会话时间", "本分钟尿量", "平均流速", "电脑收到时刻")

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows: list[models.RecordRow] = []

    # ---------- Qt 必需接口 ----------
    def rowCount(self, parent=QtCore.QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent=QtCore.QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self.HEADERS)

    def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
        if role == QtCore.Qt.DisplayRole and orientation == QtCore.Qt.Horizontal:
            if 0 <= section < len(self.HEADERS):
                return self.HEADERS[section]
        return None

    def data(self, index, role=QtCore.Qt.DisplayRole):
        if not index.isValid():
            return None
        row = self._rows[index.row()]
        col = index.column()
        if role == QtCore.Qt.DisplayRole:
            return self._display(row, col)
        if role == QtCore.Qt.UserRole:          # 排序键（proxy 用 UserRole 排序）
            return self._sort_key(row, col)
        if role == QtCore.Qt.TextAlignmentRole:
            return QtCore.Qt.AlignCenter
        if role == QtCore.Qt.ToolTipRole and col == 0 and row.session:
            return f"会话 {row.session}"
        return None

    # ---------- 内部 ----------
    @staticmethod
    def _display(row: models.RecordRow, col: int) -> str:
        if col == 0:
            return str(row.minute)
        if col == 1:
            return row.time_hms or "--"
        if col == 2:
            return row.ml or "--"
        if col == 3:
            return row.rate or "--"
        if col == 4:
            return row.received_at.strftime("%H:%M:%S") if row.received_at else "--"
        return ""

    @staticmethod
    def _sort_key(row: models.RecordRow, col: int):
        if col == 0:
            return row.minute
        if col == 1:
            return row.time_hms or ""
        if col == 2:
            return _num(row.ml)
        if col == 3:
            return _num(row.rate)
        if col == 4:
            return row.received_at.isoformat() if row.received_at else ""
        return ""

    # ---------- 追加 / 重置 ----------
    def append_rows(self, rows) -> None:
        rows = list(rows)
        if not rows:
            return
        start = len(self._rows)
        self.beginInsertRows(QtCore.QModelIndex(), start, start + len(rows) - 1)
        self._rows.extend(rows)
        self.endInsertRows()

    def set_rows(self, rows) -> None:
        self.beginResetModel()
        self._rows = list(rows)
        self.endResetModel()

    def clear(self) -> None:
        self.set_rows([])

    def row_at(self, i: int) -> models.RecordRow:
        return self._rows[i]


# ============================================================
# 详情对话框（U 盘 / 网络）—— 上半结构化字段，下半该类别的原始日志
# ============================================================

class DetailDialog(QtWidgets.QDialog):
    """非模态详情窗：上=键值字段表，下=该类别原始日志行（初始化码都在这儿）。

    可同时开多个（存储 + 网络对比），内容随主窗口 refresh() 温和刷新（≥1s 一次）。
    """

    CATEGORY_TITLES = {"storage": "U 盘详情", "network": "网络详情"}

    def __init__(self, category: str, parent=None):
        super().__init__(parent)
        self._category = category
        self._lines: list[models.LogLine] = []      # 该类别全部原始日志行
        self._shown_lines: list[models.LogLine] = []  # 当前已渲染（过滤后）的行
        self._hint_visible = False
        self._colors = dict(LOG_TEXT_COLORS)
        self._search_text = ""

        self.setObjectName("detail_dialog")
        self.setWindowTitle(self.CATEGORY_TITLES.get(category, "详情"))
        self.resize(760, 620)
        self.setSizeGripEnabled(True)

        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        # ---------- 上半：结构化字段 ----------
        self.table = QtWidgets.QTableWidget(0, 2, self)
        self.table.setObjectName("detail_table")
        self.table.setHorizontalHeaderLabels(("字段", "值"))
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self.table.setAlternatingRowColors(True)
        self.table.setShowGrid(False)
        self.table.setWordWrap(False)
        self.table.verticalHeader().setVisible(False)
        self.table.verticalHeader().setDefaultSectionSize(24)
        self.table.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QtWidgets.QHeaderView.Stretch)
        self.table.horizontalHeader().setHighlightSections(False)
        root.addWidget(self.table)

        # ---------- 下半：原始日志 ----------
        bar = QtWidgets.QHBoxLayout()
        bar.setContentsMargins(0, 0, 0, 0)
        bar.setSpacing(8)
        self.search_edit = QtWidgets.QLineEdit()
        self.search_edit.setObjectName("detail_search_edit")
        self.search_edit.setPlaceholderText("在该类别的原始日志里搜索…")
        self.search_edit.setClearButtonEnabled(True)
        self.search_edit.textChanged.connect(self._on_search_changed)
        bar.addWidget(self.search_edit, 1)

        self.count_label = QtWidgets.QLabel("0 行")
        self.count_label.setObjectName("detail_count_label")
        self.count_label.setProperty("role", "hint")
        bar.addWidget(self.count_label)
        root.addLayout(bar)

        self.log_view = QtWidgets.QPlainTextEdit(self)
        self.log_view.setObjectName("detail_log_view")
        self.log_view.setReadOnly(True)
        self.log_view.setUndoRedoEnabled(False)
        self.log_view.setMaximumBlockCount(models.MAX_LOG_LINES)
        self.log_view.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)
        root.addWidget(self.log_view, 1)

        # ---------- 底部按钮 ----------
        buttons = QtWidgets.QHBoxLayout()
        buttons.setContentsMargins(0, 0, 0, 0)
        buttons.setSpacing(8)
        buttons.addStretch(1)

        self.copy_btn = QtWidgets.QPushButton("复制全部")
        self.copy_btn.setObjectName("detail_copy_btn")
        self.copy_btn.clicked.connect(self.copy_all)
        buttons.addWidget(self.copy_btn)

        self.close_btn = QtWidgets.QPushButton("关闭")
        self.close_btn.setObjectName("detail_close_btn")
        self.close_btn.clicked.connect(self.close)
        buttons.addWidget(self.close_btn)
        root.addLayout(buttons)

        self._empty_hint()

    # ---------- 内容 ----------

    def set_line_colors(self, colors: dict) -> None:
        self._colors.update(colors or {})
        self._render_lines(force=True)

    def update_data(self, fields, lines) -> None:
        """fields: [(键, 值)]；lines: [models.LogLine]。"""
        self._set_fields(fields)
        self._lines = list(lines or ())
        self._render_lines()

    def _set_fields(self, fields) -> None:
        fields = list(fields or ())
        self.table.setRowCount(len(fields))
        for r, (key, value) in enumerate(fields):
            key_item = QtWidgets.QTableWidgetItem(str(key))
            key_item.setForeground(QtGui.QColor(DETAIL_KEY_COLOR))     # 键列：次文字灰
            val_item = QtWidgets.QTableWidgetItem(str(value))
            val_item.setForeground(QtGui.QColor(DETAIL_VALUE_COLOR))   # 值列：主文字深
            self.table.setItem(r, 0, key_item)
            self.table.setItem(r, 1, val_item)
        # 高度贴合内容（7 行字段一屏看全，不留滚动条也不留大片空白）
        header_h = max(self.table.horizontalHeader().height(), 28)
        self.table.setFixedHeight(min(header_h + len(fields) * 24 + 6, 300))

    def _visible_lines(self):
        if not self._search_text:
            return self._lines
        out = []
        for ln in self._lines:
            hay = f"{ln.tag or ''} {ln.msg or ''} {ln.raw or ''}".lower()
            if self._search_text in hay:
                out.append(ln)
        return out

    def _render_lines(self, force: bool = False) -> None:
        shown = self._visible_lines()
        self.count_label.setText(f"{len(shown)} / {len(self._lines)} 行" if self._search_text
                                 else f"{len(shown)} 行")
        if not shown:
            self._empty_hint()
            self._shown_lines = []
            return
        # 增量：已渲染内容是新的前缀时只追加尾巴，避免每次全量重排
        have = self._shown_lines
        appendable = (not force and len(have) <= len(shown)
                      and all(a is b or a == b for a, b in zip(have, shown)))
        if appendable and self._hint_visible:
            appendable = False                       # 视图里还留着占位提示 → 重建
        if appendable:
            for line in shown[len(have):]:
                append_log_line(self.log_view, line, self._colors)
        else:
            self.log_view.setUpdatesEnabled(False)
            try:
                self.log_view.clear()
                for line in shown:
                    append_log_line(self.log_view, line, self._colors)
            finally:
                self.log_view.setUpdatesEnabled(True)
        self._hint_visible = False
        self._shown_lines = shown
        bar = self.log_view.verticalScrollBar()
        bar.setValue(bar.maximum())

    def _empty_hint(self) -> None:
        self.log_view.setPlainText(DETAIL_EMPTY_HINT)
        self._hint_visible = True

    def _on_search_changed(self, text: str) -> None:
        self._search_text = (text or "").strip().lower()
        self._render_lines(force=True)

    # ---------- 复制 ----------

    def copy_all(self) -> None:
        """把字段 + 原始日志整块拷进剪贴板（现场排查用）。"""
        parts = [self.windowTitle()]
        for r in range(self.table.rowCount()):
            key = self.table.item(r, 0)
            val = self.table.item(r, 1)
            if key is not None and val is not None:
                parts.append(f"{key.text()}: {val.text()}")
        parts.append("")
        parts.append(self.log_view.toPlainText())
        QtWidgets.QApplication.clipboard().setText("\n".join(parts))
        self.copy_btn.setText("已复制")
        QtCore.QTimer.singleShot(1200, lambda: self.copy_btn.setText("复制全部"))


class MainWindow(QtWidgets.QMainWindow):
    """一屏看全四个分区：尿液监测 / 存储U盘 / 设备与网络 / 记录表 + 日志。"""

    # 交互控件只发信号，动作由 main.py 实现
    connect_requested = QtCore.pyqtSignal()
    export_records_requested = QtCore.pyqtSignal()
    export_log_requested = QtCore.pyqtSignal()
    clear_requested = QtCore.pyqtSignal()        # 「清除」按钮：main.py 收到后清解析器状态

    def __init__(self, parser, parent=None):
        super().__init__(parent)
        self._parser = parser

        # 日志缓冲（受 models.MAX_LOG_LINES 约束，超出丢最旧）
        self._log_buffer: list[models.LogLine] = []
        self._log_shown = 0                 # 当前显示（过滤后）行数
        self._rx_total = 0                  # 累计接收行数
        self._level_letter: Optional[str] = None
        self._tag_group: Optional[str] = None
        self._search_text = ""
        self._log_colors = dict(LOG_TEXT_COLORS)
        self._hint_visible = False          # 日志区是否只有那行"等待串口数据…"提示
        self._connected = False
        # 「清除」后进入无数据态：四个分区一律 "--"，直到新的日志行到达才恢复渲染
        self._no_data = False
        self._detail_dialogs: dict[str, DetailDialog] = {}
        self._detail_timer = QtCore.QElapsedTimer()
        self._detail_timer.start()

        self.setObjectName("main_window")
        self.setWindowTitle("NIAIO 尿量监测上位机")
        self.resize(1240, 860)
        self.setMinimumSize(1040, 720)

        self._build_ui()

        self.set_connected(False)
        self.refresh_ports()
        self.append_log_placeholder_hint()
        self.refresh()

    # ========================================================
    # 构建界面
    # ========================================================

    def _build_ui(self) -> None:
        # style.qss 用 QWidget#central 铺应用底色，名字不能改
        central = QtWidgets.QWidget()
        central.setObjectName("central")
        self.setCentralWidget(central)

        root = QtWidgets.QVBoxLayout(central)
        root.setContentsMargins(10, 10, 10, 6)
        root.setSpacing(8)

        root.addWidget(self._build_top_bar())
        root.addWidget(self._build_top_cards())

        # 记录表 / 日志 可拖动分配高度
        self.main_splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)
        self.main_splitter.setObjectName("main_splitter")
        self.main_splitter.setChildrenCollapsible(False)
        self.main_splitter.addWidget(self._build_records_section())
        self.main_splitter.addWidget(self._build_log_section())
        self.main_splitter.setStretchFactor(0, 1)
        self.main_splitter.setStretchFactor(1, 1)
        self.main_splitter.setSizes([320, 400])
        root.addWidget(self.main_splitter, 1)

        self._build_status_bar()

    # ---------- 1. 顶部操作条（唯一可操作区） ----------
    def _build_top_bar(self) -> QtWidgets.QWidget:
        # style.qss: QWidget#conn_bar → 白底细边框面板
        bar = QtWidgets.QWidget()
        bar.setObjectName("conn_bar")
        bar.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Fixed)

        lay = QtWidgets.QHBoxLayout(bar)
        lay.setContentsMargins(10, 6, 10, 6)
        lay.setSpacing(8)

        self.port_label = QtWidgets.QLabel("串口")
        self.port_label.setObjectName("port_label")
        self.port_label.setProperty("role", "label")
        lay.addWidget(self.port_label)

        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setObjectName("port_combo")       # style.qss 有 #port_combo 规则
        self.port_combo.setMinimumWidth(180)
        self.port_combo.setSizeAdjustPolicy(QtWidgets.QComboBox.AdjustToContents)
        self.port_combo.setPlaceholderText("未发现串口")
        lay.addWidget(self.port_combo)

        # main.py 的提示语里提到"点刷新"，所以给一个本地刷新按钮（只重扫串口，不发信号）
        self.refresh_ports_btn = QtWidgets.QPushButton("刷新")
        self.refresh_ports_btn.setObjectName("refresh_ports_btn")
        self.refresh_ports_btn.clicked.connect(lambda: self.refresh_ports())
        lay.addWidget(self.refresh_ports_btn)

        self.baud_label = QtWidgets.QLabel("波特率")
        self.baud_label.setObjectName("baud_label")
        self.baud_label.setProperty("role", "label")
        lay.addWidget(self.baud_label)

        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.setObjectName("baud_combo")
        for baud in models.BAUD_CHOICES:
            self.baud_combo.addItem(str(baud), baud)
        idx = self.baud_combo.findData(models.DEFAULT_BAUD)
        self.baud_combo.setCurrentIndex(idx if idx >= 0 else 0)
        lay.addWidget(self.baud_combo)

        self.connect_btn = QtWidgets.QPushButton("连接")
        self.connect_btn.setObjectName("connect_btn")
        self.connect_btn.clicked.connect(lambda: self.connect_requested.emit())
        lay.addWidget(self.connect_btn)

        lay.addStretch(1)

        self.export_records_btn = QtWidgets.QPushButton("导出 U盘记录")
        self.export_records_btn.setObjectName("export_records_btn")
        self.export_records_btn.clicked.connect(lambda: self.export_records_requested.emit())
        lay.addWidget(self.export_records_btn)

        self.export_log_btn = QtWidgets.QPushButton("导出日志")
        self.export_log_btn.setObjectName("export_log_btn")
        self.export_log_btn.clicked.connect(lambda: self.export_log_requested.emit())
        lay.addWidget(self.export_log_btn)

        return bar

    # ---------- 2~4. 上部三张分区卡（等宽并排、高度固定） ----------
    def _build_top_cards(self) -> QtWidgets.QWidget:
        band = QtWidgets.QWidget()
        band.setObjectName("top_cards")
        # 高度固定：不随窗口拉伸吃掉日志区空间
        band.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Fixed)

        lay = QtWidgets.QHBoxLayout(band)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        lay.addWidget(self._build_urine_card(), 1)
        lay.addWidget(self._build_storage_card(), 1)
        lay.addWidget(self._build_network_card(), 1)
        return band

    def _make_card(self, obj_name: str, title: str) -> tuple[QtWidgets.QGroupBox, QtWidgets.QGridLayout]:
        """分区卡：用 QGroupBox（style.qss 的主骨架，标题条由 qss 画）。"""
        card = QtWidgets.QGroupBox(title)
        card.setObjectName(obj_name)
        grid = QtWidgets.QGridLayout(card)
        # 内边距交给 qss 的 QGroupBox padding，这里只留 0，避免双重留白
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(6)
        grid.setColumnStretch(1, 1)
        return card, grid

    def _make_field(self, grid: QtWidgets.QGridLayout, row: int, card: str, key: str,
                    caption: str, value_name: str, value_role: str) -> QtWidgets.QLabel:
        """一行字段：左边灰标签（role=label），右边数值/徽章。"""
        cap = QtWidgets.QLabel(caption)
        cap.setObjectName(f"{card}_{key}_caption")
        cap.setProperty("role", "label")

        val = QtWidgets.QLabel("--")
        val.setObjectName(value_name)               # 形如 total_ml_value / health_value
        val.setProperty("role", value_role)         # value / value-sm / status / hint
        val.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        val.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)

        grid.addWidget(cap, row, 0)
        if value_role == "status":
            # 徽章：只占自己文字宽度，靠右显示（药丸不能被拉成长条）
            val.setSizePolicy(QtWidgets.QSizePolicy.Maximum, QtWidgets.QSizePolicy.Preferred)
            grid.addWidget(val, row, 1, QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
        else:
            val.setMinimumWidth(96 if value_role == "value" else 84)   # 数字位数变化不抖
            grid.addWidget(val, row, 1)
        return val

    def _make_detail_button(self, grid: QtWidgets.QGridLayout, row: int, obj_name: str,
                            text: str, slot) -> QtWidgets.QToolButton:
        """卡片右上角的「详情」入口：QToolButton + autoRaise，视觉轻，不抢主信息。"""
        btn = QtWidgets.QToolButton()
        btn.setObjectName(obj_name)
        btn.setProperty("role", "link")          # style.qss 可按 role="link" 做扁平化
        btn.setText(text)
        btn.setToolButtonStyle(QtCore.Qt.ToolButtonTextOnly)
        btn.setAutoRaise(True)
        btn.setCursor(QtCore.Qt.PointingHandCursor)
        btn.setToolTip("查看结构化字段与该类别的原始日志")
        btn.clicked.connect(slot)
        grid.addWidget(btn, row, 1, QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
        return btn

    def _build_urine_card(self) -> QtWidgets.QGroupBox:
        card, grid = self._make_card("urine_card", "尿液监测")
        self.urine_total_value = self._make_field(
            grid, 1, "urine", "total", "累计尿量 (mL)", "total_ml_value", "value")
        self.urine_rate_value = self._make_field(
            grid, 2, "urine", "rate", "实时速率 (mL/h)", "rate_mlh_value", "value")
        self.urine_drops_value = self._make_field(
            grid, 3, "urine", "drops", "累计滴数", "drops_value", "value")
        self.urine_elapsed_value = self._make_field(
            grid, 4, "urine", "elapsed", "运行时长", "elapsed_value", "value")
        self.urine_state_value = self._make_field(
            grid, 5, "urine", "state", "运行状态", "state_value", "status")
        self.urine_alert_value = self._make_field(
            grid, 6, "urine", "alert", "报警", "alert_value", "status")
        grid.setRowStretch(7, 1)
        return card

    def _build_storage_card(self) -> QtWidgets.QGroupBox:
        card, grid = self._make_card("storage_card", "存储 / U盘")
        # 标题条右侧的「详情」入口（轻量按钮，见 style.qss 的 role="link"）
        self.storage_detail_btn = self._make_detail_button(
            grid, 0, "storage_detail_btn", "详情",
            lambda: self.show_detail("storage"))
        self.storage_ready_value = self._make_field(
            grid, 1, "storage", "ready", "U盘状态", "health_value", "status")
        self.storage_session_value = self._make_field(
            grid, 2, "storage", "session", "当前文件", "session_value", "value-sm")
        self.storage_lines_value = self._make_field(
            grid, 3, "storage", "lines", "已写入行数", "lines_written_value", "value-sm")
        self.storage_failures_value = self._make_field(
            grid, 4, "storage", "failures", "写盘失败", "write_failures_value", "value-sm")
        self.storage_result_value = self._make_field(
            grid, 5, "storage", "result", "最近结果", "storage_result_value", "value-sm")
        self.storage_error_value = self._make_field(
            grid, 6, "storage", "error", "失败详情", "storage_error_value", "hint")
        grid.setRowStretch(7, 1)
        return card

    def _build_network_card(self) -> QtWidgets.QGroupBox:
        card, grid = self._make_card("network_card", "设备与网络")
        self.network_detail_btn = self._make_detail_button(
            grid, 0, "network_detail_btn", "详情",
            lambda: self.show_detail("network"))
        self.network_device_value = self._make_field(
            grid, 1, "network", "device", "设备 ID", "device_id_value", "value-sm")
        self.network_ssid_value = self._make_field(
            grid, 2, "network", "ssid", "热点名", "ssid_value", "value-sm")
        self.network_ap_ip_value = self._make_field(
            grid, 3, "network", "ap_ip", "设备 IP", "ap_ip_value", "value-sm")
        self.network_phone_value = self._make_field(
            grid, 4, "network", "phone", "手机连接", "connected_value", "status")
        self.network_phone_ip_value = self._make_field(
            grid, 5, "network", "phone_ip", "手机 IP", "sta_ip_value", "value-sm")
        self.network_phone_mac_value = self._make_field(
            grid, 6, "network", "phone_mac", "手机 MAC", "sta_mac_value", "value-sm")
        grid.setRowStretch(7, 1)
        return card

    # ---------- 5. U 盘记录表 ----------
    def _build_records_section(self) -> QtWidgets.QGroupBox:
        box = QtWidgets.QGroupBox("U 盘记录")
        box.setObjectName("records_section")
        lay = QtWidgets.QVBoxLayout(box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)

        self.records_count_label = QtWidgets.QLabel("共 0 条")
        self.records_count_label.setObjectName("records_count_label")
        self.records_count_label.setProperty("role", "hint")     # 次要统计
        self.records_count_label.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
        lay.addWidget(self.records_count_label)

        self._records_model = RecordsTableModel(self)
        self._records_proxy = QtCore.QSortFilterProxyModel(self)
        self._records_proxy.setSourceModel(self._records_model)
        self._records_proxy.setSortRole(QtCore.Qt.UserRole)     # 让"分钟/尿量/流速"按数值排序

        self.records_table = QtWidgets.QTableView()
        self.records_table.setObjectName("records_table")
        self.records_table.setModel(self._records_proxy)
        self.records_table.setSortingEnabled(True)
        self.records_table.sortByColumn(0, QtCore.Qt.AscendingOrder)
        self.records_table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.records_table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self.records_table.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self.records_table.setAlternatingRowColors(True)
        self.records_table.setShowGrid(False)
        self.records_table.setWordWrap(False)                    # style.qss 接口约定第 2 条
        self.records_table.verticalHeader().setVisible(False)
        self.records_table.verticalHeader().setDefaultSectionSize(26)   # 同上：密而不挤
        self.records_table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        self.records_table.horizontalHeader().setHighlightSections(False)
        lay.addWidget(self.records_table, 1)
        return box

    # ---------- 6. 日志区 ----------
    def _build_log_section(self) -> QtWidgets.QGroupBox:
        box = QtWidgets.QGroupBox("日志")
        box.setObjectName("log_section")
        lay = QtWidgets.QVBoxLayout(box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)

        filters = QtWidgets.QHBoxLayout()
        filters.setContentsMargins(0, 0, 0, 0)
        filters.setSpacing(8)

        self.level_filter = QtWidgets.QComboBox()
        self.level_filter.setObjectName("level_filter")
        for text, value in LEVEL_FILTER_CHOICES:
            self.level_filter.addItem(text, value)
        self.level_filter.currentIndexChanged.connect(self._on_level_filter_changed)
        filters.addWidget(self.level_filter)

        self.tag_filter = QtWidgets.QComboBox()
        self.tag_filter.setObjectName("tag_filter")
        for text, value in TAG_FILTER_CHOICES:
            self.tag_filter.addItem(text, value)
        self.tag_filter.currentIndexChanged.connect(self._on_tag_filter_changed)
        filters.addWidget(self.tag_filter)

        self.search_edit = QtWidgets.QLineEdit()
        self.search_edit.setObjectName("search_edit")
        self.search_edit.setPlaceholderText("搜索日志…")
        self.search_edit.setClearButtonEnabled(True)
        self.search_edit.textChanged.connect(self._on_search_changed)
        filters.addWidget(self.search_edit, 1)

        self.log_count_label = QtWidgets.QLabel("0 / 0 行")
        self.log_count_label.setObjectName("log_count_label")
        self.log_count_label.setProperty("role", "hint")
        self.log_count_label.setToolTip(
            f"显示行数 / 缓冲行数（缓冲上限 {models.MAX_LOG_LINES} 行；"
            f"筛选切换时最多重绘 {MAX_RENDER_LINES} 行）"
        )
        filters.addWidget(self.log_count_label)

        self.clear_btn = QtWidgets.QPushButton("清除")
        self.clear_btn.setObjectName("clear_btn")
        self.clear_btn.setToolTip("清空日志区与 U 盘记录表，并把四个分区复位为未连接态")
        self.clear_btn.clicked.connect(self._on_clear_clicked)
        filters.addWidget(self.clear_btn)
        lay.addLayout(filters)

        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setObjectName("log_view")          # style.qss 有 #log_view 规则
        self.log_view.setReadOnly(True)                  # qss 用 [readOnly="true"] 命中
        self.log_view.setUndoRedoEnabled(False)
        self.log_view.setMaximumBlockCount(models.MAX_LOG_LINES)   # 超出自动丢最旧
        self.log_view.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)
        lay.addWidget(self.log_view, 1)
        return box

    # ---------- 7. 状态栏 ----------
    def _build_status_bar(self) -> None:
        sb = self.statusBar()
        sb.setObjectName("status_bar")
        sb.setSizeGripEnabled(True)

        self.status_label = QtWidgets.QLabel("就绪")
        self.status_label.setObjectName("status_label")
        sb.addWidget(self.status_label)

        self.rx_count_label = QtWidgets.QLabel("已接收 0 行")
        self.rx_count_label.setObjectName("rx_count_label")
        self.rx_count_label.setProperty("role", "value-sm")    # qss: QStatusBar QLabel[role=value-sm]
        sb.addPermanentWidget(self.rx_count_label)

    # ========================================================
    # 对外接口（main.py 调用）
    # ========================================================

    def append_log(self, line: models.LogLine) -> None:
        """追加一行日志（主线程调用）。超上限自动丢最旧。"""
        if self._hint_visible:                 # 第一条真实日志到达时抹掉占位提示
            self.log_view.clear()
            self._hint_visible = False

        # 新数据到达 → 退出「清除」后的无数据态，各分区恢复渲染
        self._no_data = False

        self._log_buffer.append(line)
        overflow = len(self._log_buffer) - models.MAX_LOG_LINES
        if overflow > 0:
            # 丢最旧的：其中"当前可见"的那些要同步减掉显示计数
            dropped = sum(1 for ln in self._log_buffer[:overflow] if self._matches(ln))
            del self._log_buffer[:overflow]
            self._log_shown = max(0, self._log_shown - dropped)

        self._rx_total += 1
        self.rx_count_label.setText(f"已接收 {self._rx_total} 行")

        if self._matches(line):
            self._render_line(line)
            self._log_shown += 1
        self._update_log_count()

    def refresh(self) -> None:
        """把 parser 的当前状态刷到各分区（主线程调用）。"""
        if self._no_data:
            # 「清除」过 / 已断开：保持未连接态，直到新的日志行到达（避免旧的 就绪/已连接 复活）
            self._refresh_detail_dialogs(force=False)
            return
        self._refresh_urine(self._parser.urine())
        self._refresh_storage(self._parser.storage())
        self._refresh_network(self._parser.network())
        self._sync_records(self._parser.records())
        self._refresh_detail_dialogs(force=False)

    def clear_all(self) -> None:
        """清空日志显示 + U盘记录表 + 把四个分区全部重置为未连接态（一律显示 "--"）。

        由 main.py 在断开串口时调用，也供界面上的「清除」按钮调用。
        注意：本方法不发 clear_requested（避免 main.py 的槽回调 clear_all 造成递归），
        但会尽力顺带调用 parser.reset()，保证界面复位后不会被残留的解析结果顶回来。
        """
        # 1) 日志区
        self._log_buffer.clear()
        self._log_shown = 0
        self._rx_total = 0
        self.log_view.clear()
        self._hint_visible = False
        self.append_log_placeholder_hint()
        self._update_log_count()
        self.rx_count_label.setText("已接收 0 行")

        # 2) U 盘记录表
        self._records_model.clear()
        self.records_count_label.setText("共 0 条")

        # 3) 四个分区复位
        self._reset_sections()
        self._no_data = True

        # 4) 解析器状态（parser.reset() 由 parser-dev 提供；没有也不能崩）
        reset = getattr(self._parser, "reset", None)
        if callable(reset):
            try:
                reset()
            except Exception:
                pass

        # 5) 打开着的详情窗也要同步变空
        self._refresh_detail_dialogs(force=True)

    def _reset_sections(self) -> None:
        """四个分区一律回到 "--"，徽章 state 清空（中性灰，不留绿色）。"""
        for label in (self.urine_total_value, self.urine_rate_value,
                      self.urine_drops_value, self.urine_elapsed_value,
                      self.urine_state_value, self.urine_alert_value,
                      self.storage_ready_value, self.storage_session_value,
                      self.storage_lines_value, self.storage_failures_value,
                      self.storage_result_value, self.storage_error_value,
                      self.network_device_value, self.network_ssid_value,
                      self.network_ap_ip_value, self.network_phone_value,
                      self.network_phone_ip_value, self.network_phone_mac_value):
            self._set_value(label, "--")
            self._set_state(label, None)

    def _on_clear_clicked(self) -> None:
        """「清除」按钮：先复位界面，再请 main.py 清掉解析器侧状态。"""
        self.clear_all()
        self.clear_requested.emit()

    # ---------- 详情弹窗 ----------

    def show_detail(self, category: str) -> DetailDialog:
        """打开（或聚焦）U 盘 / 网络详情窗；可同时开两个对比。"""
        category = "network" if str(category).lower().startswith("net") else "storage"
        dlg = self._detail_dialogs.get(category)
        if dlg is None:
            dlg = DetailDialog(category, self)
            dlg.set_line_colors(self._log_colors)
            self._detail_dialogs[category] = dlg
        fields, lines = self._detail_data(category)
        dlg.update_data(fields, lines)
        dlg.show()
        dlg.raise_()
        dlg.activateWindow()
        return dlg

    def _detail_data(self, category: str):
        """返回 (字段列表, 原始日志行列表)；无数据时字段值一律 "--"。"""
        lines = self._parser_lines_for(category)
        if category == "storage":
            st = models.StorageState() if self._no_data else self._parser.storage()
            fields = [
                ("U盘状态", st.health if not self._no_data else "--"),
                ("当前文件", st.session or "--"),
                ("已写入行数", "--" if self._no_data else str(st.lines_written)),
                ("写盘失败次数", "--" if self._no_data else str(st.write_failures)),
                ("最近结果", st.last_result or "--"),
                ("失败详情", st.last_error or "--"),
                ("最近检查", st.checked_at.strftime("%Y-%m-%d %H:%M:%S") if st.checked_at else "--"),
            ]
        else:
            st = models.NetworkState() if self._no_data else self._parser.network()
            if self._no_data:
                phone = "--"
            elif st.connected:
                phone = "已连接"
            elif st.sta_mac or st.sta_ip:
                phone = "未连接"
            else:
                phone = "--"
            fields = [
                ("设备 ID", st.device_id or "--"),
                ("热点 MAC", st.ap_mac or "--"),
                ("热点名", st.ssid or "--"),
                ("设备 IP", st.ap_ip or "--"),
                ("手机连接", phone),
                ("手机 MAC", st.sta_mac or "--"),
                ("手机 IP", st.sta_ip or "--"),
            ]
        return fields, lines

    def _parser_lines_for(self, category: str) -> list:
        """安全调用 parser.lines_for(类别)（parser-dev 并行开发，缺失也不崩）。"""
        fn = getattr(self._parser, "lines_for", None)
        if not callable(fn):
            return []
        try:
            return list(fn(category) or ())
        except Exception:
            return []

    def _refresh_detail_dialogs(self, force: bool = False) -> None:
        """详情窗随主界面温和刷新（默认 ≥1s 一次，避免每 200ms 重排文档）。"""
        if not self._detail_dialogs:
            return
        if not force and self._detail_timer.elapsed() < DETAIL_REFRESH_MS:
            return
        self._detail_timer.restart()
        for category, dlg in list(self._detail_dialogs.items()):
            if dlg.isVisible():
                dlg.update_data(*self._detail_data(category))

    def close_detail_dialogs(self) -> None:
        for dlg in list(self._detail_dialogs.values()):
            dlg.close()

    def set_status(self, text: str) -> None:
        """底部状态栏文字。"""
        self.status_label.setText(text)

    def set_connected(self, connected: bool) -> None:
        """切换连接按钮的文字与外观（外观靠动态属性，交给 style.qss）。"""
        self._connected = bool(connected)
        self.connect_btn.setText("断开" if self._connected else "连接")
        self.connect_btn.setProperty("connected", "true" if self._connected else "false")
        # 断开是破坏性动作，借 qss 已有的 [variant="danger"] 悬停规则
        self.connect_btn.setProperty("variant", "danger" if self._connected else "primary")
        self._repolish(self.connect_btn)

    def current_port(self) -> Optional[str]:
        """当前选中的串口名；没有可用串口时返回 None。"""
        idx = self.port_combo.currentIndex()
        if idx < 0:
            return None
        data = self.port_combo.itemData(idx)
        return data if data else None

    def current_baud(self) -> int:
        """当前选中的波特率。"""
        data = self.baud_combo.currentData()
        try:
            return int(data)
        except (TypeError, ValueError):
            return models.DEFAULT_BAUD

    # ---------- 附加便利接口 ----------

    def set_ports(self, ports: Sequence) -> None:
        """填充串口列表。可传 ["COM3"] 或 [("COM3", "USB-SERIAL CH340")]。"""
        keep = self.current_port()
        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        for item in ports or ():
            if isinstance(item, (tuple, list)):
                name = str(item[0]) if item else ""
                desc = str(item[1]) if len(item) > 1 and item[1] else ""
                self.port_combo.addItem(f"{name} — {desc}" if desc else name, name)
            else:
                name = str(item)
                self.port_combo.addItem(name, name)
        self.port_combo.blockSignals(False)

        if keep:
            idx = self.port_combo.findData(keep)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)
        if self.port_combo.currentIndex() < 0 and self.port_combo.count():
            self.port_combo.setCurrentIndex(0)

    def refresh_ports(self) -> None:
        """重新扫描系统串口（没装 pyserial 时保持空列表，不报错）。"""
        try:
            from serial.tools import list_ports
        except ImportError:
            return
        ports = []
        try:
            for p in list_ports.comports():
                ports.append((p.device, p.description or ""))
        except Exception:
            return
        self.set_ports(ports)

    def set_log_text_colors(self, colors: dict) -> None:
        """覆盖日志文本/行底配色（样式同学可运行期注入）；详情弹窗同步。"""
        self._log_colors.update(colors or {})
        self._rebuild_log_view()
        for dlg in self._detail_dialogs.values():
            dlg.set_line_colors(self._log_colors)

    def closeEvent(self, event) -> None:
        """主窗关闭时一并收掉详情窗，避免留下孤儿窗口。"""
        self.close_detail_dialogs()
        super().closeEvent(event)

    # ========================================================
    # 各分区刷新
    # ========================================================

    def _refresh_urine(self, st: models.UrineState) -> None:
        self._set_value(self.urine_total_value, self._fmt_float(st.total_ml, 1))
        self._set_value(self.urine_rate_value, self._fmt_float(st.rate_mlh, 1))
        self._set_value(self.urine_drops_value,
                        "--" if st.drops is None else str(int(st.drops)))
        self._set_value(self.urine_elapsed_value, st.elapsed or "--")

        state_text = models.STATE_MEANING.get(st.state, st.state) if st.state else "--"
        self._set_value(self.urine_state_value, state_text)
        self._set_state(self.urine_state_value, URINE_STATE_PROP.get(st.state or ""))

        if st.alert:
            alert_text = f"{st.alert} · {models.ALERT_MEANING.get(st.alert, '未知')}"
        else:
            alert_text = "--"
        self._set_value(self.urine_alert_value, alert_text)
        self._set_state(self.urine_alert_value, ALERT_STATE_PROP.get(st.alert or ""))

    def _refresh_storage(self, st: models.StorageState) -> None:
        self._set_value(self.storage_ready_value, st.health)
        if st.ready is None:
            self._set_state(self.storage_ready_value, None)         # 未探测 → 中性灰
        else:
            self._set_state(self.storage_ready_value, "ok" if st.ready else "error")

        self._set_value(self.storage_session_value, st.session or "--")

        # 计数器是 int（默认 0），但"设备根本没上报过存储信息"时显示 "--" 才诚实
        has_info = (st.ready is not None or st.session is not None
                    or st.last_result is not None
                    or bool(st.lines_written) or bool(st.write_failures))
        self._set_value(self.storage_lines_value,
                        f"{st.lines_written}" if has_info else "--")
        self._set_value(self.storage_failures_value,
                        f"{st.write_failures}" if has_info else "--")
        self._set_state(self.storage_failures_value,
                        "error" if st.write_failures else None)     # 有失败才标红

        if st.last_result is None:
            result_prop = None
        elif st.last_result_ok is None:
            result_prop = None
        else:
            result_prop = "ok" if st.last_result_ok else "error"
        self._set_value(self.storage_result_value, st.last_result or "--")
        self._set_state(self.storage_result_value, result_prop)

        self._set_value(self.storage_error_value, st.last_error or "--")

    def _refresh_network(self, st: models.NetworkState) -> None:
        self._set_value(self.network_device_value, st.device_id or "--")
        self._set_value(self.network_ssid_value, st.ssid or "--")
        self._set_value(self.network_ap_ip_value, st.ap_ip or "--")

        if st.connected:
            phone_text, phone_prop = "已连接", "ok"
        elif st.sta_mac or st.sta_ip:
            phone_text, phone_prop = "未连接", "idle"
        else:
            phone_text, phone_prop = "--", None     # 还没出现过手机信息，不谎报"未连接"
        self._set_value(self.network_phone_value, phone_text)
        self._set_state(self.network_phone_value, phone_prop)

        self._set_value(self.network_phone_ip_value, st.sta_ip or "--")
        self._set_value(self.network_phone_mac_value, st.sta_mac or "--")

    def _sync_records(self, rows) -> None:
        """与 parser 的记录表同步：只追加新增行，不重建全表。"""
        rows = list(rows or ())
        model = self._records_model
        shown = model.rowCount()

        if shown and shown <= len(rows):
            last_local = model.row_at(shown - 1)
            last_remote = rows[shown - 1]
            if (last_local.minute, last_local.time_hms) != (last_remote.minute, last_remote.time_hms):
                shown = -1        # 前缀对不上（换了会话/被清空）→ 全量重置

        if shown < 0 or len(rows) < shown:
            model.set_rows(rows)
        elif len(rows) > shown:
            model.append_rows(rows[shown:])

        self.records_count_label.setText(f"共 {model.rowCount()} 条")

    # ========================================================
    # 日志区
    # ========================================================

    def _matches(self, line: models.LogLine) -> bool:
        if self._level_letter is not None:
            if line.level is None or line.level.value != self._level_letter:
                return False
        if self._tag_group is not None:
            if models.tag_group(line.tag or "") != self._tag_group:
                return False
        if self._search_text:
            haystack = f"{line.tag or ''} {line.msg or ''} {line.raw or ''}".lower()
            if self._search_text not in haystack:
                return False
        return True

    def _log_html(self, line: models.LogLine) -> str:
        """一行日志的富文本（实现见模块级 log_line_html，详情弹窗共用同一套配色）。"""
        return log_line_html(line, self._log_colors)

    def _line_bg(self, line: models.LogLine) -> Optional[str]:
        """错误/警告行的整行底色（QSS 管不到文档块，见文件头配合说明）。"""
        return log_line_bg(line, self._log_colors)

    def _render_line(self, line: models.LogLine) -> None:
        append_log_line(self.log_view, line, self._log_colors)

    def _rebuild_log_view(self) -> None:
        matched = [ln for ln in self._log_buffer if self._matches(ln)]
        tail = matched[-MAX_RENDER_LINES:]      # 一次重绘的行数上限，防止大缓冲卡界面
        self._log_shown = len(tail)

        self.log_view.setUpdatesEnabled(False)
        try:
            self.log_view.clear()
            self._hint_visible = False
            for line in tail:
                self._render_line(line)
            if not tail and not self._log_buffer:
                self.append_log_placeholder_hint()      # 一条日志都没有时才放提示
            self.log_view.verticalScrollBar().setValue(
                self.log_view.verticalScrollBar().maximum()
            )
        finally:
            self.log_view.setUpdatesEnabled(True)
        self._update_log_count()

    def _update_log_count(self) -> None:
        self.log_count_label.setText(f"{self._log_shown} / {len(self._log_buffer)} 行")

    def _on_level_filter_changed(self, index: int) -> None:
        self._level_letter = self.level_filter.itemData(index)
        self._rebuild_log_view()

    def _on_tag_filter_changed(self, index: int) -> None:
        self._tag_group = self.tag_filter.itemData(index)
        self._rebuild_log_view()

    def _on_search_changed(self, text: str) -> None:
        self._search_text = (text or "").strip().lower()
        self._rebuild_log_view()

    def append_log_placeholder_hint(self) -> None:
        """启动时在日志区放一行提示（不是日志，不占用接收计数）。"""
        self.log_view.appendPlainText("等待串口数据…（选择串口后点「连接」）")
        self._hint_visible = True

    # ========================================================
    # 小工具
    # ========================================================

    @staticmethod
    def _fmt_float(value, ndigits: int = 1) -> str:
        if value is None:
            return "--"
        try:
            return f"{float(value):.{ndigits}f}"
        except (TypeError, ValueError):
            return "--"

    def _set_value(self, label: QtWidgets.QLabel, text: str) -> None:
        if label.text() != text:
            label.setText(text)

    def _set_state(self, label: QtWidgets.QLabel, state: Optional[str]) -> None:
        """设/清 state 动态属性（None = 清掉 → style.qss 保持中性灰）。"""
        if label.property("state") != state:
            label.setProperty("state", state)
            self._repolish(label)

    @staticmethod
    def _repolish(widget: QtWidgets.QWidget) -> None:
        """动态属性变化后让 qss 重新生效。"""
        style = widget.style()
        style.unpolish(widget)
        style.polish(widget)
        widget.update()


# ============================================================
# mock 数据源（仅供 --mock / --selftest，不参与真实运行）
# ============================================================

class MockParser:
    """造假数据用的替身 parser，接口与 parser.LogParser 的读取方法一致。

    额外实现 reset() / lines_for(类别)，用于在没有 parser-dev 的成果时也能自检
    详情弹窗与「清除」流程。
    """

    def __init__(self, empty: bool = False):
        self._empty = empty
        self.reset_calls = 0

    def reset(self) -> None:
        """与真实 parser 一致：清掉已解析状态（这里就变成"什么都没收到"）。"""
        self.reset_calls += 1
        self._empty = True

    def lines_for(self, category: str) -> list:
        """某个类别的原始日志行（详情弹窗下半部分用）。"""
        if self._empty:
            return []
        return mock_category_lines(category)

    def urine(self) -> models.UrineState:
        if self._empty:
            return models.UrineState()
        return models.UrineState(
            total_ml=123.4, rate_mlh=96.0, drops=4821,
            elapsed="01:23:45", state="start", alert="A",
            clock_hms="20:29:41", battery="85%",
        )

    def storage(self) -> models.StorageState:
        if self._empty:
            return models.StorageState()
        return models.StorageState(
            ready=True, session="U0000001", lines_written=1234, write_failures=2,
            last_result="写入成功", last_result_ok=True,
            last_error=None, checked_at=datetime.now(),
        )

    def network(self) -> models.NetworkState:
        if self._empty:
            return models.NetworkState(connected=False)
        return models.NetworkState(
            ap_mac="44:B1:76:C5:15:B5", device_id="NIAIO-C515B5", ap_ip="192.168.4.1",
            ssid="NIAIO-Monitor", sta_mac="A4:C1:38:9F:2E:71", sta_ip="192.168.4.2",
            connected=True,
        )

    def records(self) -> list:
        if self._empty:
            return []
        base = datetime(2026, 10, 12, 20, 25, 0)
        raw = [
            (1, "00:01:00", "1.6ml", "96.0ml/h"),
            (2, "00:02:00", "1.9ml", "114.0ml/h"),
            (3, "00:03:00", "0.0ml", "0.0ml/h"),
            (4, "00:04:00", "2.4ml", "144.0ml/h"),
            (5, "00:05:00", "1.2ml", "72.0ml/h"),
        ]
        return [
            models.RecordRow(minute=m, time_hms=t, ml=ml, rate=r,
                             received_at=base + timedelta(minutes=m), session="U0000001")
            for m, t, ml, r in raw
        ]


def mock_category_lines(category: str, now: Optional[datetime] = None) -> list:
    """某个类别的假原始日志（详情弹窗用）：U盘=CH376 初始化码，网络=WiFi 固件/PHY/DHCP。"""
    now = now or datetime(2026, 10, 12, 20, 28, 30)
    table = {
        "storage": [
            ("I", "storage", "CH376 init: cmd=0x06 (CHECK_EXIST) ret=0x14 (USB_DEV_READY)"),
            ("I", "storage", "CH376 set mode 0x06 (host+SD) ret=0x51"),
            ("I", "storage", "CH376 disk mount ret=0x14, disk_ready=1"),
            ("I", "storage", "CH376 disk query ret=0x14, size=7.4GB free=6.9GB"),
            ("I", "storage", "CH376 open file U0000001.TXT ret=0x14"),
            ("I", "storage", "CH376 write 252 bytes ret=0x14 (cmd=0x2C)"),
            ("W", "storage", "write retry 1/3 (stage=fwrite err=0x103)"),
            ("E", "storage", "write failed stage=fwrite err=0x103 (disk full?)"),
            ("I", "storage", "CH376 close file ret=0x14, dir_update ok"),
            ("I", "storage", "进入临界区(测量文件已存在，保留原内容，当前 252 字节)"),
            ("I", "storage", "第5分钟已写入U盘: /U0000001.TXT"),
            ("E", "storage", "检测到U盘已拔出 (disk_ready=0)"),
        ],
        "network": [
            ("I", "phy_init", "phy_version 4780, hash a3f4e2b, compiled Oct 10 2026 15:22:08"),
            ("I", "wifi_init", "rx ba win: 6, tx ba win: 6, ampdu 1"),
            ("I", "wifi", "firmware version: 5.1.2.0 (ESP32-S3, coex)"),
            ("I", "wifi", "mode : sta (44:b1:76:c5:15:b5) + softAP"),
            ("I", "wifi", "softAP ssid NIAIO-Monitor, channel 1, auth WPA2_PSK"),
            ("I", "esp_netif_lwip", "softAP ip 192.168.4.1, mask 255.255.255.0"),
            ("I", "esp_netif", "DHCP server started: pool 192.168.4.2-192.168.4.11, lease 7200s"),
            ("I", "mobile", "station join mac a4:c1:38:9f:2e:71, aid=1"),
            ("I", "esp_netif", "sta ip 192.168.4.2, gw 192.168.4.1, dns 192.168.4.1"),
            ("I", "mobile", "手机已连接 RSSI -52dBm, 通道占用 11%"),
            ("W", "wifi", "station leave, reason=8 (ASSOC_LEAVE), 等待重新加入"),
            ("D", "httpd_ws", "ws handshake ok, fd=57, 订阅 状态/尿量 两个频道"),
        ],
    }
    raw = table.get(category, [])
    lines = []
    for i, (level, tag, msg) in enumerate(raw):
        lines.append(models.LogLine(
            ts_ms=900 + i * 260, level=models.Level(level), tag=tag, msg=msg,
            raw=f"{level} ({900 + i * 260}) {tag}: {msg}",
            received_at=now + timedelta(seconds=i * 2),
        ))
    return lines


def mock_log_lines(now: Optional[datetime] = None) -> list:
    """20 行混合级别的假日志（看起来真实，但明显是假的）。"""
    now = now or datetime(2026, 10, 12, 20, 29, 0)
    raw = [
        ("I", "boot", "ESP-IDF v5.1.2 2nd stage bootloader"),
        ("I", "cpu_start", "Pro cpu up, 2 core(s)"),
        ("I", "nvs_flash", "nvs flash init ok, 3 namespace(s)"),
        ("I", "wifi_init", "rx ba win: 6, tx ba win: 6"),
        ("I", "wifi", "mode : sta (44:b1:76:c5:15:b5) + softAP"),
        ("I", "wifi", "softAP ssid NIAIO-Monitor, channel 1"),
        ("I", "esp_netif_lwip", "softAP ip 192.168.4.1, mask 255.255.255.0"),
        ("I", "main", "NIAIO urine monitor boot ok, device NIAIO-C515B5"),
        ("I", "storage", "mount U disk ok, session U0000001"),
        ("I", "walltime", "wall clock 2026-10-12 20:29:41"),
        ("W", "storage", "write retry 1/3 (stage=fwrite err=0x103)"),
        ("E", "storage", "write failed stage=fwrite err=0x103 (disk full?)"),
        ("I", "storage", "write recovered, session U0000001 continues"),
        ("I", "urine", "minute 1 ml=1.6ml rate=96.0ml/h"),
        ("I", "screen", "data.t0=1.6 t1=96.0 t2=140 t3=00:01:00"),
        ("I", "monitor", "total 3.2ml elapsed 00:02:00 state=start"),
        ("W", "monitor", "alert C 少尿, rate 72.0ml/h below 80"),
        ("I", "mobile", "station join ip 192.168.4.2 mac a4:c1:38:9f:2e:71"),
        ("D", "httpd_ws", "handshake ok, fd=57"),
        ("I", "main", "heartbeat, free heap 218KB"),
    ]
    lines = []
    for i, (level, tag, msg) in enumerate(raw):
        lines.append(models.LogLine(
            ts_ms=1200 + i * 430, level=models.Level(level), tag=tag, msg=msg,
            raw=f"{level} ({1200 + i * 430}) {tag}: {msg}",
            received_at=now + timedelta(seconds=i * 3),
        ))
    return lines


# ============================================================
# 独立启动 / 自检
# ============================================================

def _fill_mock(window: MainWindow, empty: bool = False) -> None:
    if not empty:
        for line in mock_log_lines():
            window.append_log(line)
    window.refresh()


def _normalize_category(text) -> str:
    """把「存储 / U盘 / storage」和「网络 / wifi / network」之类的写法归一。"""
    t = str(text).strip().lower()
    if t.startswith("net") or "网络" in t or "wifi" in t or "无线" in t:
        return "network"
    return "storage"


def _apply_local_qss(win: MainWindow) -> bool:
    """截图自检时套上同目录的 style.qss（只读，与 main.py 的做法一致），便于看真实观感。"""
    qss_path = Path(__file__).with_name("style.qss")
    if not qss_path.exists():
        return False
    try:
        win.setStyleSheet(qss_path.read_text(encoding="utf-8"))
        return True
    except Exception:
        return False


def _selftest(argv: Optional[list] = None) -> int:
    """启动 → 渲染 → 1.5 秒后自动关闭，并检查关键控件与 "--" 占位。"""
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(argv or sys.argv[:1])
    win = MainWindow(MockParser(empty=False))
    _fill_mock(win, empty=False)
    win.set_status("自检中：NIAIO-C515B5 @ COM7，已接收 20 行")
    win.set_connected(True)
    win.show()

    failures: list[str] = []

    def check() -> None:
        required = (
            "port_combo", "baud_combo", "connect_btn", "export_records_btn", "export_log_btn",
            "urine_card", "storage_card", "network_card", "records_section", "log_section",
            "total_ml_value", "rate_mlh_value", "drops_value", "elapsed_value",
            "state_value", "alert_value",
            "health_value", "session_value", "lines_written_value", "write_failures_value",
            "storage_result_value", "storage_error_value",
            "device_id_value", "ssid_value", "ap_ip_value",
            "connected_value", "sta_ip_value", "sta_mac_value",
            "records_table", "records_count_label",
            "level_filter", "tag_filter", "search_edit", "log_view", "log_count_label",
            "status_bar", "status_label", "rx_count_label",
            # 迭代2 新增
            "clear_btn", "storage_detail_btn", "network_detail_btn",
        )
        for name in required:
            if win.findChild(QtWidgets.QWidget, name) is None:
                failures.append(f"缺少 objectName: {name}")

        checks = {
            "尿量": win.urine_total_value.text(),
            "速率": win.urine_rate_value.text(),
            "设备ID": win.network_device_value.text(),
            "手机IP": win.network_phone_ip_value.text(),
            "U盘状态": win.storage_ready_value.text(),
            "记录条数": win.records_count_label.text(),
            "日志计数": win.log_count_label.text(),
            "运行状态": win.urine_state_value.text(),
            "报警": win.urine_alert_value.text(),
        }
        for k, v in checks.items():
            print(f"  {k}: {v}")
        if win.urine_total_value.text() != "123.4":
            failures.append(f"尿量显示异常：{win.urine_total_value.text()}")
        if win.network_device_value.text() != "NIAIO-C515B5":
            failures.append("设备 ID 显示异常")
        if win.urine_state_value.text() != "测量中":
            failures.append("状态字映射异常")
        if win.records_count_label.text() != "共 5 条":
            failures.append(f"记录计数异常：{win.records_count_label.text()}")
        if win.log_view.toPlainText().count("\n") < 19:
            failures.append("日志渲染行数不足")
        if win.connect_btn.text() != "断开":
            failures.append("set_connected 未切换按钮文字")

        # style.qss 约定：role / state 必须按契约设置
        for name, role in (("total_ml_value", "value"), ("state_value", "status"),
                           ("health_value", "status"), ("connected_value", "status"),
                           ("session_value", "value-sm"), ("storage_error_value", "hint")):
            w = win.findChild(QtWidgets.QLabel, name)
            if w is None or w.property("role") != role:
                failures.append(f"{name} 的 role 应为 {role}")
        for name, state in (("state_value", "run"), ("alert_value", "ok"),
                            ("health_value", "ok"), ("connected_value", "ok")):
            w = win.findChild(QtWidgets.QLabel, name)
            if w is None or w.property("state") != state:
                failures.append(f"{name} 的 state 应为 {state}")
        print(f"  徽章 role/state: state_value={win.urine_state_value.property('state')} "
              f"alert_value={win.urine_alert_value.property('state')} "
              f"health_value={win.storage_ready_value.property('state')} "
              f"connected_value={win.network_phone_value.property('state')}")

        # 空数据窗口：检查 None → "--"，且状态徽章不设色（中性灰）
        empty_win = MainWindow(MockParser(empty=True))
        empty_win.refresh()
        for name, label in (
            ("尿量", empty_win.urine_total_value),
            ("速率", empty_win.urine_rate_value),
            ("运行时长", empty_win.urine_elapsed_value),
            ("当前文件", empty_win.storage_session_value),
            ("最近结果", empty_win.storage_result_value),
            ("设备ID", empty_win.network_device_value),
            ("手机IP", empty_win.network_phone_ip_value),
            ("手机MAC", empty_win.network_phone_mac_value),
        ):
            if label.text() != "--":
                failures.append(f"空数据未显示 --：{name} = {label.text()!r}")
        for name, label in (("state_value", empty_win.urine_state_value),
                            ("alert_value", empty_win.urine_alert_value),
                            ("health_value", empty_win.storage_ready_value),
                            ("connected_value", empty_win.network_phone_value)):
            if label.property("state") is not None:
                failures.append(f"空数据不应设 state：{name}")
        print(f"  空数据 U盘状态: {empty_win.storage_ready_value.text()} "
              f"(state={empty_win.storage_ready_value.property('state')})")
        print(f"  空数据 手机连接: {empty_win.network_phone_value.text()} "
              f"(state={empty_win.network_phone_value.property('state')})")
        empty_win.close()
        empty_win.deleteLater()

        # 筛选功能
        win.level_filter.setCurrentIndex(1)       # 仅错误
        err_lines = win.log_view.toPlainText().strip().splitlines()
        if len(err_lines) != 1:
            failures.append(f"级别筛选异常：错误档显示 {len(err_lines)} 行")
        win.level_filter.setCurrentIndex(0)
        win.search_edit.setText("storage")
        n = len(win.log_view.toPlainText().strip().splitlines())
        print(f"  搜索 storage: {n} 行")
        if n < 3:
            failures.append("搜索筛选异常")
        win.search_edit.clear()
        win.tag_filter.setCurrentIndex(2)         # 仅系统
        n = len(win.log_view.toPlainText().strip().splitlines())
        print(f"  仅系统 TAG: {n} 行")
        if n >= 20:
            failures.append("TAG 筛选未生效")
        win.tag_filter.setCurrentIndex(0)

        # ---- 迭代2：详情弹窗（结构化字段 + 该类别的原始日志） ----
        dlg = win.show_detail("storage")
        fields_n = dlg.table.rowCount()
        body = dlg.log_view.toPlainText()
        lines_n = len(body.strip().splitlines())
        print(f"  存储详情: 字段 {fields_n} 行 / 原始日志 {lines_n} 行 "
              f"| 字段表 objectName={dlg.table.objectName()} 日志 objectName={dlg.log_view.objectName()}")
        if fields_n < 7:
            failures.append(f"存储详情字段太少：{fields_n}")
        if lines_n < 10 or "CH376" not in body:
            failures.append("存储详情缺少原始日志/CH376 初始化码")
        dlg.search_edit.setText("CH376")
        n_hit = len(dlg.log_view.toPlainText().strip().splitlines())
        print(f"  存储详情里搜索 CH376: {n_hit} 行")
        if n_hit >= lines_n or n_hit == 0:
            failures.append("详情弹窗搜索无效")
        dlg.search_edit.clear()
        dlg.copy_all()
        copied = QtWidgets.QApplication.clipboard().text()
        if "CH376" not in copied:
            failures.append("详情弹窗「复制全部」内容异常")

        net_dlg = win.show_detail("network")
        net_body = net_dlg.log_view.toPlainText()
        print(f"  网络详情: 字段 {net_dlg.table.rowCount()} 行 / 原始日志 "
              f"{len(net_body.strip().splitlines())} 行 | 含固件版本: {'firmware version' in net_body}")
        if "firmware version" not in net_body or "phy_version" not in net_body:
            failures.append("网络详情缺少 WiFi 固件/PHY 版本")
        if dlg.isVisible() and net_dlg.isVisible():
            pass          # 两个详情窗可同时打开（非模态）
        dlg.close()
        net_dlg.close()

        # 无数据时详情窗不能空白
        empty_win2 = MainWindow(MockParser(empty=True))
        empty_win2.refresh()
        empty_dlg = empty_win2.show_detail("storage")
        if DETAIL_EMPTY_HINT not in empty_dlg.log_view.toPlainText():
            failures.append("无数据时详情窗未显示占位提示")
        print(f"  空数据详情窗: {empty_dlg.log_view.toPlainText()!r}")
        empty_dlg.close()
        empty_win2.close()
        empty_win2.deleteLater()

        if failures:
            print("FAIL")
            for f in failures:
                print("  -", f)
            app.exit(1)
        else:
            print("OK")
            app.exit(0)

    QtCore.QTimer.singleShot(1500, check)
    return app.exec_()


class _StubbornParser(MockParser):
    """不理会 reset() 的坏 parser —— 用来证明界面复位后不会被残留解析结果顶回来。"""

    def reset(self) -> None:
        self.reset_calls += 1        # 假装被调用了，但状态一个字都不清


def _clear_assertions(win: MainWindow, parser) -> list:
    """clear_all() 之后必须成立的全部断言（--selftest-clear 用）。"""
    fails: list[str] = []

    for name, label in (
        ("累计尿量", win.urine_total_value), ("实时速率", win.urine_rate_value),
        ("累计滴数", win.urine_drops_value), ("运行时长", win.urine_elapsed_value),
        ("运行状态", win.urine_state_value), ("报警", win.urine_alert_value),
        ("U盘状态", win.storage_ready_value), ("当前文件", win.storage_session_value),
        ("已写入行数", win.storage_lines_value), ("写盘失败", win.storage_failures_value),
        ("最近结果", win.storage_result_value), ("失败详情", win.storage_error_value),
        ("设备 ID", win.network_device_value), ("热点名", win.network_ssid_value),
        ("设备 IP", win.network_ap_ip_value), ("手机连接", win.network_phone_value),
        ("手机 IP", win.network_phone_ip_value), ("手机 MAC", win.network_phone_mac_value),
    ):
        if label.text() != "--":
            fails.append(f"{name} 未复位：{label.text()!r}")

    for name, label in (("运行状态", win.urine_state_value), ("报警", win.urine_alert_value),
                        ("U盘状态", win.storage_ready_value),
                        ("写盘失败", win.storage_failures_value),
                        ("最近结果", win.storage_result_value),
                        ("手机连接", win.network_phone_value)):
        if label.property("state") is not None:
            fails.append(f"{name} 的 state 未清空：{label.property('state')!r}")

    if win.log_count_label.text() != "0 / 0 行":
        fails.append(f"日志计数未归零：{win.log_count_label.text()!r}")
    if win._records_model.rowCount() != 0 or win.records_count_label.text() != "共 0 条":
        fails.append(f"记录表未清空：{win.records_count_label.text()!r}")
    if win.rx_count_label.text() != "已接收 0 行":
        fails.append(f"接收计数未归零：{win.rx_count_label.text()!r}")
    if len(win._log_buffer) != 0:
        fails.append(f"日志缓冲未清空：{len(win._log_buffer)} 行")
    leftover = [ln for ln in win.log_view.toPlainText().strip().splitlines()
                if ln.strip() and "等待串口数据" not in ln]
    if leftover:
        fails.append(f"日志区仍有残留行：{leftover[:2]}")
    if not callable(getattr(parser, "reset", None)):
        fails.append("parser 没有 reset()")
    return fails


def _selftest_clear(argv: Optional[list] = None, screenshot: Optional[str] = None) -> int:
    """--mock --selftest-clear：填数据 → clear_all() → 断言分区全 "--"、日志/记录归零。"""
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(argv or sys.argv[:1])
    failures: list[str] = []

    win = MainWindow(MockParser())
    _fill_mock(win, empty=False)
    win.set_connected(True)
    _apply_local_qss(win)
    win.show()
    before = (win.urine_total_value.text(), win.storage_ready_value.text(),
              win.network_phone_value.text(), win.records_count_label.text(),
              win.log_count_label.text())
    print(f"  清除前: 尿量 {before[0]} | U盘 {before[1]} | 手机 {before[2]} "
          f"| {before[3]} | 日志 {before[4]}")
    win.clear_all()
    failures += _clear_assertions(win, win._parser)
    print(f"  清除后: 尿量 {win.urine_total_value.text()} "
          f"| U盘 {win.storage_ready_value.text()}(state={win.storage_ready_value.property('state')}) "
          f"| 手机 {win.network_phone_value.text()}(state={win.network_phone_value.property('state')}) "
          f"| {win.records_count_label.text()} | 日志 {win.log_count_label.text()} "
          f"| {win.rx_count_label.text()}")

    # 回归①：清除后 main.py 的 200ms 定时刷新不得把旧数据顶回来
    for _ in range(5):
        win.refresh()
    if (win.urine_total_value.text(), win.storage_ready_value.text(),
            win.network_phone_value.text()) != ("--", "--", "--"):
        failures.append(f"清除后 refresh() 又把旧数据顶回来了："
                        f"{win.urine_total_value.text()}/{win.storage_ready_value.text()}/"
                        f"{win.network_phone_value.text()}")
    print(f"  清除后连续 refresh() 5 次: 尿量 {win.urine_total_value.text()} "
          f"| U盘 {win.storage_ready_value.text()} | 手机 {win.network_phone_value.text()}")

    # 详情窗同步变空
    dlg = win.show_detail("storage")
    if DETAIL_EMPTY_HINT not in dlg.log_view.toPlainText():
        failures.append("清除后详情窗仍有旧日志")
    print(f"  清除后存储详情: {dlg.log_view.toPlainText()!r}")
    dlg.close()

    # 新数据到达后必须恢复渲染
    win.append_log(models.LogLine(ts_ms=1, level=models.Level.INFO, tag="storage",
                                  msg="new session start", raw="I (1) storage: new session start",
                                  received_at=datetime(2026, 10, 12, 21, 0, 0)))
    win.refresh()
    if win._no_data:
        failures.append("新日志到达后仍停在无数据态")
    print(f"  新日志到达后: 日志 {win.log_count_label.text()} | 接收 {win.rx_count_label.text()}")

    # 回归②：即使 parser 不理会 reset()，界面也必须保持复位
    stubborn = MainWindow(_StubbornParser())
    _fill_mock(stubborn, empty=False)
    stubborn.show()
    stubborn.clear_all()
    for _ in range(5):
        stubborn.refresh()
    fails2 = _clear_assertions(stubborn, stubborn._parser)
    if fails2:
        failures += [f"[坏parser场景] {f}" for f in fails2]
    print(f"  parser 拒绝复位时: 尿量 {stubborn.urine_total_value.text()} "
          f"| U盘 {stubborn.storage_ready_value.text()} | 手机 {stubborn.network_phone_value.text()}")
    stubborn.close()

    if screenshot:
        win.show_detail("storage").close()          # 清掉可能残留的详情窗
        ok = win.grab().save(screenshot)
        print(f"  清除后窗口截图: {'saved' if ok else 'FAILED'} {screenshot}")

    win.close()
    if failures:
        print("FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print("CLEAR OK")
    return 0


def main(argv: Optional[list] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="ui_main.py",
        description="serial-monitor 界面自检（真实运行请用 main.py）",
    )
    ap.add_argument("--mock", action="store_true", help="用假数据启动窗口，便于无串口检查布局")
    ap.add_argument("--empty", action="store_true", help="假数据全部为空，检查 \"--\" 占位")
    ap.add_argument("--selftest", action="store_true", help="启动后 1.5 秒自动关闭并打印 OK")
    ap.add_argument("--selftest-clear", action="store_true",
                    help="填假数据 → clear_all() → 断言四个分区为 \"--\"、日志与记录归零")
    ap.add_argument("--screenshot", metavar="PNG路径", help="渲染后保存窗口截图再退出")
    ap.add_argument("--screenshot-detail", nargs=2, metavar=("类别", "PNG路径"),
                    help="打开存储/网络详情窗并截图，例如：--screenshot-detail 存储 out.png")
    args = ap.parse_args(argv)

    if not (args.mock or args.selftest or args.selftest_clear or args.screenshot
            or args.screenshot_detail):
        ap.print_help()
        return 2

    if args.selftest:
        return _selftest()

    if args.selftest_clear:
        return _selftest_clear(screenshot=args.screenshot)

    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv[:1])
    win = MainWindow(MockParser(empty=args.empty))
    _fill_mock(win, empty=args.empty)
    win.set_status("示例数据（--mock）：不连接任何串口")
    print(f"style.qss 已应用: {_apply_local_qss(win)}")
    win.show()

    if args.screenshot_detail:
        category = _normalize_category(args.screenshot_detail[0])
        path = args.screenshot_detail[1]

        def shoot_detail() -> None:
            dlg = win.show_detail(category)
            body = dlg.log_view.toPlainText()
            print(f"detail({category}): 字段 {dlg.table.rowCount()} 行 / 原始日志 "
                  f"{len(body.strip().splitlines())} 行")
            ok = dlg.grab().save(path)
            print(f"screenshot {'saved' if ok else 'FAILED'}: {path}")
            app.exit(0 if ok else 1)

        QtCore.QTimer.singleShot(1500, shoot_detail)

    if args.screenshot:
        def shoot() -> None:
            ok = win.grab().save(args.screenshot)
            print(f"screenshot {'saved' if ok else 'FAILED'}: {args.screenshot}")
            app.exit(0 if ok else 1)
        QtCore.QTimer.singleShot(1200, shoot)

    return app.exec_()


if __name__ == "__main__":
    # python ui_main.py --mock  → 用假数据启动窗口，便于无串口时检查布局
    sys.exit(main())
