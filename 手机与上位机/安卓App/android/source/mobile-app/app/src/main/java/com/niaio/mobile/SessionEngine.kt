package com.niaio.mobile

import java.util.Locale

data class SessionView(
    val ready: Boolean = false, val deviceId: String? = null, val bootId: String? = null, val sessionId: String? = null,
    val sync: String = "时间未同步", val waiting: Boolean = false, val control: String = "", val snapshot: Snapshot? = null,
    val simulated: Boolean = false, val lastUpdateHms: String? = null, val stateSeq: Long = -1, val stale: Boolean = true, val lastUpdate: Long? = null, val protocolError: String? = null,
)
/** All methods run on one owner thread; monotonically timed, with no reconnect/control replay. */
class SessionEngine {
    var view = SessionView(); private set
    private var online = false
    private var nextId = 1L
    private var syncRequest: String? = null
    private var syncId: Int? = null
    private var syncAttempts = 0
    private var syncAt = 0L
    private var syncFinished = false
    private var controlId: Int? = null
    private var controlAt = 0L
    private val outgoing = mutableListOf<String>()
    private val seenSessions = linkedSetOf<Pair<String, String>>()
    fun open() { online = true; view = view.copy(ready = false, stale = true) }
    fun disconnect() {
        online = false; outgoing.clear(); controlId = null; syncRequest = null; syncId = null
        view = view.copy(ready = false, stale = true, waiting = false,
            control = if (view.waiting) "执行情况未确认（连接已断开）" else view.control)
    }
    fun receive(text: String, now: Long, hms: String) {
        if (!online) return
        val message = MobileProtocol.parse(text) ?: run { view = view.copy(protocolError = "收到无效协议数据"); return }
        when (message) {
            is Hello -> {
                if (view.ready && view.bootId == message.bootId && view.sessionId == message.sessionId) return
                // A connection may announce only one session. Reject unsolicited identity changes.
                if (view.ready) { view = view.copy(protocolError = "会话标识意外改变，请重新连接"); return }
                val identity = message.bootId to message.sessionId
                if (identity in seenSessions) { view = view.copy(protocolError = "设备重复使用已结束会话，请重新连接"); return }
                seenSessions += identity
                if (seenSessions.size > 32) seenSessions.remove(seenSessions.first())
                view = SessionView(ready = true, deviceId = message.deviceId, bootId = message.bootId, sessionId = message.sessionId, simulated = message.simulated)
                nextId = 1; controlId = null; syncFinished = false; syncAttempts = 1; syncAt = now
                syncId = allocateId(); val parts = hms.split(':').map(String::toInt)
                syncRequest = "{\"v\":1,\"type\":\"time_sync\",\"session_id\":\"${message.sessionId}\",\"req_id\":$syncId,\"hour\":${parts[0]},\"minute\":${parts[1]},\"second\":${parts[2]}}"
                outgoing += syncRequest!!
            }
            is Snapshot -> if (matches(message.bootId, message.sessionId)) {
                val prior = view.snapshot
                if ((prior == null || message.seq > prior.seq) && message.stateSeq >= view.stateSeq && (prior == null || message.uptime >= prior.uptime)) {
                    view = view.copy(snapshot = message, stateSeq = message.stateSeq, stale = false, lastUpdate = now, lastUpdateHms = hms, protocolError = null)
                }
            }
            is Ack -> if (matches(message.bootId, message.sessionId)) {
                if (message.reqId == syncId) {
                    if (!syncFinished && message.phase == "completed") {
                        syncFinished = true; view = view.copy(sync = if (message.result == "OK") "时间已同步" else "对时失败：${message.result}")
                    } else if (!syncFinished && message.phase == "rejected") { syncFinished = true; view = view.copy(sync = "对时失败：${message.result}") }
                } else if (message.reqId == controlId) {
                    if (message.phase == "accepted") {
                        if (now - controlAt < 5000) view = view.copy(control = "命令已受理 · 等待执行结果")
                    } else {
                        controlId = null
                        // ACK can advance state, but never regress a newer status (e.g. physical key).
                        val changed = message.phase == "completed" && message.result == "OK" && message.stateSeq > view.stateSeq
                        view = view.copy(waiting = false, control = if (message.phase == "completed" && message.result == "OK") "执行完成" else "命令未完成：${message.result}",
                            stateSeq = if (changed) message.stateSeq else view.stateSeq,
                            snapshot = if (changed) view.snapshot?.copy(state = message.state, stateSeq = message.stateSeq) else view.snapshot)
                    }
                }
            }
            is ServerError -> view = view.copy(protocolError = "设备报告：${message.result}")
        }
    }
    private fun matches(boot: String, session: String) = view.ready && boot == view.bootId && session == view.sessionId
    fun tick(now: Long) {
        view = view.copy(stale = !view.ready || view.lastUpdate == null || now - view.lastUpdate!! > 3000)
        if (online && view.ready && !syncFinished && syncRequest != null && now - syncAt >= 5000) {
            if (syncAttempts < 3) { outgoing += syncRequest!!; syncAttempts++; syncAt = now }
            else view = view.copy(sync = "对时失败")
        }
        if (view.waiting && now - controlAt >= 5000) view = view.copy(control = "执行情况未确认")
    }
    fun command(action: String, now: Long): Boolean {
        tick(now)
        if (!online || !view.ready || view.stale || view.waiting || view.simulated || action !in setOf("start", "pause", "reset")) return false
        val id = allocateId() ?: run { view = view.copy(protocolError = "请求编号已用尽，请重新连接"); return false }
        controlId = id; controlAt = now; view = view.copy(waiting = true, control = "等待设备受理")
        outgoing += "{\"v\":1,\"type\":\"command\",\"session_id\":\"${view.sessionId}\",\"req_id\":$id,\"action\":\"$action\"}"
        return true
    }
    private fun allocateId(): Int? = if (nextId <= Int.MAX_VALUE) nextId++.toInt() else null
    fun drain(): List<String> = outgoing.toList().also { outgoing.clear() }
}
object DisplayFormat {
    fun elapsed(seconds: Long): String = String.format(Locale.ROOT, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60)
    fun battery(value: Int?): String = value?.let { "$it%" } ?: "--"
    fun volume(value: Double): String = String.format(Locale.ROOT, "%.1f", value)
}

