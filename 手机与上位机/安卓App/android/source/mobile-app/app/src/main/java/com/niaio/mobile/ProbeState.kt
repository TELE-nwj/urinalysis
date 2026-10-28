package com.niaio.mobile

data class ProbeState(
    val status: String = "设备未连接", val connecting: Boolean = false, val connected: Boolean = false,
    val wifiAddress: String? = null, val log: List<String> = emptyList(),
    val session: SessionView = SessionView(), val simulated: Boolean = false,
)
