"""
serial-monitor 主程序 —— 尿量计串口上位机

职责（Lead 负责的集成层）：
  ① 串口读取线程：只收字节、切行、把【原始文本行】发回主线程
     （解析在主线程做：feed_line 是纯正则，微秒级，且天然避免 parser 的并发问题）
  ② 窗口组装：把 ui_main.MainWindow 与 style.qss 接起来
  ③ 导出：U 盘记录 → CSV；原始日志 → 文本
  ④ --replay：把日志文件当串口回放，用于没有硬件时的验收

设计原则：本文件不解析日志（那是 parser.py 的事），不画界面（那是 ui_main.py 的事），
只做"接线"与"动作"。
"""

from __future__ import annotations

import argparse
import csv
import sys
from datetime import datetime
from pathlib import Path

from PyQt5 import QtCore, QtWidgets

import models
from models import LogLine, Level
from parser import LogParser

# 串口相关放在 try 里：没有 pyserial 时给出可读的提示，而不是 ImportError 堆栈
try:
    import serial
    from serial.tools import list_ports
    HAS_SERIAL = True
except ImportError:  # pragma: no cover
    HAS_SERIAL = False


# ============================================================
# 串口读取线程
# ============================================================

class SerialReader(QtCore.QThread):
    """后台读串口。只做字节→行 的切分，不做解析。

    为什么用线程而不是 QSerialPort：pyserial 行为可控、超时语义清晰，
    且本程序只读不写，没有双向时序要求。
    """

    lines_ready = QtCore.pyqtSignal(list)   # list[str]，一批完整行
    failed = QtCore.pyqtSignal(str)         # 出错描述（中文）
    opened = QtCore.pyqtSignal(str)         # 打开成功的描述

    def __init__(self, port: str, baud: int, parent=None):
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._running = False

    def run(self) -> None:
        if not HAS_SERIAL:
            self.failed.emit("未安装 pyserial，无法打开串口")
            return
        try:
            ser = serial.Serial(self._port, self._baud, timeout=0.05)
        except Exception as e:  # 串口被占用/不存在/权限不足
            self.failed.emit(f"打开 {self._port} 失败：{e}")
            return

        self._running = True
        self.opened.emit(f"{self._port} @ {self._baud}")

        buf = bytearray()
        batch: list[str] = []
        last_flush = QtCore.QElapsedTimer()
        last_flush.start()

        try:
            while self._running:
                try:
                    chunk = ser.read(4096)
                except Exception as e:
                    self.failed.emit(f"读取串口出错：{e}")
                    break
                if chunk:
                    buf.extend(chunk)
                    # 按 \n 切分；最后一段不完整则留在 buf 里等下次（半行保护）
                    while b"\n" in buf:
                        raw, _, rest = buf.partition(b"\n")
                        buf = bytearray(rest)
                        line = raw.rstrip(b"\r").decode("utf-8", errors="replace")
                        batch.append(line)
                    # 批量发送：避免每条日志一次信号，降低 UI 压力
                    if len(batch) >= 64 or last_flush.elapsed() >= 50:
                        if batch:
                            self.lines_ready.emit(batch)
                            batch = []
                        last_flush.restart()
                else:
                    # 没有新数据也把攒着的发掉，保证低速率下界面及时更新
                    if batch and last_flush.elapsed() >= 50:
                        self.lines_ready.emit(batch)
                        batch = []
                        last_flush.restart()
        finally:
            try:
                ser.close()
            except Exception:
                pass
            if batch:
                self.lines_ready.emit(batch)

    def stop(self) -> None:
        self._running = False
        self.wait(1500)


# ============================================================
# 日志回放线程（无硬件验收用）
# ============================================================

class LogReplay(QtCore.QThread):
    """把日志文件逐行喂进来，模拟串口。speed=0 表示全速。"""

    lines_ready = QtCore.pyqtSignal(list)
    opened = QtCore.pyqtSignal(str)
    finished_replay = QtCore.pyqtSignal()

    def __init__(self, path: Path, speed: float = 0.0, parent=None):
        super().__init__(parent)
        self._path = path
        self._speed = speed
        self._running = False

    def run(self) -> None:
        self._running = True
        self.opened.emit(f"回放 {self._path.name}")
        try:
            lines = self._path.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as e:
            self.opened.emit(f"读取失败：{e}")
            return
        batch: list[str] = []
        for i, line in enumerate(lines):
            if not self._running:
                break
            batch.append(line)
            if len(batch) >= 20:
                self.lines_ready.emit(batch)
                batch = []
            if self._speed > 0:
                self.msleep(int(self._speed))
        if batch:
            self.lines_ready.emit(batch)
        self.finished_replay.emit()

    def stop(self) -> None:
        self._running = False
        self.wait(1000)


# ============================================================
# 应用装配
# ============================================================

class MonitorApp(QtWidgets.QApplication):
    """把解析器、界面、串口线程接起来，并实现导出与连接动作。"""

    def __init__(self, argv: list[str], replay: Path | None = None, speed: float = 0.0):
        super().__init__(argv)

        self.parser = LogParser()
        self.reader: SerialReader | LogReplay | None = None
        self._replay_path = replay
        self._replay_speed = speed
        self._received = 0
        # 原始行留档：导出日志用。不依赖 parser 提供该能力（契约里没有），
        # 由本层自己维护，上限与界面一致，超出批量丢弃最旧的。
        self._raw_log: list[str] = []

        # 界面（按契约构造：MainWindow(parser)）
        from ui_main import MainWindow          # 延迟到这里导入，便于 --help 时不加载 PyQt 界面
        self.win = MainWindow(self.parser)

        # 视觉样式：找不到 qss 也不报错（打包后有，开发时也可能缺）
        qss_path = Path(__file__).with_name("style.qss")
        if qss_path.exists():
            self.win.setStyleSheet(qss_path.read_text(encoding="utf-8"))
        else:
            self.win.set_status("style.qss 未找到，使用默认外观")

        # 信号接线
        self.win.connect_requested.connect(self.toggle_connection)
        self.win.export_records_requested.connect(self.export_records)
        self.win.export_log_requested.connect(self.export_log)
        # 「清除」按钮：界面负责把它自己清空，解析器这边由这里清
        if hasattr(self.win, "clear_requested"):
            self.win.clear_requested.connect(self.on_clear_clicked)

        # 刷新节流：解析很快，但重画界面没必要太频繁
        self._refresh = QtCore.QTimer(self)
        self._refresh.setInterval(200)
        self._refresh.timeout.connect(self.win.refresh)
        self._refresh.start()

        if replay:
            self.start_replay()
        else:
            self.scan_ports()
            self.win.set_status("选择串口后点「连接」开始接收")

    # ---------------- 串口列表 ----------------

    def scan_ports(self) -> None:
        """扫描可用串口并填进下拉框。

        界面上那个「刷新」按钮由 ui_main 自己处理扫描；这里负责的是启动时
        先扫一次 —— 否则下拉框会是空的，用户看到"没有可用串口"却不知道为什么。
        """
        if not HAS_SERIAL:
            self.win.set_status("未安装 pyserial，无法枚举串口")
            return
        try:
            ports = [p.device for p in list_ports.comports()]
        except Exception as e:
            self.win.set_status(f"扫描串口失败：{e}")
            return
        # 按名字自然排序：COM9 排在 COM10 前面，而不是字典序
        ports.sort(key=lambda s: (len(s), s))
        self.win.set_ports(ports)
        if not ports:
            self.win.set_status("没有检测到串口：插好设备后点「刷新」")

    # ---------------- 连接 / 断开 ----------------

    def toggle_connection(self) -> None:
        if self.reader is not None:
            self.disconnect_serial()
            return
        if self._replay_path is not None:
            self.start_replay()
            return

        if not HAS_SERIAL:
            self.win.set_status("未安装 pyserial，无法连接串口")
            return
        port = self.win.current_port()
        if not port:
            self.win.set_status("没有可用串口：请插入设备后点刷新（或重启程序）")
            return
        baud = self.win.current_baud()

        rd = SerialReader(port, baud)
        rd.lines_ready.connect(self.on_lines)
        rd.failed.connect(self.on_failed)
        rd.opened.connect(self.on_opened)
        rd.finished.connect(lambda: self._clear_reader(rd))
        self.reader = rd
        rd.start()
        self.win.set_status(f"正在打开 {port} …")

    def disconnect_serial(self) -> None:
        rd = self.reader
        self.reader = None
        if rd is not None:
            rd.stop()
        self.win.set_connected(False)
        # 断开即清空：解析器状态与界面显示都要回到"未连接"，
        # 否则 U盘「就绪」、网络「已连接」这些旧值会一直挂在那儿误导操作者。
        self.clear_all_data()
        self.win.set_status("已断开（数据已清除）")

    # ---------------- 清除 ----------------

    def clear_all_data(self) -> None:
        """把解析器与界面上的所有数据一起清掉。

        界面上的「清除」按钮和断开串口都走这里 —— 两者语义相同：
        用户不希望看到上一段会话的残留数据被当成当前状态。
        """
        try:
            self.parser.reset()
        except AttributeError:
            # 兼容 parser 尚未提供 reset() 的情况（不该发生，但别让界面因此崩）
            self.parser = LogParser()
        self._raw_log.clear()
        self._received = 0
        # 重放模式下先别清掉回放线程正在用的东西；只清数据
        self.win.clear_all()

    def _clear_reader(self, rd) -> None:
        """线程自然结束时清理（例如回放跑完）。"""
        if self.reader is rd:
            self.reader = None
            self.win.set_connected(False)

    def on_clear_clicked(self) -> None:
        """界面上的「清除」按钮被点击。

        界面自己已经把显示清空了，这里负责把解析器状态与原始行留档一起清掉，
        并明确告诉用户一句 —— 否则清完没有任何反馈，用户会怀疑按钮没生效。
        """
        try:
            self.parser.reset()
        except AttributeError:
            self.parser = LogParser()
        self._raw_log.clear()
        self._received = 0
        if self.reader is not None:
            self.win.set_status("已清除历史数据（串口仍在接收，新数据会继续显示）")
        else:
            self.win.set_status("已清除全部数据")

    # ---------------- 回放 ----------------

    def start_replay(self) -> None:
        if self._replay_path is None:
            return
        if self.reader is not None:
            self.disconnect_serial()
            return
        rd = LogReplay(self._replay_path, self._replay_speed)
        rd.lines_ready.connect(self.on_lines)
        rd.opened.connect(self.on_opened)
        rd.finished_replay.connect(lambda: self.win.set_status("回放结束"))
        rd.finished.connect(lambda: self._clear_reader(rd))
        self.reader = rd
        rd.start()

    # ---------------- 串口事件 ----------------

    def on_opened(self, desc: str) -> None:
        self.win.set_connected(True)
        self.win.set_status(f"已连接：{desc}")

    def on_failed(self, msg: str) -> None:
        self.win.set_connected(False)
        self.win.set_status(msg)

    def on_lines(self, lines: list) -> None:
        """把一批原始行交给解析器，并把结构化结果推给界面。"""
        for raw in lines:
            self._received += 1
            self._raw_log.append(raw)
            line = self.parser.feed_line(raw)
            if line is not None:
                self.win.append_log(line)
        # 超上限时一次性砍掉一批，避免每行都做 list 头部删除
        overflow = len(self._raw_log) - models.MAX_LOG_LINES
        if overflow > 0:
            del self._raw_log[:overflow]

    # ---------------- 导出 ----------------

    def _ask_save(self, title: str, default_name: str, filt: str) -> Path | None:
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self.win, title, str(Path.home() / default_name), filt
        )
        return Path(path) if path else None

    def export_records(self) -> None:
        """导出 U 盘记录（从日志重建，与设备 U 盘里的文件内容等价）。"""
        rows = self.parser.records()
        if not rows:
            self.win.set_status("还没有 U 盘记录可导出（需要设备进入测量并满 1 分钟）")
            return
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = self._ask_save("导出 U 盘记录", f"尿量记录_{stamp}.csv", "CSV 文件 (*.csv)")
        if path is None:
            return
        try:
            # utf-8-sig：Excel 直接双击打开不乱码
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["分钟", "会话时间", "本分钟尿量", "平均流速", "电脑收到时刻", "会话文件"])
                for r in rows:
                    w.writerow([
                        r.minute, r.time_hms, r.ml, r.rate,
                        r.received_at.strftime("%Y-%m-%d %H:%M:%S"),
                        r.session or "",
                    ])
            self.win.set_status(f"已导出 {len(rows)} 条记录 → {path.name}")
        except Exception as e:
            self.win.set_status(f"导出失败：{e}")

    def export_log(self) -> None:
        """导出原始日志（逐行原文，不改动任何字节）。"""
        lines = self._raw_log
        if not lines:
            self.win.set_status("还没有日志可导出")
            return
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = self._ask_save("导出日志", f"串口日志_{stamp}.log", "日志文件 (*.log);;文本文件 (*.txt)")
        if path is None:
            return
        try:
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            self.win.set_status(f"已导出 {len(lines)} 行日志 → {path.name}")
        except Exception as e:
            self.win.set_status(f"导出失败：{e}")

    # ---------------- 退出 ----------------

    def shutdown(self) -> None:
        if self.reader is not None:
            self.reader.stop()
            self.reader = None


# ============================================================
# 入口
# ============================================================

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="serial-monitor",
        description="尿量计串口上位机：读日志、分区显示、导出 U 盘记录与原始日志",
    )
    ap.add_argument("--replay", metavar="日志文件",
                    help="不连串口，改为回放指定日志文件（用于无硬件时验收）")
    ap.add_argument("--speed", type=float, default=0.0,
                    help="回放时每批之间的间隔毫秒，默认 0（全速）")
    ap.add_argument("--selftest", action="store_true",
                    help="端到端自检：跑完回放后自动打印各分区摘要并退出（供自动化验收）")
    ap.add_argument("--screenshot", metavar="PNG路径",
                    help="渲染主窗口（含 style.qss）并存成 PNG 后退出，用于视觉验收")
    args = ap.parse_args(argv)

    replay = Path(args.replay) if args.replay else None
    if replay is not None and not replay.exists():
        print(f"回放文件不存在：{replay}")
        return 2

    app = MonitorApp([sys.argv[0]], replay=replay, speed=args.speed)
    app.win.show()

    if args.screenshot:
        QtCore.QTimer.singleShot(2200, lambda: _save_screenshot(app, Path(args.screenshot)))
    elif args.selftest:
        # 等回放跑完再报告：给足时间（全速回放 + 界面节流 200ms）
        QtCore.QTimer.singleShot(2500, lambda: _selftest_report(app))

    rc = app.exec_()
    app.shutdown()
    return rc


def _save_screenshot(app: "MonitorApp", path: Path) -> None:
    """把主窗口抓成 PNG。用于人工/自动核对视觉（含 qss 是否生效）。"""
    try:
        pm = app.win.grab()
        path.parent.mkdir(parents=True, exist_ok=True)
        if pm.save(str(path), "PNG"):
            print(f"screenshot saved: {path}  ({path.stat().st_size} B)")
        else:
            print(f"screenshot FAILED to save: {path}")
    except Exception as e:
        print(f"screenshot error: {e}")
    app.quit()


def _selftest_report(app: "MonitorApp") -> None:
    """把解析结果按分区打印出来，作为端到端验收证据。"""
    p = app.parser
    u = p.urine()
    s = p.storage()
    n = p.network()
    recs = p.records()
    alerts = p.alerts()

    def show(v):
        return "--" if v is None else v

    print("=" * 58)
    print("端到端自检报告（--selftest）")
    print("=" * 58)
    print(f"收到日志行数 : {app._received}")
    print(f"留档原始行数 : {len(app._raw_log)}")
    print("-" * 58)
    print("[尿液监测]")
    print(f"  累计尿量   : {show(u.total_ml)} mL")
    print(f"  实时速率   : {show(u.rate_mlh)} mL/h")
    print(f"  累计滴数   : {show(u.drops)}")
    print(f"  运行时长   : {show(u.elapsed)}")
    print(f"  状态       : {show(u.state)}")
    print(f"  报警       : {show(u.alert)}")
    print(f"  设备时钟   : {show(u.clock_hms)}")
    print(f"  电量       : {show(u.battery)}")
    print("-" * 58)
    print("[存储 / U盘]")
    print(f"  就绪       : {show(s.ready)}")
    print(f"  会话文件   : {show(s.session)}")
    print(f"  已写入行数 : {s.lines_written}")
    print(f"  失败次数   : {s.write_failures}")
    print(f"  最近结果   : {show(s.last_result)}")
    print(f"  最近错误   : {show(s.last_error)}")
    print("-" * 58)
    print("[设备与网络]")
    print(f"  设备 ID    : {show(n.device_id)}")
    print(f"  热点 MAC   : {show(n.ap_mac)}")
    print(f"  设备 IP    : {show(n.ap_ip)}")
    print(f"  手机连接   : {'已连接' if n.connected else '未连接'}")
    print(f"  手机 MAC   : {show(n.sta_mac)}")
    print(f"  手机 IP    : {show(n.sta_ip)}")
    print("-" * 58)
    print(f"[U 盘记录] 共 {len(recs)} 条")
    for r in recs[:8]:
        print(f"  第{r.minute:>2}分钟  {r.time_hms}  {r.ml:>8}  {r.rate:>10}  会话={r.session}")
    if len(recs) > 8:
        print(f"  … 其余 {len(recs) - 8} 条省略")
    print("-" * 58)
    print(f"[事件] 共 {len(alerts)} 条")
    for a in alerts[:10]:
        print(f"  {a.at.strftime('%H:%M:%S')}  [{a.kind}] {a.text}")
    if len(alerts) > 10:
        print(f"  … 其余 {len(alerts) - 10} 条省略")
    print("=" * 58)
    print("SELFTEST-DONE")
    app.quit()


if __name__ == "__main__":
    sys.exit(main())
