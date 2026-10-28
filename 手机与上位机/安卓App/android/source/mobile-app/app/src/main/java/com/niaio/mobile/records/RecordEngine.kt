package com.niaio.mobile.records

import com.niaio.mobile.Snapshot
import java.util.Locale
import kotlin.math.min

/** Owned by one thread. A data sequence and a state sequence have separate jobs. */
class RecordEngine(initial: RecordBookState = RecordBookState()) {
    var state: RecordBookState = initial.copy(
        records = initial.records.map { it.copy(status = if (it.status == "MEASURING") "UNCONFIRMED" else it.status, minutes = it.minutes.toList()) },
        alarms = initial.alarms.toList(),
    ); private set

    private data class Observation(val device: String, val sample: Snapshot, val timeMs: Long)
    private var last: Observation? = null
    private var data: Observation? = null
    private var dataRecordId: Long? = null
    private var activeId: Long? = null
    private var offline = false
    private var anchorUptime = 0L
    private var anchorWall = 0L
    private var alarmCode: String? = null
    private var activeAlarmId: Long? = null

    fun accept(deviceId: String, sample: Snapshot, wallMs: Long): List<LocalAlarm> {
        require(deviceId.isNotBlank() && wallMs >= 0 && sample.uptime >= 0 && sample.seq >= 0 && sample.stateSeq >= 0)
        require(sample.totalMl.isFinite() && sample.totalMl >= 0 && sample.rateMlh.isFinite() && sample.rateMlh >= 0)
        require(sample.drops >= 0 && sample.elapsed >= 0 && sample.state in setOf("IDLE", "MEASURING", "PAUSED") && sample.alert in setOf("A", "B", "C", "D", "E"))
        val previous = last
        val identityChanged = previous == null || previous.device != deviceId || previous.sample.bootId != sample.bootId
        val sameSession = previous != null && previous.sample.sessionId == sample.sessionId
        if (!identityChanged && previous != null) {
            if (sample.stateSeq < previous.sample.stateSeq) return emptyList()
            if (sameSession && sample.seq < previous.sample.seq) return emptyList()
            if (sameSession && sample.seq == previous.sample.seq && sample.stateSeq == previous.sample.stateSeq) {
                // Exact replay after reconnect is still a gap: do not bridge it into the next sample.
                return emptyList()
            }
        }

        val idleTransition = previous != null && !identityChanged && !offline &&
            sample.state == "IDLE" && previous.sample.state != "IDLE" && sample.stateSeq == previous.sample.stateSeq + 1
        // ACKs reuse the old data sequence, even across reset/start. Only compare a data baseline
        // that belongs to this record; old-session counters are not a regression in a new record.
        val counterRegression = previous != null && activeId != null && dataRecordId == activeId && previous.sample.state != "IDLE" &&
            (sample.totalMl < previous.sample.totalMl || sample.drops < previous.sample.drops || sample.elapsed < previous.sample.elapsed)
        val stateJump = previous != null && sample.stateSeq - previous.sample.stateSeq > 1
        val unseenState = previous != null && offline && sample.stateSeq != previous.sample.stateSeq
        val lostContinuity = identityChanged || (previous != null && sample.uptime < previous.sample.uptime) ||
            (!idleTransition && counterRegression) || stateJump || unseenState
        if (lostContinuity) {
            finish("UNCONFIRMED")
            data = null; dataRecordId = null
            anchorUptime = sample.uptime; anchorWall = wallMs
            if (identityChanged) { alarmCode = null; activeAlarmId = null }
        }
        val mapped = mappedTime(sample.uptime, wallMs)
        val observation = Observation(deviceId, sample, mapped)
        val baseline = data
        val freshData = baseline == null || baseline.device != deviceId || baseline.sample.bootId != sample.bootId ||
            sample.uptime > baseline.sample.uptime || (sample.seq > baseline.sample.seq && sample.uptime == baseline.sample.uptime)

        if (sample.state == "IDLE") {
            // An IDLE reset clears counters. The record keeps the last measurement, not reset zeros.
            if (activeId != null) finish(if (idleTransition && !lostContinuity) "ENDED" else "UNCONFIRMED")
        } else {
            if (activeId == null) {
                val confirmedStart = previous != null && !lostContinuity && !offline && previous.sample.state == "IDLE" &&
                    sample.state == "MEASURING" && sample.stateSeq == previous.sample.stateSeq + 1
                val startTime = if (confirmedStart && !freshData) wallMs else mapped
                create(sample, startTime, confirmedStart, freshData)
            }
            val id = activeId!!
            if (freshData) {
                val priorForRecord = baseline?.takeIf { dataRecordId == id && !lostContinuity }
                updateMeasurement(id, priorForRecord, observation, offline)
                data = observation; dataRecordId = id
            }
        }

        val events = updateAlarm(sample.alert, if (freshData) mapped else wallMs)
        last = observation
        if (sample.state == "IDLE" && freshData) { data = observation; dataRecordId = null }
        offline = false
        return events
    }

    fun disconnected() { offline = true }

    fun rename(id: Long, name: String) {
        val trimmed = name.trim().take(256)
        if (trimmed.isEmpty() || state.records.none { it.id == id } || state.records.first { it.id == id }.name == trimmed) return
        state = state.copy(records = state.records.map { if (it.id == id) it.copy(name = trimmed) else it },
            alarms = state.alarms.map { if (it.recordId == id) it.copy(recordName = trimmed) else it })
    }

    fun deleteRecord(id: Long): Boolean {
        val record = state.records.find { it.id == id } ?: return false
        if (record.status == "MEASURING" || id == activeId) return false
        state = state.copy(records = state.records.filterNot { it.id == id })
        return true
    }

    fun deleteAlarm(id: Long) {
        if (state.alarms.none { it.id == id }) return
        state = state.copy(alarms = state.alarms.filterNot { it.id == id })
        if (activeAlarmId == id) activeAlarmId = null
        // Keep alarmCode: deleting an event must not retrigger a sustained device condition.
    }

    fun clearAll() {
        if (state.records.isNotEmpty() || state.alarms.isNotEmpty())
            state = RecordBookState(nextRecordId = state.nextRecordId, nextAlarmId = state.nextAlarmId)
        activeId = null; activeAlarmId = null; dataRecordId = null
        // Keep the observed condition and device anchor; the next fresh sample becomes an unknown start.
    }

    private fun mappedTime(uptime: Long, fallback: Long): Long {
        return try { Math.addExact(anchorWall, Math.subtractExact(uptime, anchorUptime)).coerceAtLeast(0) }
        catch (_: ArithmeticException) { fallback }
    }

    private fun create(sample: Snapshot, startedAt: Long, known: Boolean, freshData: Boolean) {
        check(state.nextRecordId in 1 until Long.MAX_VALUE) { "本地记录编号已用尽" }
        val id = state.nextRecordId
        val record = LocalRecord(id, String.format(Locale.ROOT, "记录 %04d", id), startedAt, known, "MEASURING",
            if (freshData) sample.totalMl else 0.0, if (freshData) sample.drops else 0L, if (freshData) sample.elapsed else 0L)
        state = state.copy(records = state.records + record, nextRecordId = id + 1)
        activeId = id
    }

    private fun finish(status: String) {
        val id = activeId ?: return
        state = state.copy(records = state.records.map { if (it.id == id) it.copy(status = status) else it })
        activeId = null
    }

    private fun updateMeasurement(id: Long, prior: Observation?, current: Observation, disconnected: Boolean) {
        val record = state.records.first { it.id == id }
        val entries = record.minutes.associateByTo(sortedMapOf()) { it.timeMs }
        val bucket = minute(current.timeMs)
        val existing = entries[bucket] ?: MinuteEntry(timeMs = bucket)
        val duration = prior?.let { current.sample.uptime - it.sample.uptime } ?: 0L
        val usable = prior != null && !disconnected && duration in 1..3000 &&
            current.sample.elapsed >= prior.sample.elapsed && current.sample.totalMl >= prior.sample.totalMl && current.sample.drops >= prior.sample.drops
        val priorBucket = prior?.let { minute(it.timeMs) }
        if (prior != null && (!usable || priorBucket != bucket)) {
            if (priorBucket != null) entries[priorBucket] = (entries[priorBucket] ?: MinuteEntry(timeMs = priorBucket)).copy(partial = true)
            // Explicit empty rows for ordinary gaps; very long gaps remain visible as gaps in timestamps.
            val count = (bucket - (priorBucket ?: bucket)) / 60_000
            if (count in 1..10_080) {
                var missing = (priorBucket ?: bucket) + 60_000
                while (missing < bucket) { entries.putIfAbsent(missing, MinuteEntry(timeMs = missing)); missing += 60_000 }
            }
        }
        var row = existing
        if (usable && priorBucket == bucket) {
            val seconds = min(duration / 1000.0, (current.sample.elapsed - prior!!.sample.elapsed).toDouble())
            val volume = (row.volumeMl ?: 0.0) + current.sample.totalMl - prior.sample.totalMl
            val coverage = row.coverageSeconds + seconds
            row = row.copy(volumeMl = volume,
                drops = (row.drops ?: 0L) + current.sample.drops - prior.sample.drops,
                rateMlh = if (coverage > 0) volume / coverage * 3600.0 else null, coverageSeconds = coverage)
        } else if (prior == null && record.startKnown && current.sample.totalMl == 0.0 && current.sample.drops == 0L) {
            row = row.copy(volumeMl = 0.0, drops = 0)
        }
        row = row.copy(cumulativeMl = current.sample.totalMl, cumulativeDrops = current.sample.drops,
            partial = row.partial || row.coverageSeconds < 60.0)
        entries[bucket] = row
        val updated = record.copy(totalMl = current.sample.totalMl, totalDrops = current.sample.drops,
            elapsedSeconds = current.sample.elapsed, minutes = entries.values.toList())
        if (updated != record) state = state.copy(records = state.records.map { if (it.id == id) updated else it })
    }

    private fun updateAlarm(code: String, at: Long): List<LocalAlarm> {
        if (code == alarmCode) return emptyList()
        val priorAlarmId = activeAlarmId
        if (priorAlarmId != null && state.alarms.any { it.id == priorAlarmId && it.endedAtMs == null }) {
            state = state.copy(alarms = state.alarms.map { if (it.id == priorAlarmId) it.copy(endedAtMs = at.coerceAtLeast(it.timeMs)) else it })
        }
        alarmCode = code; activeAlarmId = null
        if (code == "A") return emptyList()
        val record = state.records.find { it.id == activeId } ?: state.records.lastOrNull() ?: return emptyList()
        check(state.nextAlarmId in 1 until Long.MAX_VALUE) { "本地警报编号已用尽" }
        val alarm = LocalAlarm(state.nextAlarmId, record.id, at, code, recordName = record.name)
        state = state.copy(alarms = state.alarms + alarm, nextAlarmId = alarm.id + 1)
        activeAlarmId = alarm.id
        return listOf(alarm)
    }

    private fun minute(time: Long) = Math.floorDiv(time, 60_000L) * 60_000L
}
