package com.niaio.mobile.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.niaio.mobile.BuildConfig
import com.niaio.mobile.ProbeState

@Composable
fun SettingsScreen(probe: ProbeState,actions: AppActions,page: String?,onPage: (String) -> Unit,onBack: () -> Unit) {
    var connectDialog by remember { mutableStateOf(false) }
    var updateDialog by remember { mutableStateOf<String?>(null) }
    var clearDialog by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize()) {
        PageHeading(when(page) {"connection" ->"设备连接";"info" ->"设备信息";"version" ->"版本与版本更新";"logs" ->"通讯记录";else ->"设置"},if(page!=null) onBack else null)
        LazyColumn(contentPadding=PaddingValues(18.dp),verticalArrangement=Arrangement.spacedBy(12.dp)) {
            when(page) {
                null -> {
                    item {SettingsEntry("设备连接","连接设备热点与通知权限",NiaioIcons.Wifi){onPage("connection")}}
                    item {SettingsEntry("设备信息","设备名称、时钟与会话信息",NiaioIcons.Info){onPage("info")}}
                    item {SettingsEntry("版本与版本更新","APP 与设备固件",NiaioIcons.Settings){onPage("version")}}
                    item {SettingsEntry("通讯记录","查看本机收发记录",AppIcons.List){onPage("logs")}}
                }
                "connection" -> {
                    item {SectionTitle("连接设备热点")}
                    item {InfoCard {KeyValueRow("设备热点","NIAIO-Monitor",showDivider=false);KeyValueRow("连接结果",if(probe.connected) "已连接" else "Wi-Fi 未连接")}}
                    item {MutedText("请先在手机系统设置中连接设备 Wi-Fi；提示无互联网时，请保持连接。")}
                    item {SecondaryAction("打开 Wi-Fi 设置",actions.onWifiSettings,icon=NiaioIcons.Wifi)}
                    item {PrimaryAction("快速连接",{connectDialog=true},enabled=!probe.connecting)}
                    item {SecondaryAction("断开连接",actions.onDisconnect,enabled=probe.connected || probe.connecting)}
                    item {SectionTitle("异常通知",Modifier.padding(top=12.dp));MutedText("新警报通过状态栏提醒，同一持续异常只提醒一次。手机断线期间无法接收新警报。")}
                    item {SecondaryAction("查看通知权限",actions.onNotificationSettings)}
                }
                "info" -> {
                    item {InfoCard {
                        KeyValueRow("设备名称",probe.session.deviceId ?: "未知",showDivider=false)
                        KeyValueRow("设备地址","192.168.4.1")
                        KeyValueRow("设备时钟",probe.session.snapshot?.clock ?: "--")
                        KeyValueRow("对时结果",probe.session.sync)
                        KeyValueRow("启动标识",probe.session.bootId ?: "--")
                        KeyValueRow("会话标识",probe.session.sessionId ?: "--")
                    }}
                    item {MutedText("设备时钟由手机连接后发送一次时分秒；之后由设备计时。")}
                }
                "version" -> {
                    item {InfoCard {KeyValueRow("APP 版本",BuildConfig.VERSION_NAME,showDivider=false);KeyValueRow("设备固件版本","未知")}}
                    item {SettingsEntry("APP 更新","当前版本 ${BuildConfig.VERSION_NAME}",NiaioIcons.Info){updateDialog="APP 更新"}}
                    item {SettingsEntry("设备固件更新","固件版本未知",NiaioIcons.Settings){updateDialog="设备固件更新"}}
                }
                "logs" -> {
                    item {Row(Modifier.fillMaxWidth(),verticalAlignment=Alignment.CenterVertically) {SectionTitle("本机通讯记录",Modifier.weight(1f));TextButton(onClick={clearDialog=true},enabled=probe.log.isNotEmpty(),modifier=Modifier.heightIn(min=48.dp)){Text("清空")}}}
                    if(probe.log.isEmpty()) item {EmptyMessage("暂无通讯记录")}
                    items(probe.log) {entry ->SelectionContainer {Text(entry,style=MaterialTheme.typography.bodySmall,modifier=Modifier.fillMaxWidth().padding(vertical=8.dp))};HorizontalDivider()}
                }
            }
        }
    }
    if(connectDialog) QuickConnectDialog({connectDialog=false},{connectDialog=false;actions.onConnect()})
    updateDialog?.let {title ->AlertDialog(onDismissRequest={updateDialog=null},title={Text(title)},text={Text("此版本尚未开放${title}。")},confirmButton={TextButton(onClick={updateDialog=null},modifier=Modifier.heightIn(min=48.dp)){Text("返回")}})}
    if(clearDialog) ConfirmDialog("清空通讯记录","清空本机通讯记录？检测记录和警报保留。","清空",{clearDialog=false},{clearDialog=false;actions.onClearLog()})
}

@Composable
private fun SettingsEntry(title: String,detail: String,icon: ImageVector,onClick: () -> Unit) {
    Surface(onClick=onClick,shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant,modifier=Modifier.fillMaxWidth()) {
        Row(Modifier.padding(14.dp).heightIn(min=48.dp),verticalAlignment=Alignment.CenterVertically) {
            Icon(icon,null,Modifier.size(24.dp),tint=MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f).padding(horizontal=12.dp)) {Text(title,fontWeight=FontWeight.SemiBold,style=MaterialTheme.typography.titleMedium);MutedText(detail,Modifier.padding(top=4.dp))}
            Icon(AppIcons.Right,null,Modifier.size(20.dp),tint=MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
