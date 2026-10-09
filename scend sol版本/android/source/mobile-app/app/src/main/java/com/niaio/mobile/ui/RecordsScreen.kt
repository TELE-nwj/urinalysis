package com.niaio.mobile.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.niaio.mobile.DisplayFormat
import com.niaio.mobile.ProbeState
import com.niaio.mobile.records.*
import com.niaio.mobile.ui.theme.NiaioPalette

@Composable
fun RecordsScreen(probe: ProbeState,book: RecordBookState,actions: AppActions,onOpen: (Long) -> Unit,persistenceStatus: String? = null,notificationStatus: String? = null) {
    var clearDialog by remember { mutableStateOf(false) }
    var renameId by remember { mutableStateOf<Long?>(null) }
    var deleteRecordId by remember { mutableStateOf<Long?>(null) }
    var deleteAlarmId by remember { mutableStateOf<Long?>(null) }
    var nowMs by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) { while(true) { kotlinx.coroutines.delay(30_000L); nowMs=System.currentTimeMillis() } }
    val recent=book.alarms.filter { it.timeMs >= nowMs-86_400_000L && it.timeMs<=nowMs }.sortedByDescending { it.timeMs }
    Column(Modifier.fillMaxSize()) {
        PageHeading("记录")
        LazyColumn(Modifier.weight(1f),contentPadding=PaddingValues(start=18.dp,end=18.dp,bottom=16.dp),verticalArrangement=Arrangement.spacedBy(10.dp)) {
            if(persistenceStatus != null) item { Surface(color=NiaioPalette.DangerWash,shape=MaterialTheme.shapes.medium) { Text(persistenceStatus,Modifier.fillMaxWidth().padding(12.dp),color=NiaioPalette.Danger) } }
            item { StorageSummary(probe) }
            item { SectionTitle("最近 24 小时警报",Modifier.padding(top=6.dp)) }
            if(notificationStatus != null) item { MutedText(notificationStatus) }
            item {
                Surface(shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant) {
                    if(recent.isEmpty()) EmptyMessage("最近 24 小时暂无已记录警报")
                    else LazyColumn(Modifier.fillMaxWidth().heightIn(max=240.dp)) {
                        items(recent,key={it.id}) { alarm -> AlarmRow(alarm,book.records.firstOrNull{it.id==alarm.recordId}?.name ?: alarm.recordName,onDelete={deleteAlarmId=alarm.id}) }
                    }
                }
            }
            item { SectionTitle("检测记录",Modifier.padding(top=8.dp)) }
            if(book.records.isEmpty()) item { EmptyMessage("暂无手机检测记录\n连接设备后，接收到的检测数据将保存在这里") }
            items(book.records.sortedByDescending { it.id },key={it.id}) { record ->
                RecordCard(record,onOpen={onOpen(record.id)},onRename={renameId=record.id},onDelete={deleteRecordId=record.id},paused=record.status=="MEASURING" && probe.session.snapshot?.state=="PAUSED")
            }
        }
        HorizontalDivider()
        Row(Modifier.fillMaxWidth().padding(horizontal=18.dp,vertical=8.dp),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
            SecondaryAction("导出记录",actions.onExport,Modifier.weight(1f),enabled=book.records.isNotEmpty() || book.alarms.isNotEmpty())
            TextButton(onClick={clearDialog=true},modifier=Modifier.weight(1f).heightIn(min=52.dp),enabled=book.records.isNotEmpty() || book.alarms.isNotEmpty(),colors=ButtonDefaults.textButtonColors(contentColor=NiaioPalette.Danger)) { Text("清除所有记录") }
        }
    }
    if(clearDialog) ConfirmDialog("清除所有记录","清除手机中的全部检测记录（含当前记录）、分钟数据和警报？设备继续检测，首页读数与 U 盘文件不受影响。","确认清除",{clearDialog=false},{clearDialog=false;actions.onClearAll()},dangerous=true)
    renameId?.let { id -> book.records.firstOrNull{it.id==id}?.let { RenameDialog(it,onDismiss={renameId=null},onSave={name ->renameId=null;actions.onRename(id,name)}) } }
    deleteRecordId?.let { id -> book.records.firstOrNull{it.id==id}?.let { record ->
        ConfirmDialog("删除记录","删除手机中的“${record.name}”及分钟数据？警报独立保留，U 盘文件不受影响。","删除",{deleteRecordId=null},{deleteRecordId=null;if(record.status!="MEASURING") actions.onDeleteRecord(id)},dangerous=true)
    } }
    deleteAlarmId?.let { id -> ConfirmDialog("删除警报","删除该手机警报？关联记录中的历史警报数量将同步更新。这不表示设备异常恢复，U 盘文件不受影响。","删除",{deleteAlarmId=null},{deleteAlarmId=null;actions.onDeleteAlarm(id)},dangerous=true) }
}

@Composable
private fun StorageSummary(probe: ProbeState) {
    val sample=probe.session.snapshot.takeUnless { probe.simulated || probe.session.simulated }
    val state=sample?.storage
    Surface(shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant) {
        Row(Modifier.padding(12.dp),verticalAlignment=Alignment.CenterVertically) {
            Icon(NiaioIcons.Storage,null,Modifier.size(24.dp),tint=MaterialTheme.colorScheme.primary)
            Column(Modifier.padding(start=10.dp).weight(1f)) {
                Text((if(sample!=null && (!probe.connected || probe.session.stale)) "上次：" else "")+when(state) { "READY" -> "U 盘可写入"; "ABSENT" -> "U 盘未连接 · 记录未保存"; "ERROR" -> "U 盘写入失败 · 记录未保存"; else -> "U 盘状态未知" },fontWeight=FontWeight.SemiBold)
                MutedText(if(!probe.connected || probe.session.stale) "等待设备连接后检查缓存状态" else if(sample?.storageChecked==null) "尚未收到缓存检查结果" else "缓存检查：${((sample.uptime-sample.storageChecked).coerceAtLeast(0))/1000} 秒前 · 手机记录独立保存")
            }
        }
    }
}

@Composable
private fun RecordCard(record: LocalRecord,onOpen: () -> Unit,onRename: () -> Unit,onDelete: () -> Unit,paused: Boolean) {
    var menu by remember(record.id) { mutableStateOf(false) }
    Box(Modifier.fillMaxWidth()) {
        Surface(onClick=onOpen,shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant,modifier=Modifier.fillMaxWidth()) {
            Column(Modifier.padding(start=16.dp,end=66.dp,top=16.dp,bottom=14.dp)) {
                Text(record.name,style=MaterialTheme.typography.titleMedium,fontWeight=FontWeight.SemiBold)
                MutedText((if(record.startKnown) "开始 " else "首次接入 ")+fullTime(record.startedAtMs),Modifier.padding(top=6.dp))
                Text(recordStatus(record.status)+(if(paused) " · 已暂停" else ""),Modifier.padding(top=6.dp),style=MaterialTheme.typography.labelMedium,color=when(record.status) { "ENDED" -> NiaioPalette.Ok;"MEASURING" -> MaterialTheme.colorScheme.primary;else -> NiaioPalette.Warn })
            }
        }
        Box(Modifier.align(Alignment.TopEnd).padding(8.dp)) {
            IconButton(onClick={menu=true},modifier=Modifier.size(48.dp)) { Icon(AppIcons.More,"修改 ${record.name}") }
            DropdownMenu(expanded=menu,onDismissRequest={menu=false}) {
                DropdownMenuItem(text={Text("重命名")},onClick={menu=false;onRename()})
                DropdownMenuItem(text={Text(if(record.status=="MEASURING") "当前记录尚未结束，无法删除" else "删除记录")},enabled=record.status!="MEASURING",onClick={menu=false;onDelete()})
            }
        }
    }
}

@Composable
private fun RenameDialog(record: LocalRecord,onDismiss: () -> Unit,onSave: (String) -> Unit) {
    var name by remember(record.id) { mutableStateOf(record.name) }
    val trimmed=name.trim()
    AlertDialog(onDismissRequest=onDismiss,title={Text("重命名记录")},text={Column {
        MutedText("编号和开始时间保持不变，关联警报名称同步更新。")
        OutlinedTextField(value=name,onValueChange={name=it.take(32)},label={Text("记录名称")},supportingText={Text(if(trimmed.isEmpty()) "请填写名称" else "${name.length}/32")},isError=trimmed.isEmpty(),singleLine=false,modifier=Modifier.fillMaxWidth().padding(top=12.dp))
    }},confirmButton={TextButton(onClick={onSave(trimmed)},enabled=trimmed.isNotEmpty(),modifier=Modifier.heightIn(min=48.dp)){Text("保存")}},dismissButton={TextButton(onClick=onDismiss,modifier=Modifier.heightIn(min=48.dp)){Text("返回")}})
}

@Composable
internal fun AlarmRow(alarm: LocalAlarm,name: String,onDelete: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(start=12.dp,top=10.dp,bottom=10.dp,end=4.dp),verticalAlignment=Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text("${fullTime(alarm.timeMs)} · ${alarmReason(alarm.code)}",fontWeight=FontWeight.SemiBold,style=MaterialTheme.typography.bodyMedium,color=NiaioPalette.Danger)
            MutedText(name,Modifier.padding(top=3.dp))
            MutedText(alarm.endedAtMs?.let { "恢复 ${fullTime(it)}" } ?: "恢复状态未确认",Modifier.padding(top=3.dp))
        }
        TextButton(onClick=onDelete,modifier=Modifier.heightIn(min=48.dp),colors=ButtonDefaults.textButtonColors(contentColor=NiaioPalette.Danger)) { Text("删除") }
    }
    HorizontalDivider()
}

@Composable
internal fun RecordDetailScreen(record: LocalRecord?,book: RecordBookState,actions: AppActions,onBack: () -> Unit,onAnalysis: () -> Unit) {
    var alarmsDialog by remember { mutableStateOf(false) }
    var deleteAlarmId by remember { mutableStateOf<Long?>(null) }
    val alarms=book.alarms.filter {it.recordId==record?.id}.sortedByDescending{it.timeMs}
    Column(Modifier.fillMaxSize()) {
        PageHeading("记录详情",onBack)
        if(record==null) EmptyMessage("该记录已删除，或尚未加载完成")
        else LazyColumn(contentPadding=PaddingValues(18.dp),verticalArrangement=Arrangement.spacedBy(12.dp)) {
            item { Text(record.name,style=MaterialTheme.typography.titleLarge,fontWeight=FontWeight.SemiBold) }
            item { MutedText((if(record.startKnown) "开始 " else "首次接入 ")+fullTime(record.startedAtMs)+"\n"+recordStatus(record.status)) }
            item { Surface(shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant) {
                Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(14.dp)) {
                    Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) {
                        DetailMetric("累计尿量",decimal(record.totalMl)+" mL",Modifier.weight(1f))
                        DetailMetric("累计滴数","${record.totalDrops} 滴",Modifier.weight(1f))
                    }
                    Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) {
                        DetailMetric("有效检测时间",DisplayFormat.elapsed(record.elapsedSeconds),Modifier.weight(1f))
                        Column(Modifier.weight(1f)) { Text("历史警报",color=NiaioPalette.Danger,style=MaterialTheme.typography.bodySmall);Text("${alarms.size} 次",color=NiaioPalette.Danger,style=MaterialTheme.typography.titleLarge,fontWeight=FontWeight.SemiBold) }
                    }
                }
            } }
            item { Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                Button(onClick={alarmsDialog=true},modifier=Modifier.weight(1f).heightIn(min=52.dp),shape=MaterialTheme.shapes.medium,colors=ButtonDefaults.buttonColors(containerColor=NiaioPalette.DangerWash,contentColor=NiaioPalette.Danger)) { Text("查看警报") }
                PrimaryAction("数据分析",onAnalysis,Modifier.weight(1f),icon=AppIcons.Chart)
            } }
            item { SectionTitle("每分钟记录");MutedText("按自然分钟统计，缺失数据以 -- 表示") }
            if(record.minutes.isEmpty()) item { EmptyMessage("尚无分钟记录") }
            items(record.minutes,key={it.timeMs}) { MinuteRow(it) }
        }
    }
    if(alarmsDialog && record!=null) AlertDialog(onDismissRequest={alarmsDialog=false},title={Text("本次检测警报")},text={Column {
        Text(record.name)
        if(alarms.isEmpty()) EmptyMessage("本次检测暂无已记录警报") else LazyColumn(Modifier.heightIn(max=360.dp)) {items(alarms,key={it.id}) { AlarmRow(it,record.name,onDelete={deleteAlarmId=it.id}) } }
    }},confirmButton={TextButton(onClick={alarmsDialog=false},modifier=Modifier.heightIn(min=48.dp)){Text("返回")}})
    deleteAlarmId?.let { id -> ConfirmDialog("删除警报","删除该手机警报？历史警报数量同步更新，不改变设备异常状态及 U 盘文件。","删除",{deleteAlarmId=null},{deleteAlarmId=null;actions.onDeleteAlarm(id)},dangerous=true) }
}

@Composable
private fun DetailMetric(label: String,value: String,modifier: Modifier) { Column(modifier) {MutedText(label);Text(value,style=MaterialTheme.typography.titleLarge,fontWeight=FontWeight.SemiBold)} }

@Composable
private fun MinuteRow(entry: MinuteEntry) {
    var expanded by remember(entry.timeMs) { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().heightIn(min=48.dp).clickable{expanded=!expanded},verticalAlignment=Alignment.CenterVertically) {
            Text(bucketTime(entry.timeMs)+" – "+shortTime(entry.timeMs+60_000),Modifier.weight(1f),style=MaterialTheme.typography.bodyMedium,fontWeight=FontWeight.SemiBold)
            Text(if(expanded) "收起累计" else "展开累计",color=MaterialTheme.colorScheme.primary,style=MaterialTheme.typography.labelMedium)
        }
        Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
            MinuteMetric("新增尿量",decimal(entry.volumeMl)+" mL",Modifier.weight(1f))
            MinuteMetric("新增滴数",(entry.drops?.toString() ?: "--")+" 滴",Modifier.weight(1f))
            MinuteMetric("平均速率",decimal(entry.rateMlh)+" mL/h",Modifier.weight(1f))
        }
        if(entry.partial || entry.volumeMl==null) MutedText(if(entry.volumeMl==null) "数据缺失" else "部分分钟 · 有效 ${decimal(entry.coverageSeconds)} 秒",Modifier.padding(top=8.dp))
        if(expanded) Surface(color=MaterialTheme.colorScheme.surfaceVariant,shape=MaterialTheme.shapes.small,modifier=Modifier.fillMaxWidth().padding(top=8.dp)) {
            Text("累计尿量 ${decimal(entry.cumulativeMl)} mL  ·  累计滴数 ${entry.cumulativeDrops ?: "--"} 滴",Modifier.padding(10.dp),style=MaterialTheme.typography.bodySmall)
        }
        HorizontalDivider(Modifier.padding(top=12.dp))
    }
}

@Composable
private fun MinuteMetric(label: String,value: String,modifier: Modifier) { Column(modifier) {MutedText(label);Text(value,style=MaterialTheme.typography.bodyMedium,fontWeight=FontWeight.SemiBold)} }
