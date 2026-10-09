package com.niaio.mobile.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.niaio.mobile.DisplayFormat
import com.niaio.mobile.ProbePolicy
import com.niaio.mobile.ProbeState
import com.niaio.mobile.SessionView
import com.niaio.mobile.Snapshot
import com.niaio.mobile.ui.theme.NiaioTone

/** 界面对外只读的一切交互都以回调形式传入，Activity 持有连接与生命周期。 */
data class MonitorActions(
    val onPage: (Int) -> Unit,
    val onConnect: () -> Unit,
    val onDisconnect: () -> Unit,
    val onOpenWifiSettings: () -> Unit,
    val onSimulated: (Boolean) -> Unit,
    val onCommand: (String) -> Unit,
    val onClearLog: () -> Unit,
    val onForegroundReconnect: (Boolean) -> Unit,
    val onConfirmReset: (Boolean) -> Unit
)

private const val PAGE_LIVE = 0
private const val PAGE_DEVICE = 1

/**
 * 两个页面：实时监测与设备连接（合同固定）。
 *
 * 版式只做了一件事——把原先平铺的一长串读数按「现在能不能信这组数」排队：
 * 最上面是数据新鲜度，然后是主读数，其次是指标与设备细节，最后才是操作。
 * 每一项合同要求的内容都在原处，文案逐字未改。
 */
@Composable
fun MonitorScreen(state: ProbeState, page: Int, confirmReset: Boolean, actions: MonitorActions) {
    val session = state.session
    val sample = session.snapshot
    val simulated = state.simulated || session.simulated
    val live = state.connected && session.ready && !session.stale

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.surface)
            .padding(WindowInsets.safeDrawing.asPaddingValues())
    ) {
        AppHeader(state = state, session = session, live = live)
        TabRow(selectedTabIndex = page) {
            Tab(selected = page == PAGE_LIVE, onClick = { actions.onPage(PAGE_LIVE) }, text = { Text("实时监测") })
            Tab(selected = page == PAGE_DEVICE, onClick = { actions.onPage(PAGE_DEVICE) }, text = { Text("设备连接") })
        }
        if (simulated) {
            ToneBanner(
                tone = NiaioTone.Warn,
                icon = NiaioIcons.Info,
                text = "模拟数据，仅用于界面演示。模拟演示禁用真实测量控制，样例不代表设备数据。"
            )
        } else if (page == PAGE_LIVE && !live) {
            ToneBanner(
                tone = if (state.connected) NiaioTone.Warn else NiaioTone.Danger,
                icon = NiaioIcons.Warning,
                text = if (sample == null) "数据暂未更新。" else if (state.connected) {
                    "数据暂未更新 · 以下为最后数据，不是实时读数。"
                } else {
                    "设备离线 · 以下为最后数据，不是实时读数，测量控制已禁用。"
                }
            )
        }
        if (page == PAGE_LIVE) {
            LivePage(state = state, session = session, sample = sample, simulated = simulated, live = live, actions = actions)
        } else {
            DevicePage(state = state, session = session, actions = actions)
        }
    }
    ResetDialog(visible = confirmReset, actions = actions)
}

/** 标题、连接状态与「最后收到新数据」——合同要求保留最后更新时间，这里让它常驻可见。 */
@Composable
private fun AppHeader(state: ProbeState, session: SessionView, live: Boolean) {
    val tone = when {
        live -> NiaioTone.Ok
        state.connected -> NiaioTone.Warn
        state.connecting -> NiaioTone.Info
        else -> NiaioTone.Neutral
    }
    Row(
        modifier = Modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp, top = 14.dp, bottom = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text("尿量监测仪", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            session.lastUpdateHms?.let {
                Spacer(Modifier.height(3.dp))
                Text(
                    "最后收到新数据 $it（手机时间）",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Spacer(Modifier.width(12.dp))
        ToneChip(text = state.status, tone = tone, pulse = state.connecting)
    }
}

@Composable
private fun LivePage(
    state: ProbeState,
    session: SessionView,
    sample: Snapshot?,
    simulated: Boolean,
    live: Boolean,
    actions: MonitorActions
) {
    val controls = live && !session.waiting && !simulated
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 20.dp, end = 20.dp, bottom = 28.dp)
    ) {
        session.protocolError?.let { message ->
            item {
                Spacer(Modifier.height(16.dp))
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }

        item {
            // 主读数：整屏唯一的大字号，合同指定给累计尿量。
            Column(Modifier.fillMaxWidth().padding(top = 20.dp, bottom = 18.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "累计尿量",
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.weight(1f)
                    )
                    ToneChip(text = stateLabel(sample?.state), tone = stateTone(sample?.state), dot = sample != null)
                }
                Spacer(Modifier.height(10.dp))
                ReadoutValue(
                    value = sample?.let { DisplayFormat.volume(it.totalMl) } ?: "--",
                    unit = "mL"
                )
                Spacer(Modifier.height(12.dp))
                ReadoutNote(
                    text = if (live) {
                        "有效测量 " + (sample?.let { DisplayFormat.elapsed(it.elapsed) } ?: "--:--:--")
                    } else {
                        "取自 " + (session.lastUpdateHms ?: "--") + " 快照 · 有效测量 " +
                            (sample?.let { DisplayFormat.elapsed(it.elapsed) } ?: "--:--:--")
                    }
                )
                Spacer(Modifier.height(18.dp))
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                MetricTile(
                    label = "实时流速",
                    value = sample?.let { DisplayFormat.volume(it.rateMlh) } ?: "--",
                    unit = "mL/h",
                    modifier = Modifier.weight(1f)
                )
                Box(
                    Modifier
                        .width(1.dp)
                        .height(48.dp)
                        .background(MaterialTheme.colorScheme.outlineVariant)
                )
                MetricTile(
                    label = "累计滴数",
                    value = sample?.let { it.drops.toString() } ?: "--",
                    unit = "滴",
                    modifier = Modifier.weight(1f).padding(start = 16.dp)
                )
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }

        item {
            InfoCard(
                header = if (live) "设备状态" else "最后设备状态",
                headerChip = {
                    ToneChip(
                        text = session.sync,
                        tone = if (session.sync == "时间已同步") NiaioTone.Ok else NiaioTone.Neutral,
                        icon = if (session.sync == "时间已同步") NiaioIcons.Check else null,
                        dot = session.sync != "时间已同步"
                    )
                },
                footer = storageFootnote(sample)
            ) {
                StatusRow(
                    icon = NiaioIcons.Clock,
                    label = if (live) "设备时钟" else "最后设备时钟",
                    value = sample?.clock ?: "--:--:--",
                    showDivider = false
                )
                StatusRow(icon = NiaioIcons.Battery, label = "电量", value = DisplayFormat.battery(sample?.battery))
                StatusRow(
                    icon = NiaioIcons.Storage,
                    label = "U盘",
                    value = storageLabel(sample?.storage),
                    valueTone = storageTone(sample?.storage)
                )
            }
        }

        item {
            // 离线和数据陈旧不虚构实时数据：报警值来自上一份快照，就不能当作当前结论展示。
            val alertKnown = live && sample != null
            AlertBar(
                tone = if (alertKnown) alertTone(sample?.alert) else NiaioTone.Warn,
                icon = if (alertKnown && sample?.alert == "A") NiaioIcons.Check else NiaioIcons.Warning,
                text = if (alertKnown) "报警：" + alertLabel(sample?.alert) else "报警状态未知",
                note = when {
                    sample == null -> "尚无数据"
                    live -> "连续监测中"
                    else -> "离线不可判定"
                }
            )
        }

        item {
            Spacer(Modifier.height(20.dp))
            ActionColumn {
                if (session.control.isNotEmpty()) {
                    if (session.waiting) {
                        WaitingIndicator(session.control)
                        Spacer(Modifier.height(9.dp))
                    } else {
                        Text(
                            text = session.control,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                PrimaryAction(
                    text = when (sample?.state) {
                        "MEASURING" -> "暂停测量"
                        "PAUSED" -> "继续测量"
                        else -> "开始测量"
                    },
                    icon = if (sample?.state == "MEASURING") NiaioIcons.Pause else null,
                    enabled = controls,
                    onClick = { actions.onCommand(if (sample?.state == "MEASURING") "pause" else "start") }
                )
                SecondaryAction(text = "复位测量", enabled = controls, onClick = { actions.onConfirmReset(true) })
                if (session.waiting) {
                    QuietAction(text = "执行未确认？查看连接并重新连接", onClick = { actions.onPage(PAGE_DEVICE) })
                } else {
                    QuietAction(text = "查看设备连接 / 重新连接", onClick = { actions.onPage(PAGE_DEVICE) })
                }
            }
        }
    }
}

@Composable
private fun DevicePage(state: ProbeState, session: SessionView, actions: MonitorActions) {
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 20.dp, end = 20.dp, bottom = 28.dp)
    ) {
        item {
            Column(Modifier.fillMaxWidth().padding(top = 20.dp)) {
                SectionTitle("连接步骤")
                Spacer(Modifier.height(14.dp))
                StepItem(
                    index = 1,
                    title = "在系统设置中连接设备热点",
                    detail = "提示「无互联网连接」时请保持连接，不要切换到蜂窝网络。"
                )
                Spacer(Modifier.height(14.dp))
                StepItem(
                    index = 2,
                    title = "返回本应用并点击「" + connectLabel(state, session) + "」",
                    detail = "默认 SSID：NIAIO-Monitor（可配置，以系统热点为准）。"
                )
                Spacer(Modifier.height(18.dp))
                ActionColumn {
                    PrimaryAction(
                        text = connectLabel(state, session),
                        icon = NiaioIcons.Wifi,
                        enabled = !state.connecting,
                        onClick = { actions.onForegroundReconnect(true); actions.onConnect() }
                    )
                    SecondaryAction(
                        text = "打开系统 Wi-Fi 设置",
                        icon = NiaioIcons.Settings,
                        onClick = actions.onOpenWifiSettings
                    )
                    QuietAction(
                        text = "断开连接",
                        enabled = state.connected || state.connecting,
                        onClick = { actions.onForegroundReconnect(false); actions.onDisconnect() }
                    )
                }
            }
        }

        item {
            InfoCard(header = "设备信息") {
                KeyValueRow(label = "设备名", value = session.deviceId ?: "--", showDivider = false)
                KeyValueRow(label = "Wi-Fi 地址", value = state.wifiAddress ?: "--", monospace = true)
                KeyValueRow(label = "启动标识", value = session.bootId ?: "--", monospace = true)
                KeyValueRow(label = "会话标识", value = session.sessionId ?: "--", monospace = true)
                KeyValueRow(
                    label = "连接结果",
                    value = state.status,
                    valueTone = if (state.connected) NiaioTone.Ok else NiaioTone.Neutral
                )
                KeyValueRow(
                    label = "对时结果",
                    value = session.sync,
                    valueTone = if (session.sync == "时间已同步") NiaioTone.Ok else NiaioTone.Warn
                )
            }
        }

        item {
            InfoCard(footer = "模拟演示禁用真实测量控制，样例不代表设备数据。") {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            text = "模拟演示（静态样例）",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    Spacer(Modifier.width(16.dp))
                    Switch(
                        checked = state.simulated,
                        onCheckedChange = { actions.onForegroundReconnect(false); actions.onSimulated(it) }
                    )
                }
            }
        }

        item {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(top = 22.dp)) {
                SectionTitle("通信记录", modifier = Modifier.weight(1f))
                QuietAction(text = "清空", onClick = actions.onClearLog)
            }
        }
        if (state.log.isEmpty()) {
            item {
                Spacer(Modifier.height(10.dp))
                DisabledHint("暂无收发记录。")
            }
        } else {
            // 逐条 item：通信记录可能很长，交给 LazyColumn 惰性处理，不用一次性铺满。
            items(state.log.asReversed()) { line ->
                SelectionContainer {
                    Text(
                        text = line,
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)
                    )
                }
            }
        }

        item {
            Column(Modifier.fillMaxWidth().padding(top = 18.dp)) {
                DisabledHint("只通过设备 Wi-Fi 通信。前台恢复会重新连接，旧开始、暂停、复位请求不会重发。")
                Spacer(Modifier.height(8.dp))
                DisabledHint("实机兼容性与测量控制尚未验证。")
                Spacer(Modifier.height(12.dp))
                SelectionContainer {
                    Text(
                        text = ProbePolicy.URL,
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

@Composable
private fun ResetDialog(visible: Boolean, actions: MonitorActions) {
    if (!visible) return
    AlertDialog(
        onDismissRequest = { actions.onConfirmReset(false) },
        title = { Text("复位测量") },
        text = { Text("将清空本次尿量、滴数和有效测量时间，并回到待开始。U盘历史文件不会删除。") },
        dismissButton = { TextButton(onClick = { actions.onConfirmReset(false) }) { Text("取消") } },
        confirmButton = {
            TextButton(onClick = { actions.onConfirmReset(false); actions.onCommand("reset") }) { Text("确认复位") }
        }
    )
}

private fun connectLabel(state: ProbeState, session: SessionView): String =
    if (state.connected || session.deviceId != null) "重新连接" else "连接设备"

private fun stateLabel(state: String?): String = when (state) {
    "IDLE" -> "待开始"
    "MEASURING" -> "测量中"
    "PAUSED" -> "已暂停"
    else -> "--"
}

private fun stateTone(state: String?): NiaioTone = when (state) {
    "MEASURING" -> NiaioTone.Info
    "PAUSED" -> NiaioTone.Warn
    "IDLE" -> NiaioTone.Neutral
    else -> NiaioTone.Neutral
}

private fun alertLabel(alert: String?): String = when (alert) {
    "A" -> "正常"
    "B" -> "无尿"
    "C" -> "少尿"
    "D" -> "多尿"
    "E" -> "尿袋满"
    else -> "--"
}

private fun alertTone(alert: String?): NiaioTone = when (alert) {
    "A" -> NiaioTone.Ok
    "B", "C", "D", "E" -> NiaioTone.Danger
    else -> NiaioTone.Neutral
}

private fun storageLabel(storage: String?): String = when (storage) {
    "ABSENT" -> "未连接"
    "READY" -> "可用"
    "ERROR" -> "异常"
    else -> "未确认"
}

private fun storageTone(storage: String?): NiaioTone = when (storage) {
    "READY" -> NiaioTone.Ok
    "ERROR" -> NiaioTone.Danger
    "ABSENT" -> NiaioTone.Warn
    else -> NiaioTone.Neutral
}

/** 合同写死了这条缓存的诚实说法，措辞不改。 */
private fun storageFootnote(sample: Snapshot?): String {
    val checked = sample?.storageChecked
    return if (checked == null) {
        "U盘状态为缓存；检查时间尚未确认"
    } else {
        "U盘状态为缓存；上次检查在该快照 " + ((sample.uptime - checked).coerceAtLeast(0) / 1000) + " 秒前"
    }
}
