package com.niaio.mobile

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.assertEquals
import org.junit.Test

class ProbePolicyTest {
    @Test fun onlyDeviceSubnetAddressesAreEligible() {
        assertTrue(ProbePolicy.isDeviceAddress("192.168.4.2"))
        assertTrue(ProbePolicy.isDeviceAddress("192.168.4.254"))
        listOf("192.168.1.2", "10.0.0.2", "192.168.4.0", "192.168.4.255",
            "192.168.4.999", "192.168.4.1", "192.168.4.2.evil", "192.168.4.+2",
            "192.168.4.02", "192.168.4.2\n", "::1", "").forEach {
            assertFalse(it, ProbePolicy.isDeviceAddress(it))
        }
    }
    @Test fun e0PayloadIsFixedSimulatedEchoWithoutBusinessCommands() {
        assertEquals("SIMULATED G0 echo", ProbePolicy.ECHO)
        assertEquals("ws://192.168.4.1/ws", ProbePolicy.URL)
        assertTrue(ProbePolicy.ECHO.toByteArray(Charsets.UTF_8).size <= 1024)
        listOf("time_sync", "start", "pause", "reset").forEach {
            assertFalse(ProbePolicy.ECHO.contains(it))
        }
    }
}
