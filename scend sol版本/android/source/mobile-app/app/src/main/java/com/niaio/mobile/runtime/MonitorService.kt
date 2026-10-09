package com.niaio.mobile.runtime

import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import com.niaio.mobile.DeviceWifiConnection

/** User-started, non-sticky connectedDevice foreground service. Activity visibility is irrelevant. */
class MonitorService : Service() {
    private lateinit var connection: DeviceWifiConnection
    private lateinit var notifications: MonitorNotifications
    private var foreground = false
    private var connectPending = false
    private var stopping = false
    private var connectedOrConnecting = false
    private var stopGeneration = 0L
    private var lastNoticeStatus: String? = null
    private var startupFailure: String? = null

    override fun onCreate() {
        super.onCreate()
        notifications = MonitorNotifications(applicationContext, MonitorRuntime::notificationProblem)
        connection = DeviceWifiConnection(applicationContext) { state ->
            connectedOrConnecting = state.connected || state.connecting
            MonitorRuntime.updateProbe(state)
            if (foreground && state.status != lastNoticeStatus) {
                lastNoticeStatus = state.status
                try { notifications.updateForeground(state.status) }
                catch (error: RuntimeException) { MonitorRuntime.notificationProblem("连接通知更新失败：${error.message.orEmpty()}") }
            }
        }
        MonitorRuntime.attach(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Never reconnect after process death or re-deliver a command intent.
        if (intent?.action != ACTION_CONNECT || !MonitorRuntime.wantsConnection()) {
            stopSelf(startId)
            return START_NOT_STICKY
        }
        try {
            val notice = notifications.foreground("正在连接设备 Wi-Fi")
            if (Build.VERSION.SDK_INT >= 29) startForeground(MonitorNotifications.FOREGROUND_ID, notice, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            else startForeground(MonitorNotifications.FOREGROUND_ID, notice)
            foreground = true
            notifications.refreshAvailability()
            connectFromUser()
        } catch (error: RuntimeException) {
            startupFailure = "连接服务启动失败：${error.message ?: error.javaClass.simpleName}"
            MonitorRuntime.runtimeProblem(startupFailure!!)
            stopSelf(startId)
        }
        return START_NOT_STICKY
    }

    internal fun connectFromUser() {
        if (!foreground || connectedOrConnecting) return
        stopGeneration++
        stopping = false
        connectPending = true
        recordsReady()
    }

    internal fun recordsReady() {
        if (!connectPending || stopping || !foreground || !MonitorRuntime.recordsLoaded()) return
        connectPending = false
        try { connection.connect() }
        catch (error: RuntimeException) {
            connection.disconnect(log = false)
            MonitorRuntime.runtimeProblem("连接失败：${error.message ?: error.javaClass.simpleName}")
        }
    }

    internal fun command(action: String) { if (!stopping && foreground) connection.command(action) }
    internal fun clearLog() = connection.clearLog()
    internal fun disconnectFromUser() {
        if (stopping) return
        stopping = true
        connectPending = false
        val ending = ++stopGeneration
        connection.disconnect()
        MonitorRuntime.connectionEnded()
        // Remain alive until the ordered save queue has attempted its final write.
        MonitorRuntime.flush {
            if (stopping && stopGeneration == ending) {
                if (foreground) stopForeground(STOP_FOREGROUND_REMOVE)
                foreground = false
                stopSelf()
            }
        }
    }

    override fun onDestroy() {
        stopping = true
        connectPending = false
        stopGeneration++
        if (::connection.isInitialized) connection.disconnect(log = false)
        MonitorRuntime.connectionEnded()
        MonitorRuntime.detach(this)
        startupFailure?.let(MonitorRuntime::runtimeProblem)
        if (foreground) stopForeground(STOP_FOREGROUND_REMOVE)
        foreground = false
        // The process-owned queue continues draining if the OS destroys this Service instance.
        MonitorRuntime.flush { }
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object { const val ACTION_CONNECT = "com.niaio.mobile.CONNECT" }
}
