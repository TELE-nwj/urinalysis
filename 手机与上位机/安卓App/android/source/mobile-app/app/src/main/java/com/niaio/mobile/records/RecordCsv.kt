package com.niaio.mobile.records

import java.time.Instant

object RecordCsv {
    /** One rectangular UTF-8-ready CSV; orphaned and older alarms are included. Empty cells mean unknown. */
    fun export(state: RecordBookState): String = buildString {
        val columns = listOf("row_type", "record_id", "record_name", "record_started_at_ms", "record_started_at_iso",
            "start_known", "record_status", "total_ml", "total_drops", "elapsed_seconds", "time_ms", "time_iso",
            "volume_ml", "drops", "rate_mlh", "cumulative_ml", "cumulative_drops", "coverage_seconds", "partial",
            "alarm_id", "alarm_code", "alarm_ended_at_ms", "alarm_ended_at_iso")
        fun row(fields: Map<String, Any?>) {
            append(columns.joinToString(",") { escape(fields[it]?.toString() ?: "") }); append("\r\n")
        }
        append(columns.joinToString(",") { escape(it) }); append("\r\n")
        for (record in state.records) {
            val common = mapOf<String, Any?>("record_id" to record.id, "record_name" to record.name,
                "record_started_at_ms" to record.startedAtMs, "record_started_at_iso" to iso(record.startedAtMs),
                "start_known" to record.startKnown, "record_status" to record.status,
                "total_ml" to record.totalMl, "total_drops" to record.totalDrops, "elapsed_seconds" to record.elapsedSeconds)
            row(common + ("row_type" to "RECORD"))
            for (minute in record.minutes) row(common + mapOf("row_type" to "MINUTE", "time_ms" to minute.timeMs,
                "time_iso" to iso(minute.timeMs), "volume_ml" to minute.volumeMl, "drops" to minute.drops,
                "rate_mlh" to minute.rateMlh, "cumulative_ml" to minute.cumulativeMl, "cumulative_drops" to minute.cumulativeDrops,
                "coverage_seconds" to minute.coverageSeconds, "partial" to minute.partial))
        }
        for (alarm in state.alarms) row(mapOf("row_type" to "ALARM", "record_id" to alarm.recordId,
            "record_name" to alarm.recordName, "time_ms" to alarm.timeMs, "time_iso" to iso(alarm.timeMs),
            "alarm_id" to alarm.id, "alarm_code" to alarm.code, "alarm_ended_at_ms" to alarm.endedAtMs,
            "alarm_ended_at_iso" to alarm.endedAtMs?.let(::iso)))
    }

    private fun iso(timeMs: Long) = Instant.ofEpochMilli(timeMs).toString()

    private fun escape(raw: String): String {
        val significant = raw.dropWhile { it.isWhitespace() || it == '\uFEFF' }
        val dangerous = significant.firstOrNull() in listOf('=', '+', '-', '@') ||
            raw.firstOrNull() in listOf('\t', '\r', '\n')
        val safe = if (dangerous) "'$raw" else raw
        return "\"${safe.replace("\"", "\"\"")}\""
    }
}
