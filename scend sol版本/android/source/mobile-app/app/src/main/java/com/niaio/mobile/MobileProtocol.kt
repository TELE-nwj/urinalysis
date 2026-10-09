package com.niaio.mobile

import java.math.BigDecimal

sealed interface ServerMessage
 data class Hello(val deviceId: String, val bootId: String, val sessionId: String, val simulated: Boolean = false) : ServerMessage
 data class Snapshot(val bootId: String, val sessionId: String, val seq: Long, val stateSeq: Long,
    val uptime: Long, val totalMl: Double, val rateMlh: Double, val drops: Long, val elapsed: Long,
    val state: String, val alert: String, val battery: Int?, val storage: String,
    val storageChecked: Long?, val clock: String, val timeSynced: Boolean) : ServerMessage
 data class Ack(val bootId: String, val sessionId: String, val reqId: Int, val phase: String,
    val result: String, val state: String, val stateSeq: Long) : ServerMessage
 data class ServerError(val result: String) : ServerMessage

/** No Android JSON coercion: numbers, booleans, duplicate keys and byte limits are checked here. */
object MobileProtocol {
    private val id = Regex("[A-Za-z0-9_-]{1,64}")
    private val hms = Regex("(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]")
    private val states = setOf("IDLE", "MEASURING", "PAUSED")
    private val results = setOf("OK", "BUSY", "EXPIRED", "INVALID_ARGUMENT", "NOT_READY", "INTERNAL_ERROR")
    fun parse(text: String): ServerMessage? = try {
        require(text.toByteArray(Charsets.UTF_8).size <= 1024)
        val m = Reader(text).read() as? Map<*, *> ?: error("object required")
        require(integer(m, "v") == 1L)
        when (string(m, "type")) {
            "hello" -> Hello(identifier(m, "device_id"), identifier(m, "boot_id"), identifier(m, "session_id"), m["mode"] == "SIMULATED" || m["simulated"] == true || identifier(m, "device_id").contains("SIMULATED", ignoreCase = true))
            "status" -> Snapshot(identifier(m, "boot_id"), identifier(m, "session_id"), integer(m, "seq"), integer(m, "state_seq"),
                integer(m, "sample_uptime_ms"), decimal(m, "total_ml"), decimal(m, "rate_mlh"), integer(m, "total_drops"),
                integer(m, "elapsed_s"), choice(m, "state", states), choice(m, "alert", setOf("A", "B", "C", "D", "E")),
                nullableInteger(m, "battery_percent", 100)?.toInt(), choice(m, "storage_state", setOf("UNKNOWN", "ABSENT", "READY", "ERROR")),
                nullableInteger(m, "storage_checked_uptime_ms"), string(m, "clock_hms").also { require(hms.matches(it)) },
                m["time_synced"] as? Boolean ?: error("boolean required"))
            "ack" -> Ack(identifier(m, "boot_id"), identifier(m, "session_id"), integer(m, "req_id", Int.MAX_VALUE.toLong()).also { require(it > 0) }.toInt(),
                choice(m, "phase", setOf("accepted", "completed", "rejected")), choice(m, "result", results), choice(m, "state", states), integer(m, "state_seq"))
            "error" -> ServerError(choice(m, "result", results))
            else -> error("unknown type")
        }
    } catch (_: IllegalArgumentException) { null } catch (_: IllegalStateException) { null }
    private fun string(m: Map<*, *>, key: String) = m[key] as? String ?: error("string required")
    private fun identifier(m: Map<*, *>, key: String) = string(m, key).also { require(id.matches(it)) }
    private fun choice(m: Map<*, *>, key: String, options: Set<String>) = string(m, key).also { require(it in options) }
    private fun integer(m: Map<*, *>, key: String, max: Long = Long.MAX_VALUE): Long {
        val raw = m[key] as? NumberToken ?: error("number required")
        require(raw.raw.matches(Regex("0|[1-9][0-9]*")))
        return raw.raw.toLongOrNull()?.also { require(it <= max) } ?: error("integer overflow")
    }
    private fun nullableInteger(m: Map<*, *>, key: String, max: Long = Long.MAX_VALUE): Long? {
        require(m.containsKey(key)); return if (m[key] == null) null else integer(m, key, max)
    }
    private fun decimal(m: Map<*, *>, key: String): Double {
        val token = m[key] as? NumberToken ?: error("number required")
        val value = BigDecimal(token.raw); require(value.signum() >= 0)
        return value.toDouble().also { require(it.isFinite()) }
    }
    private data class NumberToken(val raw: String)
    private class Reader(private val text: String) {
        private var p = 0
        fun read(): Any? { val result = value(0); whitespace(); require(p == text.length); return result }
        private fun whitespace() { while (p < text.length && text[p] in " \t\n\r") p++ }
        private fun take(c: Char): Boolean { whitespace(); return if (p < text.length && text[p] == c) { p++; true } else false }
        private fun value(depth: Int): Any? {
            require(depth < 32); whitespace(); require(p < text.length)
            return when (text[p]) {
                '{' -> { p++; val map = linkedMapOf<String, Any?>(); if (!take('}')) {
                    do { whitespace(); val k = quoted(); require(!map.containsKey(k)); require(take(':')); map[k] = value(depth + 1) } while (take(','))
                    require(take('}')) }; map }
                '[' -> { p++; val a = mutableListOf<Any?>(); if (!take(']')) { do { a += value(depth + 1) } while (take(',')); require(take(']')) }; a }
                '"' -> quoted()
                't' -> literal("true", true)
                'f' -> literal("false", false)
                'n' -> literal("null", null)
                else -> { val match = Regex("-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?").find(text, p) ?: error("bad JSON")
                    require(match.range.first == p); p = match.range.last + 1; NumberToken(match.value) }
            }
        }
        private fun literal(s: String, result: Any?): Any? { require(text.startsWith(s, p)); p += s.length; return result }
        private fun quoted(): String {
            require(p < text.length && text[p++] == '"'); val out = StringBuilder()
            while (p < text.length) { val c = text[p++]; if (c == '"') return out.toString(); require(c >= ' ')
                if (c != '\\') out.append(c) else { require(p < text.length); when (val e = text[p++]) {
                    '"', '\\', '/' -> out.append(e); 'b' -> out.append('\b'); 'f' -> out.append('\u000c'); 'n' -> out.append('\n'); 'r' -> out.append('\r'); 't' -> out.append('\t')
                    'u' -> { require(p + 4 <= text.length); val hex = text.substring(p, p + 4); require(hex.all { it in "0123456789abcdefABCDEF" }); out.append(hex.toInt(16).toChar()); p += 4 }
                    else -> error("bad escape")
                } }
            }; error("unterminated string")
        }
    }
}

