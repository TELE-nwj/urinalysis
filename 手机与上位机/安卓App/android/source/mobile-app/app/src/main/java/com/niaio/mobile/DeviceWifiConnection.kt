package com.niaio.mobile

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import okhttp3.*
import okio.ByteString
import java.net.Proxy
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit

/** Every socket is explicitly bound to a selected local Wi-Fi Network; never cellular. */
class DeviceWifiConnection(context: Context, private val onState: (ProbeState) -> Unit) {
    private val manager = context.getSystemService(ConnectivityManager::class.java)
    private val main = Handler(Looper.getMainLooper())
    private var state = ProbeState()
    private val engine = SessionEngine()
    private var generation = 0L
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var selectedNetwork: Network? = null
    private var client: OkHttpClient? = null
    private var socket: WebSocket? = null
    private var timeout: Runnable? = null
    private val heartbeat = object : Runnable {
        override fun run() {
            engine.tick(SystemClock.elapsedRealtime()); flush(); update(state.copy(session = engine.view))
            if (state.connected) main.postDelayed(this, 250)
        }
    }
    fun connect() {
        check(Looper.myLooper() == Looper.getMainLooper())
        disconnect(log = false)
        val attempt = generation
        update(state.copy(status = "正在连接", connecting = true, simulated = false))
        append("INFO", "只使用 192.168.4.* Wi-Fi；不使用蜂窝网络")
        val networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { if (attempt == generation) openIfEligible(network, manager.getLinkProperties(network), attempt) }
            override fun onLinkPropertiesChanged(network: Network, properties: LinkProperties) { if (attempt == generation) openIfEligible(network, properties, attempt) }
            override fun onLost(network: Network) { if (attempt == generation && network == selectedNetwork) fail("设备 Wi-Fi 已断开") }
            override fun onUnavailable() { if (attempt == generation) fail("未找到设备 Wi-Fi，请先通过系统设置连接热点") }
        }
        callback = networkCallback
        timeout = Runnable { if (attempt == generation && !engine.view.ready) fail("连接超时，请确认设备 Wi-Fi 与固件") }.also { main.postDelayed(it, 12000) }
        try {
            val request = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build()
            manager.requestNetwork(request, networkCallback, main, 10000)
        } catch (e: SecurityException) { fail("系统拒绝 Wi-Fi 网络请求：${e.message}") }
          catch (e: RuntimeException) { fail("Wi-Fi 网络请求失败：${e.message}") }
    }
    private fun openIfEligible(network: Network, properties: LinkProperties?, attempt: Long) {
        if (selectedNetwork != null) return
        val capabilities = manager.getNetworkCapabilities(network) ?: return
        if (!capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) return
        val address = properties?.linkAddresses?.mapNotNull { it.address.hostAddress }?.firstOrNull(ProbePolicy::isDeviceAddress) ?: return
        selectedNetwork = network
        update(state.copy(status = "正在连接 WebSocket", wifiAddress = address))
        append("INFO", "Wi-Fi 地址 $address，连接 ${ProbePolicy.URL}")
        val boundClient = OkHttpClient.Builder().socketFactory(network.socketFactory)
            .dns(object : Dns { override fun lookup(hostname: String): List<java.net.InetAddress> = network.getAllByName(hostname).toList() }).proxy(Proxy.NO_PROXY)
            .protocols(listOf(Protocol.HTTP_1_1)).retryOnConnectionFailure(false)
            .connectTimeout(5, TimeUnit.SECONDS).readTimeout(0, TimeUnit.MILLISECONDS).pingInterval(5, TimeUnit.SECONDS).build()
        client = boundClient
        socket = boundClient.newWebSocket(Request.Builder().url(ProbePolicy.URL).build(), object : WebSocketListener() {
            private fun deliver(action: () -> Unit) { main.post { if (attempt == generation) action() } }
            override fun onOpen(webSocket: WebSocket, response: Response) = deliver {
                engine.open(); update(state.copy(status = "已连接 · 等待设备信息", connecting = false, connected = true))
                append("OPEN", "HTTP ${response.code}"); main.post(heartbeat)
            }
            override fun onMessage(webSocket: WebSocket, text: String) = deliver {
                if (text.toByteArray(Charsets.UTF_8).size > 1024) { webSocket.close(1009, "frame too large"); fail("拒绝超过 1024 字节的帧") }
                else {
                    append("RX", text)
                    engine.receive(text, SystemClock.elapsedRealtime(), SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(Date()))
                    flush(); update(state.copy(session = engine.view, status = if (engine.view.ready) "已连接" else state.status))
                    if (engine.view.ready) cancelTimeout()
                }
            }
            override fun onMessage(webSocket: WebSocket, bytes: ByteString) = deliver { webSocket.close(1003, "text only"); fail("拒绝二进制帧") }
            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) = deliver { webSocket.close(code, reason); fail("设备关闭连接：$code $reason") }
            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = deliver { fail("设备连接已关闭：$code $reason") }
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = deliver { fail("WebSocket 失败：${t.message ?: t.javaClass.simpleName}") }
        })
    }
    private fun flush() {
        for (payload in engine.drain()) {
            if (state.simulated) append("SIMULATED TX", payload)
            else if (socket?.send(payload) == true) append("TX", payload)
            else { fail("请求未进入发送队列，执行情况未确认"); return }
        }
    }
    fun command(action: String) {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (!state.simulated && !engine.view.simulated && engine.command(action, SystemClock.elapsedRealtime())) { flush(); update(state.copy(session = engine.view)) }
    }
    /** Explicit static sample: never opens a socket or enables measurement controls. */
    fun setSimulated(enabled: Boolean) {
        disconnect(log = false)
        update(state.copy(simulated = enabled, session = SessionView()))
        if (!enabled) return
        engine.open()
        val demoSession = "demo-$generation"
        engine.receive("""{"v":1,"type":"hello","device_id":"SIMULATED","boot_id":"demo","session_id":"$demoSession"}""", SystemClock.elapsedRealtime(), "12:34:56")
        flush()
        engine.receive("""{"v":1,"type":"status","boot_id":"demo","session_id":"$demoSession","seq":1,"state_seq":0,"sample_uptime_ms":1500,"total_ml":125.0,"rate_mlh":60.0,"total_drops":250,"elapsed_s":90201,"state":"IDLE","alert":"A","battery_percent":0,"storage_state":"UNKNOWN","storage_checked_uptime_ms":null,"clock_hms":"12:34:56","time_synced":false}""", SystemClock.elapsedRealtime(), "12:34:56")
        update(state.copy(status = "SIMULATED · 静态样例", connected = true, session = engine.view)); main.post(heartbeat)
    }
    fun disconnect(log: Boolean = true) {
        check(Looper.myLooper() == Looper.getMainLooper())
        generation++; main.removeCallbacks(heartbeat); cancelTimeout(); engine.disconnect()
        callback?.let { try { manager.unregisterNetworkCallback(it) } catch (_: IllegalArgumentException) { } }
        callback = null; socket?.cancel(); socket = null
        client?.connectionPool?.evictAll(); client?.dispatcher?.executorService?.shutdown(); client = null; selectedNetwork = null
        update(state.copy(status = "设备未连接", connecting = false, connected = false, wifiAddress = null, session = engine.view))
        if (log) append("INFO", "已断开；不重发旧控制请求")
    }
    private fun cancelTimeout() { timeout?.let(main::removeCallbacks); timeout = null }
    private fun fail(message: String) { disconnect(log = false); update(state.copy(status = "设备离线")); append("ERROR", message) }
    fun clearLog() = update(state.copy(log = emptyList()))
    private fun append(direction: String, message: String) {
        val stamp = SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(Date())
        update(state.copy(log = (state.log + "$stamp $direction ${message.take(1024)}").takeLast(200)))
    }
    private fun update(value: ProbeState) { state = value; onState(value) }
}


