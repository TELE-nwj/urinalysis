"""
parser.py 的单元测试：用真实样本 samples/real_sample.log 驱动解析器，
逐条覆盖任务书里的 10 条断言，并补若干陷阱回归。

运行：
    cd F:\\ESP32\\niaio\\git\\serial-monitor
    D:\\python\\python.exe -m unittest discover -s tests -v
"""

from __future__ import annotations

import sys
import unittest
from datetime import datetime
from pathlib import Path
from unittest import mock

# 允许从任意目录运行本测试文件：把项目根目录加进 sys.path
_ROOT = Path(__file__).resolve().parents[1]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

import models  # noqa: E402
import parser as parser_module  # noqa: E402
from models import ALERT_MEANING, STATE_MEANING, Level, LogLine, tag_group  # noqa: E402
from parser import LogParser  # noqa: E402

SAMPLE_PATH = _ROOT / "samples" / "real_sample.log"
# 真机摘录：从用户真机日志（2502 行）里逐行摘出来的 23 行，另一套会话格式
# （新建测量文件 / 新测量文件 / 已就绪 / 打开用户文件），
# 用来锁住「U 盘记录的 session 列必须是 U0000003」这条回归。
REAL_DEVICE_PATH = _ROOT / "samples" / "real_device.log"


# ============================================================
# 辅助
# ============================================================

def read_sample(path: Path = SAMPLE_PATH) -> list[str]:
    """读入样本，保留原始行（行内空格不能动 —— 记录行靠空格对齐）。"""
    with open(path, encoding="utf-8", errors="replace") as fp:
        return fp.read().splitlines()


def parse_all(path: Path = SAMPLE_PATH) -> LogParser:
    """把整份样本喂给一个新解析器。"""
    parser = LogParser()
    for line in read_sample(path):
        parser.feed_line(line)
    return parser


def parse_until(marker: str, path: Path = SAMPLE_PATH) -> LogParser:
    """喂到第一行包含 marker 的日志为止，用于检查中间状态。"""
    parser = LogParser()
    for line in read_sample(path):
        parser.feed_line(line)
        if marker in line:
            break
    return parser


# ============================================================
# 断言 1 ~ 9：真实样本上的解析结果
# ============================================================

class SampleContractTest(unittest.TestCase):
    """任务书里的 10 条验收断言。"""

    @classmethod
    def setUpClass(cls) -> None:
        cls.parser = parse_all()

    # ---- 断言 1 ----
    def test_01_wifi_log_returns_logline_with_info_level(self) -> None:
        parser = LogParser()
        line = parser.feed_line("I (763) pp: pp rom version: e7ae62f")
        self.assertIsInstance(line, LogLine)
        self.assertEqual(line.level, Level.INFO)
        self.assertEqual(line.level, "I")            # Level 是 str 枚举，可直接比字符串
        self.assertEqual(line.level.cn, "信息")
        self.assertEqual(line.ts_ms, 763)
        self.assertEqual(line.tag, "pp")
        self.assertEqual(line.raw, "I (763) pp: pp rom version: e7ae62f")
        self.assertEqual(tag_group(line.tag), "system")

        # 样本第 4 行：TAG 与消息之间没有空格，也要能解析成系统层日志
        line2 = parser.feed_line("I (773) wifi:wifi firmware version: b9f67df")
        self.assertIsInstance(line2, LogLine)
        self.assertEqual(line2.level, Level.INFO)
        self.assertEqual(line2.tag, "wifi")
        self.assertEqual(line2.msg, "wifi firmware version: b9f67df")
        self.assertEqual(tag_group(line2.tag), "system")

    # ---- 断言 2 ----
    def test_02_ap_mac_and_device_id(self) -> None:
        net = self.parser.network()
        self.assertEqual(net.ap_mac, "44:B1:76:C5:15:B5")
        self.assertEqual(net.device_id, "NIAIO-C515B5")

    # ---- 断言 3 ----
    def test_03_ap_ip(self) -> None:
        self.assertEqual(self.parser.network().ap_ip, "192.168.4.1")

    # ---- 断言 4 ----
    def test_04_station_join_and_client_ip(self) -> None:
        net = self.parser.network()
        self.assertEqual(net.sta_mac, "22:C0:90:3F:5A:C3")
        self.assertTrue(net.connected)
        self.assertEqual(net.sta_ip, "192.168.4.2")

        # join 那一行处理完就应立刻是 connected
        early = parse_until("22:c0:90:3f:5a:c3 join")
        self.assertTrue(early.network().connected)
        self.assertEqual(early.network().sta_mac, "22:C0:90:3F:5A:C3")

    # ---- 断言 5 ----
    def test_05_total_ml_from_screen_command(self) -> None:
        parser = parse_until('data.t0.txt="13.5"')
        self.assertIsInstance(parser.urine().total_ml, float)
        self.assertEqual(parser.urine().total_ml, 13.5)

    # ---- 断言 6 ----
    def test_06_records_rebuilt_from_critical_section(self) -> None:
        rows = self.parser.records()
        self.assertGreaterEqual(len(rows), 5)
        self.assertEqual([r.minute for r in rows], [1, 2, 3, 4, 5])

        first = rows[0]
        self.assertEqual(first.minute, 1)
        self.assertEqual(first.time_hms, "00:01:00")
        self.assertEqual(first.ml, "1.6ml")
        self.assertEqual(first.rate, "96.0ml/h")
        self.assertEqual(first.session, "U0000001")
        self.assertIsInstance(first.received_at, datetime)

    # ---- 断言 7 ----
    def test_07_write_failure_counted_and_error_contains_timeout(self) -> None:
        st = self.parser.storage()
        self.assertEqual(st.write_failures, 1)
        self.assertIsNotNone(st.last_error)
        self.assertIn("超时", st.last_error)
        self.assertIn("写入阶段", st.last_error)
        self.assertIn("错误码=3", st.last_error)
        # 样本里第4分钟失败之后，第5分钟又写成功了一次，
        # 所以 last_result_ok 回到 True；而 last_error 按契约保留「最近一次失败详情」
        self.assertTrue(st.last_result_ok)

    # ---- 断言 8 ----
    def test_08_24h_conclusion_alert(self) -> None:
        alerts = [a for a in self.parser.alerts() if a.kind == "24h结论"]
        self.assertEqual(len(alerts), 1)
        self.assertIn("结论=C", alerts[0].text)
        self.assertIn("1234.5", alerts[0].text)
        self.assertIsInstance(alerts[0].at, datetime)

    # ---- 断言 9 ----
    def test_09_urine_bag_full_alert(self) -> None:
        alerts = [a for a in self.parser.alerts() if a.kind == "尿袋满"]
        self.assertEqual(len(alerts), 1)
        self.assertIn("2010.5", alerts[0].text)

    # ---- 断言 10 ----
    def test_10_blank_half_and_garbage_lines_never_raise(self) -> None:
        parser = LogParser()
        weird = [
            "",                                        # 空行
            " ",                                       # 空格行
            "\t",                                      # 制表符行
            "\x00\x01 junk 乱码",                       # 乱码
            "I (25611) screen: [串口屏] data.t0.txt=",  # 半行：值被截断
            'I (25611) screen: [串口屏] data.t1.txt="',  # 半行：引号没闭合
            "I (123",                                  # 半行：头部截断
            "第4分钟写盘失败(",                          # 半行：括号没闭合
            'I (1) screen: [串口屏] data.t0.txt="abc"',  # 值不是数字
            'I (1) screen: [串口屏] data.t2.txt="--"',   # 值不是整数
            'E (90000111) screen: 数据帧发送失败: data.t0.txt="1.0"',  # 发送失败的帧
        ]
        for raw in weird:
            with self.subTest(raw=raw):
                parser.feed_line(raw)                  # 唯一要求：不抛异常

        # 非空但解析不出头部的行，仍要按契约返回 LogLine（整行当消息）
        line = parser.feed_line("随机乱码 \x00\x01 junk")
        self.assertIsInstance(line, LogLine)
        self.assertIsNone(line.level)
        self.assertIsNone(line.ts_ms)
        self.assertEqual(line.tag, "")
        self.assertEqual(line.msg, "随机乱码 \x00\x01 junk")

        # 纯空白行没有可显示内容 -> None
        self.assertIsNone(parser.feed_line("   "))
        self.assertIsNone(parser.feed_line(""))

        # 上面那些非法屏指令 / 发送失败的帧，都不能改动尿量状态
        self.assertIsNone(parser.urine().total_ml)


# ============================================================
# 额外回归：样本里的陷阱与状态机细节
# ============================================================

class ParserDetailTest(unittest.TestCase):
    """样本里几个容易踩的坑，单独回归一遍。"""

    def test_11_tag_without_space_is_tolerated(self) -> None:
        # 样本第 3 行：wifi:wifi driver task: ... （TAG 后缺空格）
        parser = LogParser()
        line = parser.feed_line(
            "I (773) wifi:wifi driver task: 3fceed50, prio:23, stack:6656, core=0"
        )
        self.assertIsInstance(line, LogLine)
        self.assertEqual(line.level, Level.INFO)
        self.assertEqual(line.tag, "wifi")
        self.assertEqual(
            line.msg, "wifi driver task: 3fceed50, prio:23, stack:6656, core=0"
        )
        self.assertEqual(line.ts_ms, 773)

    def test_12_write_success_counted(self) -> None:
        parser = parse_until("第1分钟已写入U盘")
        st = parser.storage()
        self.assertEqual(st.lines_written, 1)
        self.assertTrue(st.last_result_ok)
        self.assertIsNone(st.last_error)
        self.assertIn("已写入U盘", st.last_result)

        # 失败那一刻：写盘结果是失败，错误详情已记下
        failed = parse_until("5秒后重试").storage()
        self.assertEqual(failed.lines_written, 3)      # 第1~3分钟成功
        self.assertEqual(failed.write_failures, 1)
        self.assertFalse(failed.last_result_ok)
        self.assertEqual(failed.last_result, "写盘失败")

        # 整份样本：4 次成功、1 次失败，最后一次（第5分钟）是成功的
        full = parse_all().storage()
        self.assertEqual(full.lines_written, 4)
        self.assertTrue(full.last_result_ok)
        self.assertIsNotNone(full.last_error)          # 旧错误不会被清掉

    def test_13_usb_ready_then_removed(self) -> None:
        early = parse_until("存储服务就绪")
        self.assertTrue(early.storage().ready)
        self.assertIsNotNone(early.storage().checked_at)
        self.assertEqual(early.storage().health, "就绪")

        full = parse_all().storage()
        self.assertFalse(full.ready)                 # 样本末尾已拔盘
        self.assertEqual(full.health, "不可用")

    def test_14_state_transition_alerts(self) -> None:
        alerts = [a for a in parse_all().alerts() if a.kind == "状态"]
        texts = [a.text for a in alerts]
        self.assertIn("待开始 -> 测量中", texts)
        self.assertIn("测量中 -> 暂停", texts)

    def test_15_send_failure_line_does_not_clobber_urine(self) -> None:
        # 样本结尾那条 E 级「数据帧发送失败」里也带 data.t0.txt="1.0"，
        # 那是没发出去的帧，不能改尿量：整份样本跑完后应停在 24.8。
        self.assertEqual(parse_all().urine().total_ml, 24.8)

        only = LogParser()
        only.feed_line('E (90000111) screen: 数据帧发送失败: data.t0.txt="1.0"')
        self.assertIsNone(only.urine().total_ml)

    def test_16_boot_alert_and_session_counter(self) -> None:
        parser = parse_all()
        boots = [a for a in parser.alerts() if a.kind == "启动"]
        self.assertEqual(len(boots), 1)
        self.assertIn("编译于", boots[0].text)
        self.assertEqual(parser.session_index, 1)
        self.assertIn("Oct 12 2026", parser.boot_text)

    def test_17_all_screen_fields_parsed(self) -> None:
        u = parse_all().urine()
        self.assertEqual(u.total_ml, 24.8)
        self.assertEqual(u.rate_mlh, 678.0)
        self.assertEqual(u.drops, 51)
        self.assertEqual(u.elapsed, "00:03:00")
        self.assertEqual(u.state, "stop")
        self.assertEqual(u.alert, "A")
        self.assertEqual(u.clock_hms, "00:00:02")
        self.assertEqual(u.battery, "54%")
        # 与 models.py 的常量表对得上
        self.assertEqual(STATE_MEANING[u.state], "暂停")
        self.assertEqual(ALERT_MEANING[u.alert], "正常")

    def test_18_fresh_parser_reports_unknown_as_none(self) -> None:
        parser = LogParser()
        u, s, n = parser.urine(), parser.storage(), parser.network()
        self.assertIsNone(u.total_ml)
        self.assertIsNone(u.rate_mlh)
        self.assertIsNone(u.drops)
        self.assertIsNone(s.ready)
        self.assertIsNone(s.session)
        self.assertEqual(s.health, "未探测")
        self.assertEqual(s.lines_written, 0)
        self.assertEqual(s.write_failures, 0)
        self.assertIsNone(s.last_result_ok)
        self.assertIsNone(n.ap_mac)
        self.assertIsNone(n.device_id)
        self.assertIsNone(n.ap_ip)
        self.assertIsNone(n.sta_mac)
        self.assertFalse(n.connected)
        self.assertEqual(n.ssid, "NIAIO-Monitor")
        self.assertEqual(parser.records(), [])
        self.assertEqual(parser.alerts(), [])

    def test_19_alerts_keep_log_order(self) -> None:
        alerts = parse_all().alerts()
        self.assertTrue(alerts)
        stamps = [a.at for a in alerts]
        self.assertEqual(stamps, sorted(stamps))     # 时间线必须单调不减

    def test_20_records_and_alerts_return_copies(self) -> None:
        parser = parse_all()
        parser.records().append("外部乱塞")            # 返回值是副本，改不动内部状态
        parser.alerts().append("外部乱塞")
        self.assertEqual(len(parser.records()), 5)
        self.assertEqual(len(parser.alerts()), len(parse_all().alerts()))


# ============================================================
# 迭代2：reset() 与 lines_for() 详情数据源
# ============================================================

class ResetAndDetailTest(unittest.TestCase):
    """断开串口要能把状态清干净；「详情」弹窗要能按类别取到原始行。"""

    def test_21_reset_clears_every_state(self) -> None:
        parser = parse_all()
        # 先确认样本确实把状态填满了，否则下面的断言没有意义
        self.assertEqual(parser.urine().total_ml, 24.8)
        self.assertIsNotNone(parser.storage().ready)
        self.assertTrue(parser.network().connected)
        self.assertEqual(len(parser.records()), 5)
        self.assertTrue(parser.alerts())

        parser.reset()

        u = parser.urine()
        for field in ("total_ml", "rate_mlh", "drops", "elapsed", "state",
                      "alert", "clock_hms", "battery"):
            with self.subTest(field=field):
                self.assertIsNone(getattr(u, field))

        s = parser.storage()
        self.assertIsNone(s.ready)
        self.assertIsNone(s.session)
        self.assertEqual(s.lines_written, 0)
        self.assertEqual(s.write_failures, 0)
        self.assertIsNone(s.last_result)
        self.assertIsNone(s.last_result_ok)
        self.assertIsNone(s.last_error)
        self.assertIsNone(s.checked_at)
        self.assertEqual(s.health, "未探测")

        n = parser.network()
        self.assertIsNone(n.ap_mac)
        self.assertIsNone(n.device_id)
        self.assertIsNone(n.ap_ip)
        self.assertIsNone(n.sta_mac)
        self.assertIsNone(n.sta_ip)
        self.assertFalse(n.connected)
        self.assertEqual(n.ssid, "NIAIO-Monitor")

        self.assertEqual(parser.records(), [])
        self.assertEqual(parser.alerts(), [])
        self.assertEqual(parser.session_index, 0)
        self.assertEqual(parser.boot_text, "")
        # 详情数据源也要一起清空
        self.assertEqual(parser.lines_for("storage"), [])
        self.assertEqual(parser.lines_for("network"), [])

    def test_22_reset_is_repeatable_and_counter_restarts(self) -> None:
        parser = LogParser()
        for _ in range(3):
            parser.reset()                                   # 空状态反复 reset 不报错
            self.assertIsNone(parser.urine().total_ml)

        boot = "I (2) main: 尿量计启动  分层版  编译于 Oct 12 2026 20:06:49"
        parser.feed_line(boot)
        self.assertEqual(parser.session_index, 1)
        parser.reset()
        self.assertEqual(parser.session_index, 0)             # 计数器回到 0
        parser.feed_line(boot)
        self.assertEqual(parser.session_index, 1)             # 重新从 1 开始，不累加

        # 清空后继续喂数据，解析一切照常
        parser.feed_line("I (3) storage: 存储服务就绪")
        self.assertTrue(parser.storage().ready)
        self.assertEqual(len(parser.lines_for("storage")), 1)

    def test_23_lines_for_storage_only_contains_storage_tags(self) -> None:
        rows = parse_all().lines_for("storage")
        self.assertTrue(rows)
        self.assertTrue(all(isinstance(r, LogLine) for r in rows))
        tags = {r.tag for r in rows}
        self.assertLessEqual(tags, {"storage", "CH376T", "S1"})   # 只允许这三个 TAG
        self.assertIn("storage", tags)
        self.assertIn("S1", tags)                                 # S1 的写盘行属于存储类
        self.assertGreaterEqual(len(rows), 15)
        # 界面上要看得到初始化码
        self.assertTrue(any(r.msg.startswith("CH376 版本号") for r in rows))
        self.assertTrue(any("存储服务就绪" in r.msg for r in rows))
        # 别的类别的行不能混进来
        self.assertFalse(any(r.tag in ("wifi", "screen", "monitor") for r in rows))

    def test_24_lines_for_network_contains_wifi_and_netif(self) -> None:
        rows = parse_all().lines_for("network")
        tags = {r.tag for r in rows}
        self.assertIn("wifi", tags)
        self.assertIn("esp_netif_lwip", tags)
        self.assertNotIn("S1", tags)
        # 版本号 / 物理层校准这些初始化码要看得到
        self.assertTrue(any(r.msg == "wifi firmware version: b9f67df" for r in rows))
        self.assertTrue(any(r.tag == "phy_init" and "phy_version 712" in r.msg
                            for r in rows))
        self.assertTrue(any("192.168.4.1" in r.msg for r in rows))
        # 纯业务行不能混进来
        self.assertFalse(any(r.tag in ("monitor", "screen", "storage", "S1")
                             for r in rows))

    def test_25_lines_for_unknown_category_returns_empty_list(self) -> None:
        parser = parse_all()
        for bad in ("不存在的类别", "", "Storage", "NETWORK", "uric",
                    None, 123, [], {}):
            with self.subTest(category=repr(bad)):
                self.assertEqual(parser.lines_for(bad), [])      # 不抛异常

    def test_26_lines_for_returns_a_copy(self) -> None:
        parser = parse_all()
        rows = parser.lines_for("network")
        count = len(rows)
        self.assertGreater(count, 0)
        rows.append("外部乱塞")                                   # 改返回值
        rows.clear()
        again = parser.lines_for("network")
        self.assertEqual(len(again), count)
        self.assertEqual([r.raw for r in again],
                         [r.raw for r in parser.lines_for("network")])

    def test_27_lines_for_network_is_long_and_keeps_file_order(self) -> None:
        rows = parse_all().lines_for("network")
        self.assertGreaterEqual(len(rows), 30)        # 样本里 WiFi 启动段很长
        sample = read_sample()
        cursor = 0
        for i, row in enumerate(rows):
            try:
                cursor = sample.index(row.raw, cursor) + 1   # 游标推进，容忍重复行
            except ValueError:
                self.fail(f"第 {i} 行不在样本里或顺序错乱：{row.raw!r}")

    def test_28_raw_lines_are_capped_at_max_log_lines(self) -> None:
        with mock.patch("parser.MAX_LOG_LINES", 3):
            parser = LogParser()
            for i in range(5):
                parser.feed_line(f"I ({i}) storage: 第{i}行")
            rows = parser.lines_for("storage")
            self.assertEqual(len(rows), 3)                       # 只留最近 3 行
            self.assertEqual([r.msg for r in rows], ["第2行", "第3行", "第4行"])
        # 上限就是 models.MAX_LOG_LINES，别在 parser.py 里另起一个数
        self.assertEqual(parser_module.MAX_LOG_LINES, models.MAX_LOG_LINES)


# ============================================================
# 真机格式：会话名（用户真机日志暴露的 bug）
# ============================================================

class RealDeviceSessionTest(unittest.TestCase):
    """真机固件的会话格式与旧样本不同，U 盘记录表的 session 列曾经整列是 None。"""

    def test_29_real_device_record_gets_session(self) -> None:
        parser = parse_all(REAL_DEVICE_PATH)
        self.assertEqual(parser.storage().session, "U0000003")
        rows = parser.records()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].minute, 1)
        self.assertEqual(rows[0].time_hms, "00:01:00")
        self.assertEqual(rows[0].ml, "7.5ml")
        self.assertEqual(rows[0].rate, "450.0ml/h")
        self.assertEqual(rows[0].session, "U0000003")        # 修 bug 前这里是 None
        self.assertEqual(parser.storage().lines_written, 1)
        self.assertEqual(parser.storage().write_failures, 0)
        self.assertTrue(parser.storage().ready)

        # 第一次建文件失败那一段（用户名试探行 + 新建文件失败）不能定出会话名
        early = parse_until("新建文件失败", REAL_DEVICE_PATH)
        self.assertIsNone(early.storage().session)
        self.assertEqual(early.records(), [])

    def test_30_session_backfills_records_that_came_first(self) -> None:
        """日志从中间开始时：记录行先到、会话名后到，会话名要回填给旧记录。"""
        parser = LogParser()
        parser.feed_line("I (1) S1: >>>>> 进入临界区(停液滴中断): 第1分钟 00:01:00          7.5ml           450.0ml/h")
        parser.feed_line("I (2) S1: >>>>> 进入临界区(停液滴中断): 第2分钟 00:02:00          6.0ml           360.0ml/h")
        self.assertEqual([r.session for r in parser.records()], [None, None])

        parser.feed_line("I (3) storage: 新测量文件: /U0000009.TXT")
        self.assertEqual([r.session for r in parser.records()],
                         ["U0000009", "U0000009"])            # 回填

        # 会话名已知之后，新来的记录直接带上它
        parser.feed_line("I (4) S1: >>>>> 进入临界区(停液滴中断): 第3分钟 00:03:00          5.0ml           300.0ml/h")
        self.assertEqual(parser.records()[-1].session, "U0000009")

        # 换会话文件后：老记录保持老名字（只回填 None），新记录用新名字
        parser.feed_line("I (5) storage: 新测量文件: /U0000010.TXT")
        parser.feed_line("I (6) S1: >>>>> 进入临界区(停液滴中断): 第4分钟 00:04:00          4.0ml           240.0ml/h")
        self.assertEqual([r.session for r in parser.records()],
                         ["U0000009", "U0000009", "U0000009", "U0000010"])
        self.assertEqual(parser.storage().session, "U0000010")

    def test_31_username_probe_lines_never_define_session(self) -> None:
        """CH376 连打的一串「用户名 'U000000N'」是试名字，不能拿来定会话。"""
        parser = LogParser()
        for n in ("U0000001", "U0000002", "U0000003"):
            parser.feed_line(f"I (1) storage: 用户名 '{n}' → 文件名 '/{n}.TXT'")
        self.assertIsNone(parser.storage().session)

        parser.feed_line("I (2) storage: 新测量文件: /U0000007.TXT")
        self.assertEqual(parser.storage().session, "U0000007")
        # 会话定了以后再来的用户名试探行也不能把它改掉
        parser.feed_line("I (3) storage: 用户名 'U0000001' → 文件名 '/U0000001.TXT'")
        self.assertEqual(parser.storage().session, "U0000007")

    def test_32_ready_and_open_user_file_also_set_session(self) -> None:
        ready = LogParser()
        ready.feed_line("I (1) S1: <<<<< 退出临界区: 测量文件 /U0000013.TXT 已就绪")
        self.assertEqual(ready.storage().session, "U0000013")
        # 这些都不是写盘事件，别误判成成功/失败
        self.assertEqual(ready.storage().lines_written, 0)
        self.assertEqual(ready.storage().write_failures, 0)
        self.assertIsNone(ready.storage().last_result_ok)

        opened = LogParser()
        opened.feed_line("I (1) storage: 打开用户文件(追加模式, 当前 56 字节): /U0000012.TXT")
        self.assertEqual(opened.storage().session, "U0000012")
        self.assertEqual(opened.storage().lines_written, 0)

        # 真机那次「新建文件失败」不能被当成写盘失败
        failed_new = LogParser()
        failed_new.feed_line(
            "W (1) S1: <<<<< 退出临界区: 新建文件失败(错误码=-1 一般失败)，"
            "U盘仍不可用，继续计量，5秒后重试"
        )
        self.assertIsNone(failed_new.storage().session)
        self.assertEqual(failed_new.storage().write_failures, 0)

    def test_33_reset_clears_pending_backfill_state(self) -> None:
        parser = LogParser()
        parser.feed_line("I (1) S1: >>>>> 进入临界区(停液滴中断): 第1分钟 00:01:00          7.5ml           450.0ml/h")
        self.assertEqual(len(parser.records()), 1)

        parser.reset()
        self.assertEqual(parser.records(), [])
        parser.feed_line("I (2) storage: 新测量文件: /U0000001.TXT")
        self.assertEqual(parser.storage().session, "U0000001")
        self.assertEqual(parser.records(), [])       # 不会冒出被回填出来的幽灵记录


    def test_34_session_creation_failure_is_visible(self) -> None:
        """真机那次「新建文件失败」要让界面看得到原因，但不能算作写盘失败。"""
        parser = parse_until("新建文件失败", REAL_DEVICE_PATH)
        st = parser.storage()
        self.assertEqual(st.last_result, "会话建立失败")
        self.assertIs(st.last_result_ok, False)
        self.assertIn("新建文件失败", st.last_error)
        self.assertIn("错误码=-1", st.last_error)
        self.assertIn("一般失败", st.last_error)
        self.assertEqual(st.write_failures, 0)        # 与「写盘失败」分开，别混计数
        self.assertEqual(st.lines_written, 0)
        self.assertIsNone(st.session)                 # 失败那次不能定出会话名

        # 「恢复会话」失败走同一条规则
        resumed = LogParser()
        resumed.feed_line(
            "W (1) S1: <<<<< 退出临界区: 恢复会话失败(错误码=-2 磁盘错误)，5秒后重试"
        )
        self.assertEqual(resumed.storage().last_result, "会话建立失败")
        self.assertIn("恢复会话失败 错误码=-2 磁盘错误", resumed.storage().last_error)
        self.assertEqual(resumed.storage().write_failures, 0)

        # 而「写盘失败」仍然走原来的规则，不会被新规则抢走
        write_fail = LogParser()
        write_fail.feed_line(
            "W (1) S1: <<<<< 退出临界区: 第4分钟写盘失败(写入阶段 错误码=3 超时): "
            "/U0000001.TXT 已跳过，5秒后重试"
        )
        self.assertEqual(write_fail.storage().write_failures, 1)
        self.assertIn("超时", write_fail.storage().last_error)

    def test_35_storage_result_recovers_after_session_is_created(self) -> None:
        """失败之后建好了会话并写盘成功，last_result_ok 要回到 True。"""
        mid = parse_until("新建文件失败", REAL_DEVICE_PATH)
        self.assertIs(mid.storage().last_result_ok, False)

        parser = parse_all(REAL_DEVICE_PATH)          # 整份真机样本
        st = parser.storage()
        self.assertTrue(st.last_result_ok)            # 最后一次是写盘成功
        self.assertEqual(st.last_result, "第1分钟已写入U盘")
        self.assertIn("新建文件失败", st.last_error)   # 中间那次失败原因保留着
        self.assertEqual(st.session, "U0000003")
        self.assertEqual(st.write_failures, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
