package com.niaio.mobile.records

import com.niaio.mobile.Snapshot
import org.junit.Assert.*
import org.junit.Test

class RecordEngineTest {
    private val wall = 1_800_000_000_000L
    private fun sample(seq: Long = 1, uptime: Long = 0, total: Double = 0.0,
        drops: Long = 0, state: String = "MEASURING", stateSeq: Long = 1,
        elapsed: Long = uptime / 1000, alert: String = "A", boot: String = "boot",
        session: String = "session") = Snapshot(boot, session, seq, stateSeq, uptime,
        total, 120.0, drops, elapsed, state, alert, null, "UNKNOWN", null, "12:00:00", false)

    @Test fun confirmedStartAndDuplicateDoNotInventMeasurements() {
        val engine = RecordEngine()
        val idle = sample(state = "IDLE", stateSeq = 0)
        engine.accept("device", idle, wall)
        assertTrue(engine.state.records.isEmpty())
        val started = idle.copy(state = "MEASURING", stateSeq = 1)
        engine.accept("device", started, wall + 100)
        val state = engine.state
        assertTrue(state.records.single().startKnown)
        engine.accept("device", started, wall + 200)
        assertSame(state, engine.state)
    }

    @Test fun sameSequenceResetAckEndsAndRetainsFinalTotals() {
        val engine = RecordEngine()
        val measured = sample(total = 12.5, drops = 250, elapsed = 42)
        engine.accept("device", measured, wall)
        engine.accept("device", measured.copy(state = "IDLE", stateSeq = 2), wall + 100)
        assertEquals("ENDED", engine.state.records.single().status)
        assertEquals(12.5, engine.state.records.single().totalMl, 0.0)
        engine.accept("device", sample(seq = 2, uptime = 1000, state = "IDLE", stateSeq = 2, elapsed = 0), wall + 1000)
        assertEquals(250, engine.state.records.single().totalDrops)
    }

    @Test fun pauseAndResumeAckKeepRecordAndDoNotDuplicateSamples() {
        val engine = RecordEngine()
        val measured = sample(total = 1.0)
        engine.accept("device", measured, wall)
        val rows = engine.state.records.single().minutes
        val paused = measured.copy(state = "PAUSED", stateSeq = 2)
        engine.accept("device", paused, wall + 100)
        engine.accept("device", paused.copy(state = "MEASURING", stateSeq = 3), wall + 200)
        assertEquals(1, engine.state.records.size)
        assertEquals("MEASURING", engine.state.records.single().status)
        assertEquals(rows, engine.state.records.single().minutes)
    }

    @Test fun missingIntervalIsNotSpreadAcrossMinutes() {
        val engine = RecordEngine()
        engine.accept("device", sample(), wall)
        engine.accept("device", sample(seq = 2, uptime = 1000, total = 1.0, drops = 2), wall + 1000)
        engine.disconnected()
        engine.accept("device", sample(seq = 3, uptime = 121_000, total = 25.0, drops = 50, session = "reconnect"), wall + 121_000)
        val record = engine.state.records.single()
        assertEquals(25.0, record.totalMl, 0.0)
        assertEquals(1.0, record.minutes.mapNotNull { it.volumeMl }.sum(), 0.0)
        assertTrue(record.minutes.any { it.volumeMl == null && it.partial })
        assertEquals(1.0, record.minutes.sumOf { it.coverageSeconds }, 0.0)
        assertEquals(25.0, record.minutes.last().cumulativeMl!!, 0.0)
    }

    @Test fun crossedMinuteIncrementIsNotAssignedAnInventedFraction() {
        val engine = RecordEngine()
        engine.accept("device", sample(), wall + 59_500)
        engine.accept("device", sample(seq = 2, uptime = 1000, total = 5.0, drops = 10), wall + 60_500)
        val rows = engine.state.records.single().minutes
        assertEquals(2, rows.size)
        assertEquals(0.0, rows.mapNotNull { it.volumeMl }.sum(), 0.0)
        assertTrue(rows.all { it.partial })
    }

    @Test fun continuousMinutesKeepKnownSubtotalsWhenBoundaryIntervalsAreUnknown() {
        val engine = RecordEngine()
        for (second in 0L..125L) engine.accept("device", sample(seq = second + 1, uptime = second * 1000,
            total = second.toDouble(), drops = second * 2), wall + 500 + second * 1000)
        val rows = engine.state.records.single().minutes
        assertEquals(3, rows.size)
        assertEquals(59.0, rows[0].volumeMl!!, 0.0)
        assertEquals(59.0, rows[1].volumeMl!!, 0.0)
        assertEquals(59.0, rows[1].coverageSeconds, 0.0)
        assertEquals(3600.0, rows[1].rateMlh!!, 0.0)
        assertTrue(rows.all { it.volumeMl != null && it.partial })
        assertEquals(125.0, rows.last().cumulativeMl!!, 0.0)
    }

    @Test fun resetThenStartAckCannotCarryOldMeasurementIntoNewRecord() {
        val engine = RecordEngine()
        val measured = sample(total = 20.0, drops = 40, elapsed = 5)
        engine.accept("device", measured, wall)
        val reset = measured.copy(state = "IDLE", stateSeq = 2)
        engine.accept("device", reset, wall + 100)
        engine.accept("device", reset.copy(state = "MEASURING", stateSeq = 3), wall + 200)
        assertEquals(20.0, engine.state.records.first().totalMl, 0.0)
        assertEquals(0.0, engine.state.records.last().totalMl, 0.0)
        assertTrue(engine.state.records.last().startKnown)
        engine.accept("device", sample(seq = 2, uptime = 1000, stateSeq = 3, total = 1.0, drops = 2, elapsed = 1), wall + 1000)
        assertEquals(2, engine.state.records.size)
        assertEquals(1.0, engine.state.records.last().totalMl, 0.0)
    }

    @Test fun reconnectWithUnseenStateChangeCannotClaimNormalEnd() {
        val engine = RecordEngine()
        engine.accept("device", sample(total = 10.0), wall)
        engine.disconnected()
        engine.accept("device", sample(seq = 2, uptime = 1000, total = 20.0, stateSeq = 3, session = "new"), wall + 1000)
        assertEquals(listOf("UNCONFIRMED", "MEASURING"), engine.state.records.map { it.status })
        assertFalse(engine.state.records.last().startKnown)
        assertEquals(10.0, engine.state.records.first().totalMl, 0.0)
    }

    @Test fun deviceRebootAndCounterRegressionSeparateRecords() {
        val engine = RecordEngine()
        engine.accept("device", sample(total = 10.0), wall)
        engine.accept("device", sample(boot = "reboot", total = 2.0), wall + 1000)
        engine.accept("device", sample(seq = 2, uptime = 1000, total = 1.0, boot = "reboot"), wall + 2000)
        assertEquals(3, engine.state.records.size)
        assertTrue(engine.state.records.take(2).all { it.status == "UNCONFIRMED" })
    }

    @Test fun sustainedAlertReconnectRenameDeleteAndRecoveryStayConsistent() {
        val engine = RecordEngine()
        assertEquals(1, engine.accept("device", sample(alert = "B"), wall).size)
        engine.disconnected()
        assertTrue(engine.accept("device", sample(seq = 2, uptime = 1000, alert = "B", session = "new"), wall + 1000).isEmpty())
        engine.rename(1, "新的名称")
        assertEquals("新的名称", engine.state.alarms.single().recordName)
        assertEquals(1, engine.accept("device", sample(seq = 3, uptime = 2000, alert = "C", session = "new"), wall + 2000).size)
        assertEquals(wall + 2000, engine.state.alarms.first().endedAtMs)
        engine.deleteAlarm(1)
        assertEquals(1, engine.state.alarms.size)
        engine.accept("device", sample(seq = 4, uptime = 3000, alert = "A", session = "new"), wall + 3000)
        assertEquals(wall + 3000, engine.state.alarms.single().endedAtMs)
    }

    @Test fun clearAllDeletesActiveRecordAndNextSampleIsNewUnknownStart() {
        val engine = RecordEngine()
        engine.accept("device", sample(alert = "B"), wall)
        assertFalse(engine.deleteRecord(1))
        engine.clearAll()
        assertTrue(engine.state.records.isEmpty())
        assertTrue(engine.state.alarms.isEmpty())
        engine.accept("device", sample(seq = 2, uptime = 1000, total = 5.0, alert = "B"), wall + 1000)
        assertEquals(1, engine.state.records.size)
        assertFalse(engine.state.records.single().startKnown)
        assertEquals(5.0, engine.state.records.single().totalMl, 0.0)
        assertTrue(engine.state.alarms.isEmpty())
    }

    @Test fun removingEndedRecordPreservesIndependentAlarms() {
        val engine = RecordEngine()
        val measured = sample(alert = "B")
        engine.accept("device", measured, wall)
        engine.accept("device", measured.copy(state = "IDLE", stateSeq = 2), wall + 100)
        assertTrue(engine.deleteRecord(1))
        assertTrue(engine.state.records.isEmpty())
        assertEquals("记录 0001", engine.state.alarms.single().recordName)
    }

    @Test fun reloadDoesNotSilentlyJoinAnUnidentifiedActiveMeasurement() {
        val engine = RecordEngine(RecordBookState(records = listOf(LocalRecord(id = 1, status = "MEASURING")), nextRecordId = 2))
        assertEquals("UNCONFIRMED", engine.state.records.single().status)
        engine.accept("device", sample(), wall)
        assertEquals(2, engine.state.records.size)
        assertFalse(engine.state.records.last().startKnown)
    }
}
