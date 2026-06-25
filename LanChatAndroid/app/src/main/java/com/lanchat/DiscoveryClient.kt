package com.lanchat

import android.content.Context
import android.net.wifi.WifiManager
import kotlinx.coroutines.*
import java.net.*
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** UDP 服务发现客户端 */
class DiscoveryClient(
    private val context: Context,
    private val scope: CoroutineScope
) {

    private var job: Job? = null
    private val servers = mutableListOf<ServerInfo>()

    /** 发现结果回调 */
    var onServerDiscovered: ((ServerInfo) -> Unit)? = null
    var onDiscoveryStarted: (() -> Unit)? = null
    var onDiscoveryStopped: (() -> Unit)? = null
    var onDiscoveryError: ((String) -> Unit)? = null

    fun startDiscovery() {
        stopDiscovery()
        onDiscoveryStarted?.invoke()
        job = scope.launch(Dispatchers.IO) {
            var socket: DatagramSocket? = null
            var multicastLock: WifiManager.MulticastLock? = null
            try {
                val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
                multicastLock = wifiManager?.createMulticastLock("LanChatDiscovery")?.apply {
                    setReferenceCounted(false)
                    acquire()
                }

                socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    broadcast = true
                    soTimeout = 2000
                    bind(InetSocketAddress(InetAddress.getByName("0.0.0.0"), 0))
                }

                val sendBuf = DISCOVER_REQ_STR.toByteArray(Charsets.UTF_8)
                val recvBuf = ByteArray(1024)
                val broadcastAddresses = getBroadcastAddresses(wifiManager)

                while (isActive) {
                    // 广播发现请求
                    broadcastAddresses.forEach { address ->
                        try {
                            val packet = DatagramPacket(sendBuf, sendBuf.size, address, UDP_DISCOVER_PORT)
                            socket.send(packet)
                        } catch (_: Exception) {}
                    }

                    // 接收响应
                    try {
                        while (isActive) {
                            val recvPacket = DatagramPacket(recvBuf, recvBuf.size)
                            socket.receive(recvPacket)
                            val response = String(
                                recvPacket.data, recvPacket.offset, recvPacket.length,
                                Charsets.UTF_8
                            )
                            if (response.startsWith(DISCOVER_RSP_PREFIX)) {
                                val parts = response.removePrefix(DISCOVER_RSP_PREFIX).split("|")
                                if (parts.size >= 2) {
                                    val info = ServerInfo(
                                        name = parts[0],
                                        ip = parts.getOrElse(1) { recvPacket.address.hostAddress ?: "" },
                                        port = parts.getOrElse(2) { TCP_DATA_PORT.toString() }.toIntOrNull() ?: TCP_DATA_PORT
                                    )
                                    withContext(Dispatchers.Main) {
                                        if (servers.none { it.ip == info.ip }) {
                                            servers.add(info)
                                            onServerDiscovered?.invoke(info)
                                        }
                                    }
                                }
                            }
                        }
                    } catch (_: SocketTimeoutException) {}

                    delay(3000)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    onDiscoveryError?.invoke(e.message ?: e::class.java.simpleName)
                }
            } finally {
                socket?.close()
                try {
                    multicastLock?.release()
                } catch (_: Exception) {}
                withContext(Dispatchers.Main) {
                    onDiscoveryStopped?.invoke()
                }
            }
        }
    }

    fun stopDiscovery() {
        job?.cancel()
        job = null
        servers.clear()
    }

    fun getDiscoveredServers(): List<ServerInfo> = servers.toList()

    private fun getBroadcastAddresses(wifiManager: WifiManager?): List<InetAddress> {
        val addresses = linkedSetOf<InetAddress>()
        addresses.add(InetAddress.getByName("255.255.255.255"))

        val dhcpInfo = wifiManager?.dhcpInfo
        if (dhcpInfo != null && dhcpInfo.ipAddress != 0 && dhcpInfo.netmask != 0) {
            val broadcast = (dhcpInfo.ipAddress and dhcpInfo.netmask) or dhcpInfo.netmask.inv()
            val bytes = ByteBuffer
                .allocate(Int.SIZE_BYTES)
                .order(ByteOrder.LITTLE_ENDIAN)
                .putInt(broadcast)
                .array()
            addresses.add(InetAddress.getByAddress(bytes))
        }

        try {
            NetworkInterface.getNetworkInterfaces().asSequence()
                .filter { it.isUp && !it.isLoopback }
                .flatMap { it.interfaceAddresses.asSequence() }
                .mapNotNull { it.broadcast }
                .forEach { addresses.add(it) }
        } catch (_: Exception) {}

        return addresses.toList()
    }
}
