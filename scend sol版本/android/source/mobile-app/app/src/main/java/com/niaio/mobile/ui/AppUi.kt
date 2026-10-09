package com.niaio.mobile.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.niaio.mobile.ui.theme.NiaioPalette
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

internal object AppIcons {
    val Back = vector("Back", "M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z")
    val More = vector("More", "M12 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm0 2a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0 6a2 2 0 1 0 0 4 2 2 0 0 0 0-4z")
    val List = vector("List", "M3 5h18v2H3V5zm0 6h18v2H3v-2zm0 6h18v2H3v-2z")
    val Right = vector("Right", "M9.29 6.71 14.58 12l-5.29 5.29L10.7 18.7 17.4 12l-6.7-6.7z")
    val Chart = vector("Chart", "M3 3h2v16h16v2H3V3zm4 12 4-5 4 3 5-7 1.6 1.2-6.2 8.7-4.1-3.1-2.7 3.5L7 15z")
    private fun vector(name: String, path: String): ImageVector = ImageVector.Builder(name,24.dp,24.dp,24f,24f).apply {
        addPath(addPathNodes(path), fill = SolidColor(Color.Black))
    }.build()
}

internal fun fullTime(timeMs: Long): String = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.CHINA).format(Date(timeMs))
internal fun bucketTime(timeMs: Long, hourly: Boolean = false): String = SimpleDateFormat(if (hourly) "yyyy-MM-dd HH:00" else "yyyy-MM-dd HH:mm", Locale.CHINA).format(Date(timeMs))
internal fun shortTime(timeMs: Long): String = SimpleDateFormat("HH:mm", Locale.CHINA).format(Date(timeMs))
internal fun decimal(value: Double?): String = value?.takeIf { it.isFinite() }?.let { String.format(Locale.ROOT,"%.1f",it) } ?: "--"
internal fun alarmReason(code: String): String = when (code) { "A" -> "正常"; "B" -> "无尿"; "C" -> "少尿"; "D" -> "多尿"; "E" -> "尿袋满"; else -> "未知警报" }
internal fun recordStatus(status: String): String = when(status) { "MEASURING" -> "测量中"; "ENDED" -> "正常结束"; else -> "结束状态未确认" }

@Composable
internal fun PageHeading(title: String, onBack: (() -> Unit)? = null) {
    Row(Modifier.fillMaxWidth().heightIn(min=64.dp).padding(horizontal=if(onBack == null) 18.dp else 8.dp), verticalAlignment=Alignment.CenterVertically) {
        if (onBack != null) IconButton(onClick=onBack, modifier=Modifier.size(48.dp)) { Icon(AppIcons.Back,"返回") }
        Text(title,style=MaterialTheme.typography.titleLarge,fontWeight=FontWeight.SemiBold,modifier=Modifier.weight(1f))
    }
    if(onBack != null) HorizontalDivider()
}

@Composable
internal fun MutedText(text: String, modifier: Modifier = Modifier) { Text(text, modifier, style=MaterialTheme.typography.bodySmall,color=MaterialTheme.colorScheme.onSurfaceVariant) }

@Composable
internal fun EmptyMessage(text: String) { Box(Modifier.fillMaxWidth().padding(vertical=26.dp), contentAlignment=Alignment.Center) { MutedText(text) } }

@Composable
internal fun ConfirmDialog(title: String, text: String, confirm: String, onDismiss: () -> Unit, onConfirm: () -> Unit, dangerous: Boolean = false) {
    AlertDialog(onDismissRequest=onDismiss,title={Text(title)},text={Text(text)},confirmButton={
        TextButton(onClick=onConfirm, modifier=Modifier.heightIn(min=48.dp), colors=ButtonDefaults.textButtonColors(contentColor=if(dangerous) NiaioPalette.Danger else MaterialTheme.colorScheme.primary)) { Text(confirm) }
    },dismissButton={ TextButton(onClick=onDismiss, modifier=Modifier.heightIn(min=48.dp)) { Text("返回") } })
}
