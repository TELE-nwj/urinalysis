package com.niaio.mobile.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.niaio.mobile.DisplayFormat
import com.niaio.mobile.ProbeState
import com.niaio.mobile.records.RecordBookState
import com.niaio.mobile.ui.theme.NiaioPalette
import com.niaio.mobile.ui.theme.TabularNumerals

data class AppActions(
    val onConnect: () -> Unit, val onDisconnect: () -> Unit, val onCommand: (String) -> Unit,
    val onWifiSettings: () -> Unit, val onNotificationSettings: () -> Unit, val onClearLog: () -> Unit,
    val onRename: (Long, String) -> Unit, val onDeleteRecord: (Long) -> Unit,
    val onDeleteAlarm: (Long) -> Unit, val onClearAll: () -> Unit,
    val onExport: () -> Unit, val onRecordOpened: () -> Unit,
)

/** Activity owns all mutations; navigation and protected-focus dialogs stay in the UI. */
@Composable
fun AppScreen(probe: ProbeState, book: RecordBookState, requestedRecordId: Long?, actions: AppActions, persistenceStatus: String? = null, notificationStatus: String? = null) {
    var page by rememberSaveable { mutableIntStateOf(0) }
    var recordId by rememberSaveable { mutableStateOf<Long?>(null) }
    var analysis by rememberSaveable { mutableStateOf(false) }
    var settingsPage by rememberSaveable { mutableStateOf<String?>(null) }
    LaunchedEffect(requestedRecordId) {
        if(requestedRecordId != null) { page=1;recordId=requestedRecordId;analysis=false;actions.onRecordOpened() }
    }
    val back: () -> Unit = { when { analysis -> analysis=false; recordId != null -> recordId=null; settingsPage != null -> settingsPage=null; else -> page=0 } }
    BackHandler(enabled=page != 0 || recordId != null || settingsPage != null,onBack=back)
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).padding(WindowInsets.safeDrawing.asPaddingValues())) {
        Box(Modifier.weight(1f)) {
            when(page) {
                0 -> DetectionScreen(probe,actions)
                1 -> {
                    val selected=book.records.firstOrNull { it.id==recordId }
                    if(recordId == null) RecordsScreen(probe,book,actions,onOpen={ recordId=it;analysis=false },persistenceStatus=persistenceStatus,notificationStatus=notificationStatus)
                    else if(analysis && selected != null) AnalysisScreen(selected,onBack={ analysis=false })
                    else RecordDetailScreen(selected,book,actions,onBack={ recordId=null;analysis=false },onAnalysis={ analysis=true })
                }
                else -> SettingsScreen(probe,actions,settingsPage,onPage={ settingsPage=it },onBack={ settingsPage=null })
            }
        }
        HorizontalDivider(color=MaterialTheme.colorScheme.outlineVariant)
        NavigationBar(containerColor=NiaioPalette.PaperSunken, tonalElevation=0.dp) {
            val labels=listOf("检测","记录","设置")
            val icons=listOf(NiaioIcons.Drop,AppIcons.List,NiaioIcons.Settings)
            labels.forEachIndexed { index,label -> NavigationBarItem(selected=page==index,
                onClick={page=index;recordId=null;analysis=false;settingsPage=null},
                icon={Icon(icons[index],label)},label={Text(label)}) }
        }
    }
}

@Composable
private fun DetectionScreen(probe: ProbeState, actions: AppActions) {
    val session=probe.session
    val simulated=probe.simulated || session.simulated
    // A simulated snapshot is not a product reading; this surface has no demo entry.
    val sample=session.snapshot.takeUnless { simulated }
    val live=probe.connected && session.ready && !session.stale && !simulated
    val controls=live && !session.waiting
    var connectDialog by remember { mutableStateOf(false) }
    var resetDialog by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().padding(horizontal=18.dp)) {
        Row(Modifier.fillMaxWidth().padding(top=12.dp,bottom=10.dp),verticalAlignment=Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("尿量监测仪",style=MaterialTheme.typography.titleLarge,fontWeight=FontWeight.SemiBold)
                Spacer(Modifier.height(4.dp))
                MutedText(session.lastUpdateHms?.let { "最后数据 $it" } ?: "等待设备数据")
            }
            Column(horizontalAlignment=Alignment.End) {
                Surface(shape=MaterialTheme.shapes.medium,color=if(live) NiaioPalette.OkWash else NiaioPalette.WarnWash) {
                    Text(when { probe.connecting -> "连接中"; !probe.connected -> "Wi-Fi 未连接"; live -> "已连接"; else -> "等待新数据" },
                        Modifier.padding(horizontal=10.dp,vertical=6.dp),color=if(live) NiaioPalette.Ok else NiaioPalette.Warn,fontSize=14.sp,fontWeight=FontWeight.SemiBold)
                }
                Row(Modifier.padding(top=5.dp),verticalAlignment=Alignment.CenterVertically) {
                    Icon(NiaioIcons.Battery,null,Modifier.size(18.dp),tint=MaterialTheme.colorScheme.onSurfaceVariant)
                    Text((if(live) "电量 " else "上次电量 ")+DisplayFormat.battery(sample?.battery),fontSize=14.sp,color=MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        if(!live && sample != null) Surface(color=NiaioPalette.WarnWash,shape=MaterialTheme.shapes.small) {
            Text(if(probe.connected) "数据暂未更新 · 以下为最后数据" else "Wi-Fi 未连接 · 以下为最后数据",Modifier.fillMaxWidth().padding(10.dp),fontSize=12.sp,color=NiaioPalette.Warn)
        }
        Column(Modifier.fillMaxWidth().padding(top=14.dp,bottom=12.dp)) {
            Row(Modifier.fillMaxWidth(),verticalAlignment=Alignment.CenterVertically) {
                Text("累计尿量",Modifier.weight(1f),color=MaterialTheme.colorScheme.onSurfaceVariant)
                MutedText(when(sample?.state) { "MEASURING" -> "检测中"; "PAUSED" -> "已暂停"; "IDLE" -> "待开始"; else -> "等待数据" })
            }
            Row(verticalAlignment=Alignment.CenterVertically) {
                Text(decimal(sample?.totalMl),style=MaterialTheme.typography.displayLarge.merge(TabularNumerals).copy(fontSize=58.sp,lineHeight=65.sp),fontWeight=FontWeight.SemiBold)
                Text("mL",Modifier.padding(start=8.dp,top=18.dp),fontSize=20.sp,color=MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Text("有效检测时间  "+(sample?.elapsed?.let(DisplayFormat::elapsed) ?: "--"),style=MaterialTheme.typography.bodyMedium.merge(TabularNumerals))
        }
        HorizontalDivider()
        Row(Modifier.fillMaxWidth().padding(vertical=12.dp),horizontalArrangement=Arrangement.spacedBy(18.dp)) {
            DetectionMetric("实时流速",decimal(sample?.rateMlh),"mL/h",Modifier.weight(1f))
            DetectionMetric("累计滴数",sample?.drops?.toString() ?: "--","滴",Modifier.weight(1f))
        }
        HorizontalDivider()
        val normal=live && sample?.alert=="A"
        val abnormal=live && sample?.alert in listOf("B","C","D","E")
        val alarmInk=if(normal) NiaioPalette.Ok else if(abnormal) NiaioPalette.Danger else NiaioPalette.Warn
        Surface(Modifier.fillMaxWidth().padding(top=10.dp),shape=MaterialTheme.shapes.medium,color=if(normal) NiaioPalette.OkWash else if(abnormal) NiaioPalette.DangerWash else NiaioPalette.WarnWash) {
            Row(Modifier.padding(12.dp),verticalAlignment=Alignment.CenterVertically) {
                Icon(if(normal) NiaioIcons.Check else NiaioIcons.Warning,null,Modifier.size(20.dp),tint=alarmInk)
                Text(if(live && sample != null) "当前报警：${alarmReason(sample.alert)}" else "当前报警状态未知",Modifier.padding(start=8.dp),fontWeight=FontWeight.SemiBold,color=alarmInk)
            }
        }
        // Reserve the bottom for actions; the readout never expands to fill blank space.
        Spacer(Modifier.weight(1f))
        val note=session.protocolError ?: session.control.takeIf { it.isNotBlank() }
        if(note != null) MutedText(note,Modifier.padding(top=6.dp,bottom=5.dp))
        val commandLabel=when(sample?.state) { "MEASURING" -> "暂停检测"; "PAUSED" -> "继续检测"; else -> "开始检测" }
        PrimaryAction(commandLabel,onClick={ actions.onCommand(if(sample?.state=="MEASURING") "pause" else "start") },enabled=controls,icon=if(sample?.state=="MEASURING") NiaioIcons.Pause else NiaioIcons.Drop)
        Spacer(Modifier.height(7.dp))
        SecondaryAction("复位检测",onClick={resetDialog=true},enabled=controls,icon=NiaioIcons.Restart)
        Row(Modifier.fillMaxWidth().padding(top=7.dp,bottom=10.dp),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
            SecondaryAction("快速连接",{connectDialog=true},Modifier.weight(1f),enabled=!probe.connecting,icon=NiaioIcons.Wifi)
            SecondaryAction("断开连接",actions.onDisconnect,Modifier.weight(1f),enabled=probe.connected || probe.connecting)
        }
    }
    if(connectDialog) QuickConnectDialog(onDismiss={connectDialog=false},onConnect={connectDialog=false;actions.onConnect()})
    if(resetDialog) ConfirmDialog("复位检测","结束本次检测并清零设备当前读数。已保存的手机记录、警报与 U 盘记录保留。","确认复位",{resetDialog=false},{resetDialog=false;if(controls) actions.onCommand("reset")})
}

@Composable
private fun DetectionMetric(label: String,value: String,unit: String,modifier: Modifier) {
    Column(modifier) {
        Text(label,color=MaterialTheme.colorScheme.onSurfaceVariant,fontSize=14.sp)
        Row(verticalAlignment=Alignment.CenterVertically) {
            Text(value,style=MaterialTheme.typography.headlineLarge.merge(TabularNumerals),fontWeight=FontWeight.SemiBold,modifier=Modifier.weight(1f,fill=false))
            Text(unit,Modifier.padding(start=4.dp),fontSize=14.sp,color=MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
internal fun QuickConnectDialog(onDismiss: () -> Unit,onConnect: () -> Unit) {
    ConfirmDialog("快速连接","请连接好 Wi-Fi\n\n连接设备热点 NIAIO-Monitor 后，继续连接。","继续连接",onDismiss,onConnect)
}
