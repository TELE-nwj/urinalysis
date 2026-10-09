package com.niaio.mobile

import org.junit.Assert.*
import org.junit.Test

class MobileProtocolTest {
    private val hello = """{"v":1,"type":"hello","device_id":"NIAIO-1234","boot_id":"b1","session_id":"s1"}"""
    private fun status(seq: Int = 1, stateSeq: Int = 0, state: String = "IDLE") = """{"v":1,"type":"status","boot_id":"b1","session_id":"s1","seq":$seq,"state_seq":$stateSeq,"sample_uptime_ms":1500,"total_ml":125.0,"rate_mlh":60.0,"total_drops":250,"elapsed_s":90201,"state":"$state","alert":"A","battery_percent":0,"storage_state":"UNKNOWN","storage_checked_uptime_ms":null,"clock_hms":"18:30:25","time_synced":true}"""
    private fun ack(id: Int, phase: String, stateSeq: Int, state: String = "IDLE") = """{"v":1,"type":"ack","boot_id":"b1","session_id":"s1","req_id":$id,"phase":"$phase","result":"OK","state":"$state","state_seq":$stateSeq}"""
    @Test fun validatesStrictTypesDuplicateKeysBoundsAndAllowsExtensions() {
        assertNotNull(MobileProtocol.parse(status().dropLast(1) + ",\"extension\":{\"x\":1}}"))
        listOf(status().replace("\"v\":1", "\"v\":1.0"), status().replace("\"v\":1", "\"v\":1,\"v\":1"),
            status().replace("\"battery_percent\":0", "\"battery_percent\":101"), status().replace("18:30:25", "24:00:00"),
            status().replace("125.0", "1e999"), status().replace("\"total_drops\":250", "\"total_drops\":250.0"),
            status().replace("\"elapsed_s\":90201", "\"elapsed_s\":-1"), status().replace("\"time_synced\":true", "\"time_synced\":\"true\""),
            hello.replace("s1", "bad id"), "{}", hello + " garbage").forEach { assertNull(it, MobileProtocol.parse(it)) }
    }
    @Test fun syncIsOncePerSessionAndUsesSameRequestForThreeAttempts() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56")
        val first = e.drain().single(); assertTrue(first.contains("\"hour\":12")); assertFalse(first.contains("date"))
        e.receive(hello, 1, "13:00:00"); assertTrue(e.drain().isEmpty())
        e.tick(5000); assertEquals(first, e.drain().single()); e.tick(10000); assertEquals(first, e.drain().single())
        e.tick(15000); assertTrue(e.drain().isEmpty()); assertEquals("对时失败", e.view.sync)
        e.receive(ack(1, "completed", 0), 16000, "13:00:00"); e.tick(30000)
        assertEquals("时间已同步", e.view.sync); assertTrue(e.drain().isEmpty())
        e.disconnect(); e.open(); e.receive(hello.replace("s1", "s2"), 40000, "13:00:00")
        assertEquals(1, e.drain().size)
    }
    @Test fun acceptedIsNotCompleteTimeoutAndLateAckCannotRewindState() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56"); e.drain()
        e.receive(status(), 100, "12:34:56"); assertTrue(e.command("start", 200)); e.drain()
        e.receive(ack(2, "accepted", 0), 300, "12:34:56"); assertTrue(e.view.waiting)
        e.tick(5200); assertEquals("执行情况未确认", e.view.control); assertTrue(e.view.waiting)
        e.receive(status(2, 3, "PAUSED"), 5300, "12:34:56")
        e.receive(ack(2, "completed", 1, "MEASURING"), 5400, "12:34:56")
        assertEquals("PAUSED", e.view.snapshot?.state); assertEquals(3L, e.view.stateSeq); assertFalse(e.view.waiting)
    }
    @Test fun disconnectNeverReplaysCommandsOldSessionAndDuplicateStatusDoNotRefresh() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56"); e.drain()
        e.receive(status(), 100, "12:34:56"); e.receive(status(), 3000, "12:34:56"); e.tick(3101)
        assertTrue(e.view.stale); assertFalse(e.command("start", 3101))
        e.receive(status(2), 3200, "12:34:56"); assertTrue(e.command("reset", 3300)); e.drain()
        e.disconnect(); e.open(); e.receive(hello.replace("s1", "s2"), 4000, "12:34:56")
        assertTrue(e.drain().single().contains("time_sync")); e.receive(status(3), 4100, "12:34:56")
        assertNull(e.view.snapshot); assertFalse(e.view.waiting)
    }
    @Test fun elapsedAndBatteryHaveHonestFormatting() {
        assertEquals("25:03:21", DisplayFormat.elapsed(90201)); assertEquals("00:00:00", DisplayFormat.elapsed(0))
        assertEquals("--", DisplayFormat.battery(null)); assertEquals("0%", DisplayFormat.battery(0))
    }
    @Test fun completedSyncStopsRetryAndRejectedControlUnlocksWithoutStateChange() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56"); e.drain()
        e.receive(ack(1, "accepted", 0), 100, "12:34:56"); assertEquals("时间未同步", e.view.sync)
        e.receive(ack(1, "completed", 0), 200, "12:34:56"); e.tick(10000); assertTrue(e.drain().isEmpty())
        e.receive(status(), 10100, "12:34:56"); assertTrue(e.command("start", 10200)); e.drain()
        e.receive(ack(2, "rejected", 0).replace("\"OK\"", "\"BUSY\""), 10300, "12:34:56")
        assertFalse(e.view.waiting); assertEquals("IDLE", e.view.snapshot?.state); assertTrue(e.view.control.contains("BUSY"))
        assertTrue(e.command("start", 10400)); assertTrue(e.drain().single().contains("\"req_id\":3"))
    }
    @Test fun terminalAckCannotBeDowngradedByLateAcceptedAndUnknownAckCannotChangeState() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56"); e.drain()
        e.receive(status(), 100, "12:34:56"); assertTrue(e.command("start", 200)); e.drain()
        e.receive(ack(2, "completed", 1, "MEASURING"), 300, "12:34:56")
        e.receive(ack(2, "accepted", 0), 400, "12:34:56")
        e.receive(ack(99, "completed", 99, "PAUSED"), 500, "12:34:56")
        assertFalse(e.view.waiting); assertEquals("执行完成", e.view.control)
        assertEquals("MEASURING", e.view.snapshot?.state); assertEquals(1L, e.view.stateSeq)
    }
    @Test fun simulatedServerHelloIsFlaggedAndOldBootAndOldSequenceCannotUpdate() {
        val e = SessionEngine(); e.open(); e.receive(hello.dropLast(1) + ",\"mode\":\"SIMULATED\"}", 0, "12:34:56"); e.drain()
        assertTrue(e.view.simulated); e.receive(status(2, 2, "PAUSED"), 100, "12:34:56")
        e.receive(status(3, 3).replace("b1", "b0"), 1000, "12:34:56")
        e.receive(status(1, 1), 2000, "12:34:56"); e.tick(3101)
        assertEquals("PAUSED", e.view.snapshot?.state); assertEquals(2L, e.view.stateSeq); assertTrue(e.view.stale)
    }
    @Test fun invalidFramesMissingFieldsAndIntegerRequestIdsAreRejected() {
        listOf(status().replace("\"seq\":1", "\"seq\":1.0"), status().replace("\"total_ml\":125.0,", ""),
            ack(1, "completed", 0).replace("\"req_id\":1", "\"req_id\":1.0"), ack(1, "completed", 0).replace("\"req_id\":1", "\"req_id\":0"),
            ack(1, "completed", 0).replace("\"req_id\":1", "\"req_id\":2147483648"), hello.dropLast(1) + ",\"extra\":{\"x\":1,\"x\":2}}",
            hello.replace("hello", "future"), " " .repeat(1025), status().replace("UNKNOWN", "BROKEN")).forEach { assertNull(it, MobileProtocol.parse(it)) }
    }
    @Test fun reusedEndedSessionIsNotSynchronizedAgain() {
        val e = SessionEngine(); e.open(); e.receive(hello, 0, "12:34:56"); e.drain()
        e.disconnect(); e.open(); e.receive(hello, 1000, "13:00:00")
        assertTrue(e.drain().isEmpty()); assertFalse(e.view.ready)
    }
}
