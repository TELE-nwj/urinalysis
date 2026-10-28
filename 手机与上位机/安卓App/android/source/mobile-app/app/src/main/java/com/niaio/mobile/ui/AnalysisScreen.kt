package com.niaio.mobile.ui

import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.niaio.mobile.records.LocalRecord
import com.niaio.mobile.records.MinuteEntry
import com.niaio.mobile.records.RecordAggregation
import com.niaio.mobile.ui.theme.NiaioPalette
import kotlin.math.abs
import kotlin.math.max

private enum class AnalysisMetric(val label: String,val title: String,val unit: String) {
    VOLUME("尿量","新增尿量","mL"), RATE("速率","平均速率","mL/h"), DROPS("滴数","新增滴数","滴");
    fun value(row: MinuteEntry): Double? = when(this) { VOLUME -> row.volumeMl;RATE -> row.rateMlh;DROPS -> row.drops?.toDouble() }?.takeIf { it.isFinite() }
    fun display(row: MinuteEntry): String = (if(this==DROPS) row.drops?.toString() ?: "--" else decimal(value(row)))+" "+unit
}

@Composable
fun AnalysisScreen(record: LocalRecord,onBack: () -> Unit) {
    var hourly by rememberSaveable(record.id) { mutableStateOf(false) }
    var metricName by rememberSaveable(record.id) { mutableStateOf(AnalysisMetric.VOLUME.name) }
    val metric=AnalysisMetric.valueOf(metricName)
    var bars by rememberSaveable(record.id) { mutableStateOf(false) }
    var selected by rememberSaveable(record.id) { mutableIntStateOf(-1) }
    val rows=remember(record,hourly) { (if(hourly) RecordAggregation.hourly(record) else record.minutes).sortedBy { it.timeMs } }
    LaunchedEffect(hourly,metricName) { selected=-1 }
    Column(Modifier.fillMaxSize()) {
        PageHeading("数据分析",onBack)
        LazyColumn(contentPadding=PaddingValues(18.dp),verticalArrangement=Arrangement.spacedBy(10.dp)) {
            item { Text(record.name,style=MaterialTheme.typography.titleMedium,fontWeight=FontWeight.SemiBold);MutedText((if(record.startKnown) "开始 " else "首次接入 ")+fullTime(record.startedAtMs)) }
            item { AnalysisOptions(listOf("每分钟","每小时"),if(hourly) 1 else 0) {hourly=it==1} }
            item { AnalysisOptions(AnalysisMetric.entries.map { it.label },metric.ordinal) {metricName=AnalysisMetric.entries[it].name;if(it==AnalysisMetric.RATE.ordinal) bars=false} }
            if(metric!=AnalysisMetric.RATE) item { AnalysisOptions(listOf("折线图","柱状图"),if(bars) 1 else 0){bars=it==1} }
            item {
                Row(Modifier.fillMaxWidth(),verticalAlignment=Alignment.CenterVertically) { SectionTitle(metric.title,Modifier.weight(1f));MutedText(metric.unit) }
                MutedText(if(rows.isEmpty()) "暂无可分析数据" else bucketTime(rows.first().timeMs,hourly)+" 至 "+bucketTime(rows.last().timeMs,hourly),Modifier.padding(top=4.dp))
            }
            item {
                Surface(shape=MaterialTheme.shapes.medium,color=MaterialTheme.colorScheme.surfaceVariant) {
                    if(rows.none {metric.value(it)!=null}) EmptyMessage("暂无已接收的${metric.label}数据")
                    else DataChart(rows,metric,hourly,bars && metric!=AnalysisMetric.RATE,selected,onSelect={selected=it})
                }
            }
            item {
                val row=rows.getOrNull(selected)
                Text(if(row==null) "点击图中数据查看数值" else bucketTime(row.timeMs,hourly)+" · "+metric.display(row)+" · "+dataQuality(row,metric,hourly),color=MaterialTheme.colorScheme.primary,style=MaterialTheme.typography.bodyMedium)
                if(rows.isNotEmpty()) Row(horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick={selected=(selected-1).coerceAtLeast(0)},enabled=selected>0,modifier=Modifier.weight(1f).heightIn(min=48.dp)) {Text("上一时间")}
                    TextButton(onClick={selected=(selected+1).coerceAtMost(rows.lastIndex)},enabled=selected<rows.lastIndex,modifier=Modifier.weight(1f).heightIn(min=48.dp)) {Text("下一时间")}
                }
                MutedText("缺失数据留空，不补零、不跨缺口连线。"+(if(bars) "浅色描边柱表示部分区间。" else "空心点表示部分区间。")+(if(metric==AnalysisMetric.RATE) "速率按有效检测时长加权平均。" else ""))
            }
            item { SectionTitle("完整数据表",Modifier.padding(top=6.dp));Row(Modifier.fillMaxWidth().padding(top=10.dp)) {MutedText("时间",Modifier.weight(1.5f));MutedText(metric.title,Modifier.weight(1f));MutedText("数据状态",Modifier.weight(1f))};HorizontalDivider(Modifier.padding(top=8.dp)) }
            if(rows.isEmpty()) item {EmptyMessage("暂无数据")}
            itemsIndexed(rows,key={_,row ->row.timeMs}) { index,row ->
                Row(Modifier.fillMaxWidth().padding(vertical=8.dp),verticalAlignment=Alignment.CenterVertically) {
                    Text(bucketTime(row.timeMs,hourly),style=MaterialTheme.typography.bodySmall,modifier=Modifier.weight(1.5f))
                    Text(metric.display(row),style=MaterialTheme.typography.bodySmall,fontWeight=FontWeight.SemiBold,modifier=Modifier.weight(1f))
                    Text(dataQuality(row,metric,hourly),style=MaterialTheme.typography.bodySmall,modifier=Modifier.weight(1f),color=if(index==selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                }
                HorizontalDivider()
            }
        }
    }
}

private fun dataQuality(row: MinuteEntry,metric: AnalysisMetric,hourly: Boolean): String = when { metric.value(row)==null -> "缺失";row.partial -> if(hourly) "部分小时" else "部分分钟";else -> "已接收" }

@Composable
private fun AnalysisOptions(labels: List<String>,selected: Int,onSelect: (Int) -> Unit) {
    Surface(color=MaterialTheme.colorScheme.surfaceVariant,shape=MaterialTheme.shapes.medium) {
        Row(Modifier.fillMaxWidth().padding(4.dp),horizontalArrangement=Arrangement.spacedBy(4.dp)) {
            labels.forEachIndexed { index,label ->
                TextButton(onClick={onSelect(index)},modifier=Modifier.weight(1f).heightIn(min=48.dp),shape=MaterialTheme.shapes.small,
                    colors=ButtonDefaults.textButtonColors(containerColor=if(selected==index) NiaioPalette.BlueWash else Color.Transparent,contentColor=if(selected==index) NiaioPalette.BlueDeep else MaterialTheme.colorScheme.onSurfaceVariant)) {Text(label,fontWeight=FontWeight.SemiBold)}
            }
        }
    }
}

/** Nullable and nonadjacent bins explicitly break lines; data values are never synthesized. */
@Composable
private fun DataChart(rows: List<MinuteEntry>,metric: AnalysisMetric,hourly: Boolean,bars: Boolean,selected: Int,onSelect: (Int) -> Unit) {
    val ink=MaterialTheme.colorScheme.onSurfaceVariant
    val grid=MaterialTheme.colorScheme.outlineVariant
    val blue=MaterialTheme.colorScheme.primary
    val paper=MaterialTheme.colorScheme.surfaceVariant
    Canvas(Modifier.fillMaxWidth().height(240.dp).padding(8.dp)
        .semantics { contentDescription="${if(hourly) "每小时" else "每分钟"}${metric.title}${if(bars) "柱状图" else "折线图"}。下方提供完整数据表和时间选择。" }
        .pointerInput(rows,metric,hourly,bars) {
            detectTapGestures { tap ->
                val left=44.dp.toPx();val right=16.dp.toPx();val inset=if(bars) 18.dp.toPx() else 8.dp.toPx()
                val usable=(size.width-left-right-inset*2).coerceAtLeast(1f)
                val first=rows.first().timeMs;val span=(rows.last().timeMs-first).coerceAtLeast(1)
                val target=first.toDouble()+((tap.x-left-inset)/usable).coerceIn(0f,1f).toDouble()*span.toDouble()
                onSelect(rows.indices.minByOrNull {abs(rows[it].timeMs-target)} ?: 0)
            }
        }) {
        val left=44.dp.toPx();val right=16.dp.toPx();val top=18.dp.toPx();val bottom=36.dp.toPx()
        val inset=if(bars) 18.dp.toPx() else 8.dp.toPx()
        val chartWidth=(size.width-left-right-inset*2).coerceAtLeast(1f)
        val chartHeight=(size.height-top-bottom).coerceAtLeast(1f)
        val baseline=top+chartHeight
        val maximum=max(rows.mapNotNull{metric.value(it)}.maxOrNull() ?: 0.0,1.0)*1.15
        val span=(rows.last().timeMs-rows.first().timeMs).coerceAtLeast(1)
        fun x(index: Int)=if(rows.size==1) left+inset+chartWidth/2 else left+inset+(rows[index].timeMs-rows.first().timeMs).toFloat()/span*chartWidth
        fun y(value: Double)=baseline-(value/maximum*chartHeight).toFloat()
        val paint=Paint(Paint.ANTI_ALIAS_FLAG).apply { color=ink.toArgb();textSize=11.dp.toPx() }
        repeat(4) { step ->
            val value=maximum*step/3;val yy=y(value)
            drawLine(grid,Offset(left,yy),Offset(size.width-right,yy),1.dp.toPx())
            paint.textAlign=Paint.Align.RIGHT
            drawContext.canvas.nativeCanvas.drawText(if(metric==AnalysisMetric.DROPS) value.toLong().toString() else decimal(value),left-6.dp.toPx(),yy+4.dp.toPx(),paint)
        }
        val period=if(hourly) 3_600_000L else 60_000L
        var previous: Int?=null
        rows.forEachIndexed { index,row ->
            val value=metric.value(row)
            if(value==null) {previous=null;return@forEachIndexed}
            val point=Offset(x(index),y(value))
            if(bars) {
                val width=minOf(30.dp.toPx(),chartWidth/max(rows.size,1)*0.65f).coerceAtLeast(2.dp.toPx())
                val height=(baseline-point.y).coerceAtLeast(1.dp.toPx())
                drawRect(if(row.partial) blue.copy(alpha=0.3f) else blue,Offset(point.x-width/2,point.y),Size(width,height))
                if(row.partial) drawRect(blue,Offset(point.x-width/2,point.y),Size(width,height),style=Stroke(1.dp.toPx()))
            } else {
                previous?.let { before -> if(row.timeMs-rows[before].timeMs==period) drawLine(blue,Offset(x(before),y(metric.value(rows[before])!!)),point,2.dp.toPx()) }
                drawCircle(if(row.partial) paper else blue,3.dp.toPx(),point)
                if(row.partial) drawCircle(blue,3.dp.toPx(),point,style=Stroke(1.5.dp.toPx()))
            }
            if(index==selected) {drawLine(ink.copy(alpha=0.5f),Offset(point.x,top),Offset(point.x,baseline),1.dp.toPx());drawCircle(blue,6.dp.toPx(),point,style=Stroke(2.dp.toPx()))}
            previous=index
        }
        paint.textAlign=Paint.Align.CENTER
        listOf(0,rows.size/2,rows.lastIndex).distinct().forEach { index -> drawContext.canvas.nativeCanvas.drawText(shortTime(rows[index].timeMs),x(index),size.height-10.dp.toPx(),paint) }
    }
}
