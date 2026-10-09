package com.niaio.mobile.records

import java.io.Serializable

/** Null increments mean no observed increment; partial values are known subtotals, not estimates. */
data class MinuteEntry(
    val timeMs: Long = 0,
    val volumeMl: Double? = null,
    val drops: Long? = null,
    val rateMlh: Double? = null,
    val cumulativeMl: Double? = null,
    val cumulativeDrops: Long? = null,
    val coverageSeconds: Double = 0.0,
    val partial: Boolean = true,
) : Serializable

data class LocalRecord(
    val id: Long = 0,
    val name: String = "",
    val startedAtMs: Long = 0,
    val startKnown: Boolean = false,
    val status: String = "UNCONFIRMED",
    val totalMl: Double = 0.0,
    val totalDrops: Long = 0,
    val elapsedSeconds: Long = 0,
    val minutes: List<MinuteEntry> = emptyList(),
) : Serializable

data class LocalAlarm(
    val id: Long = 0,
    val recordId: Long = 0,
    val timeMs: Long = 0,
    val code: String = "",
    val endedAtMs: Long? = null,
    val recordName: String = "",
) : Serializable

data class RecordBookState(
    val records: List<LocalRecord> = emptyList(),
    val alarms: List<LocalAlarm> = emptyList(),
    val nextRecordId: Long = 1,
    val nextAlarmId: Long = 1,
) : Serializable
