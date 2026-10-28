package com.niaio.mobile.records

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.zip.CRC32

/** Private schema, not Java object deserialization. Every allocation is bounded before reading. */
internal object RecordCodec {
    const val MAX_BYTES = 32 * 1024 * 1024
    private const val MAGIC = 0x4E524231 // NRB1
    private const val VERSION = 1
    private const val MAX_ITEMS = 500_000
    private const val MAX_STRING_BYTES = 4096

    fun encode(state: RecordBookState): ByteArray {
        validate(state)
        val payload = ByteArrayOutputStream()
        DataOutputStream(payload).use { out ->
            out.writeLong(state.nextRecordId); out.writeLong(state.nextAlarmId)
            out.writeInt(state.records.size)
            for (record in state.records) {
                out.writeLong(record.id); out.text(record.name); out.writeLong(record.startedAtMs)
                out.writeBoolean(record.startKnown); out.text(record.status); out.writeDouble(record.totalMl)
                out.writeLong(record.totalDrops); out.writeLong(record.elapsedSeconds); out.writeInt(record.minutes.size)
                for (minute in record.minutes) {
                    out.writeLong(minute.timeMs); out.optionalDouble(minute.volumeMl); out.optionalLong(minute.drops)
                    out.optionalDouble(minute.rateMlh); out.optionalDouble(minute.cumulativeMl); out.optionalLong(minute.cumulativeDrops)
                    out.writeDouble(minute.coverageSeconds); out.writeBoolean(minute.partial)
                }
            }
            out.writeInt(state.alarms.size)
            for (alarm in state.alarms) {
                out.writeLong(alarm.id); out.writeLong(alarm.recordId); out.writeLong(alarm.timeMs)
                out.text(alarm.code); out.optionalLong(alarm.endedAtMs); out.text(alarm.recordName)
            }
        }
        val bytes = payload.toByteArray()
        ensure(bytes.size <= MAX_BYTES - 20, "记录文件超出大小上限；原文件未覆盖")
        return ByteArrayOutputStream(bytes.size + 20).also { buffer -> DataOutputStream(buffer).use {
            it.writeInt(MAGIC); it.writeInt(VERSION); it.writeInt(bytes.size); it.write(bytes)
            it.writeLong(CRC32().apply { update(bytes) }.value)
        } }.toByteArray()
    }

    fun decode(bytes: ByteArray): RecordBookState = try {
        ensure(bytes.size in 20..MAX_BYTES, "记录文件长度无效")
        DataInputStream(ByteArrayInputStream(bytes)).use { outer ->
            ensure(outer.readInt() == MAGIC, "记录文件标识无效")
            ensure(outer.readInt() == VERSION, "记录文件版本不支持")
            val size = outer.readInt()
            ensure(size >= 0 && size == bytes.size - 20, "记录文件长度不符")
            val payload = ByteArray(size); outer.readFully(payload)
            ensure(outer.readLong() == CRC32().apply { update(payload) }.value, "记录文件校验失败")
            DataInputStream(ByteArrayInputStream(payload)).use { input ->
                val nextRecord = input.readLong(); val nextAlarm = input.readLong()
                var totalMinutes = 0L
                val records = List(input.count()) {
                    val id = input.readLong(); val name = input.text(); val started = input.readLong()
                    val known = input.boolean(); val status = input.text(); val totalMl = input.readDouble()
                    val totalDrops = input.readLong(); val elapsed = input.readLong(); val count = input.count()
                    totalMinutes += count; ensure(totalMinutes <= MAX_ITEMS, "分钟记录数量无效")
                    val minutes = List(count) {
                        MinuteEntry(input.readLong(), input.optionalDouble(), input.optionalLong(), input.optionalDouble(),
                            input.optionalDouble(), input.optionalLong(), input.readDouble(), input.boolean())
                    }
                    LocalRecord(id, name, started, known, status, totalMl, totalDrops, elapsed, minutes)
                }
                val alarms = List(input.count()) {
                    LocalAlarm(input.readLong(), input.readLong(), input.readLong(), input.text(), input.optionalLong(), input.text())
                }
                ensure(input.available() == 0, "记录文件含未知尾部数据")
                RecordBookState(records, alarms, nextRecord, nextAlarm).also(::validate)
            }
        }
    } catch (error: IOException) { throw error }
      catch (error: RuntimeException) { throw IOException("记录文件无法读取", error) }

    private fun validate(state: RecordBookState) {
        ensure(state.records.size <= MAX_ITEMS && state.alarms.size <= MAX_ITEMS, "记录数量超过上限")
        ensure(state.records.sumOf { it.minutes.size.toLong() } <= MAX_ITEMS, "分钟记录数量超过上限")
        ensure(state.nextRecordId > 0 && state.nextAlarmId > 0, "记录编号无效")
        ensure(state.records.map { it.id }.toSet().size == state.records.size, "检测编号重复")
        ensure(state.alarms.map { it.id }.toSet().size == state.alarms.size, "警报编号重复")
        for (record in state.records) {
            ensure(record.id > 0 && record.id < state.nextRecordId && record.startedAtMs >= 0, "检测编号或时间无效")
            ensure(record.status in setOf("MEASURING", "ENDED", "UNCONFIRMED"), "检测状态无效")
            number(record.totalMl); ensure(record.totalDrops >= 0 && record.elapsedSeconds >= 0, "累计数据无效")
            ensure(record.minutes.zipWithNext().all { (a, b) -> a.timeMs < b.timeMs }, "分钟记录顺序无效")
            for (minute in record.minutes) {
                ensure(minute.timeMs >= 0 && minute.timeMs % 60_000L == 0L, "分钟时间无效")
                minute.volumeMl?.let(::number); minute.rateMlh?.let(::number); minute.cumulativeMl?.let(::number)
                ensure(minute.drops == null || minute.drops >= 0, "分钟滴数无效")
                ensure(minute.cumulativeDrops == null || minute.cumulativeDrops >= 0, "累计滴数无效")
                ensure(minute.coverageSeconds.isFinite() && minute.coverageSeconds in 0.0..60.0, "分钟覆盖时长无效")
            }
        }
        for (alarm in state.alarms) {
            ensure(alarm.id > 0 && alarm.id < state.nextAlarmId && alarm.recordId > 0 && alarm.timeMs >= 0, "警报编号或时间无效")
            ensure(alarm.code in setOf("B", "C", "D", "E"), "警报原因无效")
            ensure(alarm.endedAtMs == null || alarm.endedAtMs >= alarm.timeMs, "警报恢复时间无效")
        }
    }

    private fun number(value: Double) = ensure(value.isFinite() && value >= 0, "测量数据无效")
    private fun ensure(value: Boolean, message: String) { if (!value) throw IOException(message) }
    private fun DataOutputStream.text(value: String) {
        val bytes = value.toByteArray(Charsets.UTF_8); ensure(bytes.size <= MAX_STRING_BYTES, "记录文字超出长度上限")
        writeInt(bytes.size); write(bytes)
    }
    private fun DataInputStream.text(): String {
        val size = readInt(); ensure(size in 0..MAX_STRING_BYTES && size <= available(), "记录文字长度无效")
        val bytes = ByteArray(size); readFully(bytes)
        return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
    }
    private fun DataInputStream.count(): Int = readInt().also { ensure(it in 0..MAX_ITEMS && it <= available(), "记录数量无效") }
    private fun DataInputStream.boolean(): Boolean = readUnsignedByte().also { ensure(it in 0..1, "记录布尔值无效") } == 1
    private fun DataOutputStream.optionalDouble(value: Double?) { writeBoolean(value != null); if (value != null) writeDouble(value) }
    private fun DataOutputStream.optionalLong(value: Long?) { writeBoolean(value != null); if (value != null) writeLong(value) }
    private fun DataInputStream.optionalDouble(): Double? = if (boolean()) readDouble() else null
    private fun DataInputStream.optionalLong(): Long? = if (boolean()) readLong() else null
}
