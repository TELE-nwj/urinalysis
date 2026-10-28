package com.niaio.mobile.records

import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class RecordExportTest {
    @Test fun hourlyUsesCoverageWeightsAndKeepsMissingQuantitiesMissing() {
        val at = Instant.parse("2026-10-09T01:00:00Z").toEpochMilli()
        val record = LocalRecord(minutes = listOf(
            MinuteEntry(timeMs = at, volumeMl = 2.0, drops = 4, rateMlh = 100.0, coverageSeconds = 10.0, partial = true),
            MinuteEntry(timeMs = at + 60_000, volumeMl = 3.0, drops = 6, rateMlh = 200.0, cumulativeMl = 5.0, coverageSeconds = 30.0, partial = true),
            MinuteEntry(timeMs = at + 3_600_000, partial = true)))
        val hours = RecordAggregation.hourly(record)
        assertEquals(2, hours.size)
        assertEquals(175.0, hours.first().rateMlh!!, 0.0)
        assertEquals(5.0, hours.first().volumeMl!!, 0.0)
        assertEquals(10L, hours.first().drops)
        assertNull(hours.last().volumeMl)
        assertNull(hours.last().rateMlh)
        assertTrue(hours.all { it.partial })
    }

    @Test fun csvEscapesFormulaNamesQuotesNewlinesAndRetainsEveryAlarm() {
        val state = RecordBookState(records = listOf(LocalRecord(id = 1, name = " =SUM(1,2)\"\n行",
            minutes = listOf(MinuteEntry(cumulativeMl = 42.0, cumulativeDrops = 84, partial = true)))),
            alarms = listOf(LocalAlarm(id = 1, recordId = 1, code = "B", recordName = "@danger"),
                LocalAlarm(id = 2, recordId = 999, code = "C", endedAtMs = 3, recordName = "孤立警报")))
        val csv = RecordCsv.export(state)
        assertTrue(csv.contains("' =SUM(1,2)\"\"\n行"))
        assertTrue(csv.contains("'@danger"))
        assertTrue(csv.contains("cumulative_ml"))
        assertTrue(csv.contains("\"42.0\""))
        assertTrue(csv.contains("\"84\""))
        assertEquals(2, Regex("\"ALARM\"").findAll(csv).count())
        assertTrue(csv.contains("孤立警报"))
        assertTrue(csv.contains("\"true\""))
    }

    @Test fun codecRoundTripsAndRejectsTruncationCorruptionAndTrailingData() {
        val state = RecordBookState(records = listOf(LocalRecord(id = 1, name = "真实记录",
            minutes = listOf(MinuteEntry(timeMs = 60_000, cumulativeMl = 1.5, partial = true)))),
            alarms = listOf(LocalAlarm(id = 1, recordId = 1, code = "B")), nextRecordId = 2, nextAlarmId = 2)
        val encoded = RecordCodec.encode(state)
        assertEquals(state, RecordCodec.decode(encoded))
        listOf(encoded.copyOf(encoded.size - 1), encoded.copyOf().also { it[it.size - 10] = (it[it.size - 10].toInt() xor 1).toByte() },
            encoded + byteArrayOf(0)).forEach {
            try { RecordCodec.decode(it); fail("Damaged data must be rejected") }
            catch (_: java.io.IOException) { }
        }
    }
}
