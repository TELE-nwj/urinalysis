package com.niaio.mobile.runtime

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import com.niaio.mobile.MainActivity
import com.niaio.mobile.R
import com.niaio.mobile.records.LocalAlarm
import com.niaio.mobile.records.RecordBookState

internal class MonitorNotifications(context: Context, private val problem: (String?) -> Unit) {
    private val app = context.applicationContext
    private val manager = app.getSystemService(NotificationManager::class.java)
    private var checkedAt = Long.MIN_VALUE
    private var available = false

    init {
        manager.createNotificationChannel(NotificationChannel(MONITOR_CHANNEL, "设备连接", NotificationManager.IMPORTANCE_LOW)
            .apply { description = "设备 Wi-Fi 连接正在后台监测"; setShowBadge(false) })
        manager.createNotificationChannel(NotificationChannel(ALARM_CHANNEL, "设备报警", NotificationManager.IMPORTANCE_HIGH)
            .apply { description = "无尿、少尿、多尿和尿袋满报警"; enableVibration(true) })
    }

    fun foreground(status: String): Notification = Notification.Builder(app, MONITOR_CHANNEL)
        .setSmallIcon(R.drawable.ic_launcher).setContentTitle("NIAIO 设备监测")
        .setContentText(status).setOngoing(true).setCategory(Notification.CATEGORY_SERVICE)
        .setOnlyAlertOnce(true).setContentIntent(openApp()).build()

    fun updateForeground(status: String) { manager.notify(FOREGROUND_ID, foreground(status)) }

    fun refreshAvailability(force: Boolean = true): Boolean {
        val now = SystemClock.elapsedRealtime()
        if (!force && checkedAt != Long.MIN_VALUE && now - checkedAt < 2000) return available
        checkedAt = now
        val unavailable = when {
            Build.VERSION.SDK_INT >= 33 && app.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED -> "系统通知权限未允许，报警仍保留在本地历史"
            !manager.areNotificationsEnabled() -> "系统通知已关闭，报警仍保留在本地历史"
            manager.getNotificationChannel(ALARM_CHANNEL)?.importance == NotificationManager.IMPORTANCE_NONE -> "报警通知渠道已关闭，报警仍保留在本地历史"
            else -> null
        }
        problem(unavailable)
        available = unavailable == null
        return available
    }

    fun postAlarm(alarm: LocalAlarm) {
        if (!refreshAvailability()) return
        val reason = when (alarm.code) { "B" -> "无尿"; "C" -> "少尿"; "D" -> "多尿"; "E" -> "尿袋满"; else -> return }
        val extras = Bundle().apply { putLong(RECORD_ID, alarm.recordId); putString(RECORD_NAME, alarm.recordName) }
        val notification = Notification.Builder(app, ALARM_CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher).setContentTitle("$reason · ${alarm.recordName}")
            .setContentText("设备报告$reason，点击查看关联测量记录")
            .setWhen(alarm.timeMs).setShowWhen(true).setCategory(Notification.CATEGORY_ALARM)
            .setAutoCancel(true).addExtras(extras).setContentIntent(openApp(alarm)).build()
        try { manager.notify(tag(alarm.id), ALARM_ID, notification) }
        catch (error: SecurityException) { problem("系统拒绝报警通知，报警已保留在历史：${error.message.orEmpty()}") }
    }

    fun cancelAlarm(id: Long) = manager.cancel(tag(id), ALARM_ID)
    fun cancelRecord(id: Long) {
        for (notice in manager.activeNotifications) if (isAlarm(notice.tag) && notice.notification.extras.getLong(RECORD_ID, -1) == id)
            manager.cancel(notice.tag, notice.id)
    }
    fun cancelAllAlarms() {
        for (notice in manager.activeNotifications) if (isAlarm(notice.tag)) manager.cancel(notice.tag, notice.id)
    }
    /** Do not replay loaded alarms. Remove notifications whose retained label/link is now obsolete. */
    fun reconcile(book: RecordBookState) {
        val alarms = book.alarms.associateBy { it.id }
        val records = book.records.associateBy { it.id }
        for (notice in manager.activeNotifications) {
            val alarmId = notice.tag?.takeIf(::isAlarm)?.removePrefix(ALARM_TAG)?.toLongOrNull() ?: continue
            val alarm = alarms[alarmId]
            if (alarm == null || records[alarm.recordId] == null ||
                notice.notification.extras.getString(RECORD_NAME) != alarm.recordName) manager.cancel(notice.tag, notice.id)
        }
    }

    private fun openApp(alarm: LocalAlarm? = null): PendingIntent {
        val intent = Intent(app, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        if (alarm != null) {
            intent.putExtra(RECORD_ID, alarm.recordId)
            intent.data = Uri.parse("niaio://record/${alarm.recordId}/alarm/${alarm.id}")
        }
        return PendingIntent.getActivity(app, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    }
    private fun tag(id: Long) = "$ALARM_TAG$id"
    private fun isAlarm(value: String?) = value?.startsWith(ALARM_TAG) == true

    companion object {
        const val MONITOR_CHANNEL = "MONITOR"
        const val ALARM_CHANNEL = "ALARM"
        const val RECORD_ID = "RECORD_ID"
        private const val RECORD_NAME = "RECORD_NAME"
        private const val ALARM_TAG = "NIAIO_ALARM_"
        const val FOREGROUND_ID = 1
        private const val ALARM_ID = 2
    }
}
