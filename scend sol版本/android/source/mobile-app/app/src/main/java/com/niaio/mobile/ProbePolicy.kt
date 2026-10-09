package com.niaio.mobile

/** E0 device-network eligibility and fixed diagnostic traffic; no business commands. */
object ProbePolicy {
    const val URL = "ws://192.168.4.1/ws"
    const val ECHO = "SIMULATED G0 echo"
    private val deviceAddress = Regex("192\\.168\\.4\\.([1-9][0-9]{0,2})")
    fun isDeviceAddress(address: String): Boolean {
        val match = deviceAddress.matchEntire(address) ?: return false
        return match.groupValues[1].toInt() in 2..254
    }
}
