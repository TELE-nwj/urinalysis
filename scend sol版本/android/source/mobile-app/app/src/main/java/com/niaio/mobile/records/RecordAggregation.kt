package com.niaio.mobile.records

import java.time.Instant
import java.time.ZoneId
import java.time.temporal.ChronoUnit

object RecordAggregation {
    /** Natural local hours, including distinct repeated hours during daylight-saving changes. */
    fun hourly(record: LocalRecord): List<MinuteEntry> {
        val zone = ZoneId.systemDefault()
        return record.minutes.groupBy {
            Instant.ofEpochMilli(it.timeMs).atZone(zone).truncatedTo(ChronoUnit.HOURS).toInstant().toEpochMilli()
        }.toSortedMap().map { (hour, unsorted) ->
            val minutes = unsorted.sortedBy { it.timeMs }
            val volumes = minutes.mapNotNull { it.volumeMl }
            val drops = minutes.mapNotNull { it.drops }
            val coveredRates = minutes.filter { it.rateMlh != null && it.coverageSeconds > 0 }
            val weight = coveredRates.sumOf { it.coverageSeconds }
            val coverage = minutes.sumOf { it.coverageSeconds }
            MinuteEntry(timeMs = hour,
                volumeMl = volumes.takeIf { it.isNotEmpty() }?.sum(),
                drops = drops.takeIf { it.isNotEmpty() }?.sum(),
                rateMlh = if (weight > 0) coveredRates.sumOf { it.rateMlh!! * it.coverageSeconds } / weight else null,
                cumulativeMl = minutes.lastOrNull { it.cumulativeMl != null }?.cumulativeMl,
                cumulativeDrops = minutes.lastOrNull { it.cumulativeDrops != null }?.cumulativeDrops,
                coverageSeconds = coverage,
                partial = minutes.any { it.partial || it.volumeMl == null || it.drops == null } || minutes.size < 60 || coverage < 3600,
            )
        }
    }
}
