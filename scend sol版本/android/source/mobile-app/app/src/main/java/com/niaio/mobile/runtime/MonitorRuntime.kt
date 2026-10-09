package com.niaio.mobile.runtime

import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.referentialEqualityPolicy
import com.niaio.mobile.ProbeState
import com.niaio.mobile.Snapshot
import com.niaio.mobile.records.RecordBookState
import com.niaio.mobile.records.RecordEngine
import com.niaio.mobile.records.RecordRepository

/** Process lifetime state. No Activity, restart intent, or old control request is retained. */
object MonitorRuntime {
    private val main = Handler(Looper.getMainLooper())
    private val probeValue = mutableStateOf(ProbeState())
    val probe: State<ProbeState> = probeValue
    private val bookValue = mutableStateOf(RecordBookState(), referentialEqualityPolicy())
    val book: State<RecordBookState> = bookValue
    private val selectedValue = mutableStateOf<Long?>(null)
    val selectedRecord: State<Long?> = selectedValue
    private val persistenceValue = mutableStateOf<String?>(null)
    val persistenceStatus: State<String?> = persistenceValue
    private val notificationValue = mutableStateOf<String?>(null)
    val notificationStatus: State<String?> = notificationValue

    private var rawProbe = ProbeState()
    private var application: Context? = null
    private var repository: RecordRepository? = null
    private val disk = RecordWriteQueue()
    private var engine: RecordEngine? = null
    private var loading = false
    private var loaded = false
    private var canSave = false
    private var revision = 0L
    private var dirty = false
    private var lastQueuedAt = 0L
    private var service: MonitorService? = null
    private var serviceRequested = false
    private var notifications: MonitorNotifications? = null
    private var lastAccepted: Snapshot? = null
    private var hadConnection = false

    fun initialize(context: Context) = onMain {
        if (application == null) {
            application = context.applicationContext
            repository = RecordRepository(context.applicationContext)
            notifications = MonitorNotifications(context.applicationContext, ::notificationProblem)
        }
        notifications?.refreshAvailability()
        if (loaded || loading) return@onMain
        loading = true
        persistenceProblem("记录正在加载")
        disk.load(repository!!) { result -> onMain {
            loading = false
            loaded = true
            result.fold(onSuccess = { stored ->
                engine = RecordEngine(stored)
                canSave = true
                bookValue.value = engine!!.state
                persistenceProblem(null)
                notifications?.reconcile(bookValue.value)
                if (bookValue.value !== stored) { dirty = true; saveBook(force = true) }
            }, onFailure = { error ->
                // Never replace an unreadable user file with an empty book.
                engine = RecordEngine()
                canSave = false
                persistenceProblem("记录加载失败，原文件已保留；新记录暂未保存：${error.message ?: error.javaClass.simpleName}")
            })
            service?.recordsReady()
        } }
    }

    fun connect(context: Context) = onMain {
        initialize(context)
        service?.let { serviceRequested = true; it.connectFromUser(); return@onMain }
        if (serviceRequested) return@onMain
        serviceRequested = true
        updateProbe(rawProbe.copy(status = "正在启动连接服务", connecting = true))
        try {
            context.applicationContext.startForegroundService(Intent(context.applicationContext, MonitorService::class.java)
                .setAction(MonitorService.ACTION_CONNECT))
        } catch (error: RuntimeException) {
            serviceRequested = false
            runtimeProblem("无法启动连接服务：${error.message ?: error.javaClass.simpleName}")
        }
    }

    fun disconnect(context: Context) = onMain {
        initialize(context)
        serviceRequested = false
        service?.disconnectFromUser() ?: run {
            // Also cancel an already requested service before its onStartCommand arrives.
            context.applicationContext.stopService(Intent(context.applicationContext, MonitorService::class.java))
            connectionEnded()
            updateProbe(rawProbe.copy(status = "设备未连接", connecting = false, connected = false,
                wifiAddress = null, session = rawProbe.session.copy(ready = false, stale = true, waiting = false)))
        }
    }

    fun command(action: String) = onMain { service?.command(action) }
    fun clearLog() = onMain { service?.clearLog() ?: updateProbe(rawProbe.copy(log = emptyList())) }
    fun renameRecord(id: Long, name: String) = mutate {
        it.rename(id, name)
        notifications?.cancelRecord(id)
    }
    fun deleteRecord(id: Long) = mutate {
        if (it.deleteRecord(id)) {
            notifications?.cancelRecord(id)
            if (selectedValue.value == id) selectedValue.value = null
        }
    }
    fun deleteAlarm(id: Long) = mutate { it.deleteAlarm(id); notifications?.cancelAlarm(id) }
    fun clearAll() = mutate {
        it.clearAll()
        notifications?.cancelAllAlarms()
        selectedValue.value = null
        // A subsequent fresh frame can begin a new local record; the cached last frame cannot.
    }
    fun selectRecord(id: Long?) = onMain { selectedValue.value = id }

    internal fun attach(owner: MonitorService) {
        checkMain(); service = owner; initialize(owner.applicationContext)
    }
    internal fun detach(owner: MonitorService) {
        checkMain()
        if (service === owner) { service = null; serviceRequested = false }
    }
    internal fun recordsLoaded(): Boolean = loaded
    internal fun wantsConnection(): Boolean = serviceRequested
    internal fun updateProbe(value: ProbeState) {
        checkMain(); rawProbe = value; renderProbe()
        notifications?.refreshAvailability(force = false)
        if (hadConnection && !value.connected) connectionEnded()
        hadConnection = value.connected
        val sample = value.session.snapshot
        val device = value.session.deviceId
        if (value.connected && value.session.ready && !value.simulated && !value.session.simulated &&
            !value.session.stale && device != null && sample != null && sample !== lastAccepted) {
            val priorSample = lastAccepted
            lastAccepted = sample
            val owner = engine ?: return
            val prior = owner.state
            val alarms = owner.accept(device, sample, System.currentTimeMillis())
            if (owner.state !== prior) {
                val before = prior.records.lastOrNull()
                val after = owner.state.records.lastOrNull()
                val important = priorSample?.state != sample.state || prior.records.size != owner.state.records.size ||
                    prior.alarms !== owner.state.alarms || before?.status != after?.status || before?.minutes?.size != after?.minutes?.size
                publishBook(force = important)
            }
            for (alarm in alarms) notifications?.postAlarm(alarm)
        }
    }
    internal fun connectionEnded() {
        checkMain(); lastAccepted = null; hadConnection = false
        engine?.let { val prior = it.state; it.disconnected(); if (it.state !== prior) publishBook(force = true) }
    }
    internal fun runtimeProblem(message: String) {
        checkMain()
        updateProbe(rawProbe.copy(status = message, connecting = false, connected = false,
            session = rawProbe.session.copy(ready = false, stale = true, waiting = false)))
    }
    internal fun flush(completed: () -> Unit) {
        checkMain()
        saveBook(force = true)
        disk.flush { onMain(completed) }
    }
    internal fun notificationProblem(message: String?) = onMain { notificationValue.value = message }

    private fun mutate(operation: (RecordEngine) -> Unit) = onMain {
        val owner = engine
        if (owner == null) { persistenceProblem("记录正在加载，请稍后再操作"); return@onMain }
        val prior = owner.state
        operation(owner)
        if (owner.state !== prior) publishBook(force = true)
    }
    private fun publishBook(force: Boolean) { bookValue.value = engine!!.state; dirty = true; saveBook(force) }
    private fun saveBook(force: Boolean) {
        if (!canSave || !dirty) return
        val now = SystemClock.elapsedRealtime()
        if (!force && now - lastQueuedAt < 10_000) return
        lastQueuedAt = now
        dirty = false
        val current = ++revision
        disk.save(repository!!, current, bookValue.value) { savedRevision, result -> onMain {
            if (savedRevision != revision) return@onMain
            result.fold(onSuccess = { persistenceProblem(null) },
                onFailure = { dirty = true; persistenceProblem("记录保存失败，当前更改尚未保存：${it.message ?: it.javaClass.simpleName}") })
        } }
    }
    private fun persistenceProblem(message: String?) { persistenceValue.value = message; renderProbe() }
    private fun renderProbe() {
        val issue = persistenceValue.value
        probeValue.value = if (issue == null) rawProbe else rawProbe.copy(status = "${rawProbe.status} · $issue")
    }
    private fun checkMain() = check(Looper.myLooper() == Looper.getMainLooper())
    private fun onMain(action: () -> Unit) { if (Looper.myLooper() == Looper.getMainLooper()) action() else main.post(action) }
}
