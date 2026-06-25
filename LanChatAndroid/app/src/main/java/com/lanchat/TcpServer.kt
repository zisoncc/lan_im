package com.lanchat

import kotlinx.coroutines.*
import java.io.*
import java.net.*
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/** TCP 服务端 - 接受客户端连接并转发消息 */
class TcpServer(private val scope: CoroutineScope) {

    private var udpJob: Job? = null
    private var acceptJob: Job? = null
    private var heartbeatJob: Job? = null
    private val serverSocket = java.util.concurrent.atomic.AtomicReference<ServerSocket?>()
    private val clients = ConcurrentHashMap<Socket, ClientState>()
    private val running = java.util.concurrent.atomic.AtomicBoolean(false)
    private val nextClientId = AtomicInteger(0)
    private val sendLocks = ConcurrentHashMap<Socket, Any>()

    var serverName: String = "AndroidServer"
    var onClientConnected: ((String, String) -> Unit)? = null // clientId, name
    var onClientDisconnected: ((String) -> Unit)? = null
    var onTextMessage: ((String, String) -> Unit)? = null     // clientId, text
    var onFileMeta: ((String, FileMeta) -> Unit)? = null
    var onFileData: ((String, Int, Int, ByteArray) -> Unit)? = null
    var onFileComplete: ((String, Int) -> Unit)? = null
    var onFileCancel: ((String, Int) -> Unit)? = null
    var onServerStarted: ((String) -> Unit)? = null  // 本地IP
    var onServerStopped: (() -> Unit)? = null
    var onClientListChanged: (() -> Unit)? = null

    data class ClientState(
        val id: String,
        var name: String,
        val socket: Socket,
        val ip: String
    )

    fun start(name: String) {
        stop()
        serverName = name
        running.set(true)
        val localIp = getLocalIpAddress()

        // UDP 发现响应
        udpJob = scope.launch(Dispatchers.IO) {
            try {
                val udpSocket = DatagramSocket(UDP_DISCOVER_PORT).apply {
                    broadcast = true
                    soTimeout = 1000
                }
                val recvBuf = ByteArray(256)
                while (isActive && running.get()) {
                    try {
                        val packet = DatagramPacket(recvBuf, recvBuf.size)
                        udpSocket.receive(packet)
                        val req = String(packet.data, packet.offset, packet.length, Charsets.UTF_8)
                        if (req == DISCOVER_REQ_STR) {
                            val rsp = "${DISCOVER_RSP_PREFIX}${serverName}|${localIp}|${TCP_DATA_PORT}"
                            val rspData = rsp.toByteArray(Charsets.UTF_8)
                            val rspPacket = DatagramPacket(rspData, rspData.size, packet.address, packet.port)
                            udpSocket.send(rspPacket)
                        }
                    } catch (_: SocketTimeoutException) {}
                }
                udpSocket.close()
            } catch (_: Exception) {}
        }

        // TCP 监听
        acceptJob = scope.launch(Dispatchers.IO) {
            try {
                val srv = ServerSocket(TCP_DATA_PORT)
                serverSocket.set(srv)
                withContext(Dispatchers.Main) {
                    onServerStarted?.invoke(localIp)
                }
                while (isActive && running.get()) {
                    try {
                        val client = srv.accept()
                        val ip = client.inetAddress.hostAddress ?: "unknown"
                        val id = "client_${nextClientId.incrementAndGet()}"
                        val state = ClientState(id = id, name = ip, socket = client, ip = ip)
                        clients[client] = state

                        // 启动客户端接收协程
                        scope.launch(Dispatchers.IO) {
                            handleClient(client, id)
                        }

                        withContext(Dispatchers.Main) {
                            onClientConnected?.invoke(id, ip)
                            onClientListChanged?.invoke()
                        }
                    } catch (_: Exception) { if (!running.get()) break }
                }
            } catch (_: Exception) {
                withContext(Dispatchers.Main) { onServerStopped?.invoke() }
            }
        }

        // 心跳监控 (每5秒移除超时客户端)
        heartbeatJob = scope.launch(Dispatchers.IO) {
            while (isActive && running.get()) {
                delay(HEARTBEAT_INTERVAL_MS * 2)
                // 简单心跳: 发送心跳包
                broadcastRaw(Protocol.encodeHeartbeat())
            }
        }
    }

    fun stop() {
        running.set(false)
        udpJob?.cancel()
        acceptJob?.cancel()
        heartbeatJob?.cancel()
        try { serverSocket.get()?.close() } catch (_: Exception) {}
        serverSocket.set(null)
        clients.keys.forEach { try { it.close() } catch (_: Exception) {} }
        clients.clear()
        sendLocks.clear()
        onServerStopped?.invoke()
    }

    fun isRunning(): Boolean = running.get()

    fun getClients(): List<ClientState> = clients.values.toList()

    /** 向所有客户端广播文本 */
    fun broadcastText(text: String) {
        scope.launch(Dispatchers.IO) {
            broadcastRaw(Protocol.encodeText(text))
        }
    }

    /** 向所有客户端广播数据 */
    fun broadcastRaw(data: ByteArray) {
        val iter = clients.entries.iterator()
        while (iter.hasNext()) {
            val (sock, _) = iter.next()
            try {
                val lock = sendLocks.getOrPut(sock) { Any() }
                synchronized(lock) {
                    val os = BufferedOutputStream(sock.getOutputStream())
                    os.write(data)
                    os.flush()
                }
            } catch (_: Exception) {
                iter.remove()
                sendLocks.remove(sock)
                scope.launch(Dispatchers.Main) {
                    onClientListChanged?.invoke()
                }
            }
        }
    }

    /** 向指定客户端发送数据 */
    fun sendToClient(clientId: String, data: ByteArray) {
        val entry = clients.entries.find { it.value.id == clientId } ?: return
        try {
            val lock = sendLocks.getOrPut(entry.key) { Any() }
            synchronized(lock) {
                val os = BufferedOutputStream(entry.key.getOutputStream())
                os.write(data)
                os.flush()
            }
        } catch (_: Exception) {
            removeClient(entry.key)
        }
    }

    private fun handleClient(sock: Socket, clientId: String) {
        try {
            val input = BufferedInputStream(sock.getInputStream())
            while (running.get()) {
                val packet = Protocol.readPacket(input) ?: break
                val state = clients[sock]
                val cid = state?.id ?: clientId
                when (packet.type) {
                    MessageType.CLIENT_NAME -> {
                        val name = String(packet.payload, Charsets.UTF_8)
                        clients[sock]?.name = name
                        scope.launch(Dispatchers.Main) {
                            onClientListChanged?.invoke()
                        }
                    }
                    MessageType.TEXT_MSG -> {
                        val text = String(packet.payload, Charsets.UTF_8)
                        val senderName = clients[sock]?.name ?: cid
                        // 广播给其他客户端
                        val display = "${senderName}: ${text}"
                        sendToAllExcept(sock, Protocol.encodeText(display))
                        scope.launch(Dispatchers.Main) {
                            onTextMessage?.invoke(cid, text)
                        }
                    }
                    MessageType.FILE_META -> {
                        Protocol.parseFileMeta(packet.payload)?.let { meta ->
                            onFileMeta?.invoke(cid, meta)
                        }
                    }
                    MessageType.FILE_DATA -> {
                        if (packet.payload.size >= 12) {
                            val bb = java.nio.ByteBuffer.wrap(packet.payload)
                                .order(java.nio.ByteOrder.BIG_ENDIAN)
                            val tid = bb.getInt(); val chunk = bb.getInt()
                            val total = bb.getInt()
                            val data = packet.payload.copyOfRange(12, packet.payload.size)
                            onFileData?.invoke(cid, tid, chunk, data)
                        }
                    }
                    MessageType.FILE_COMPLETE -> {
                        // 转发给其他客户端
                        sendToAllExcept(sock, Protocol.encodePacket(MessageType.FILE_COMPLETE, packet.payload))
                        if (packet.payload.size >= 4) {
                            val tid = java.nio.ByteBuffer.wrap(packet.payload)
                                .order(java.nio.ByteOrder.BIG_ENDIAN).getInt()
                            scope.launch(Dispatchers.Main) {
                                onFileComplete?.invoke(cid, tid)
                            }
                        }
                    }
                    MessageType.FILE_CANCEL -> {
                        // 转发给其他客户端
                        sendToAllExcept(sock, Protocol.encodePacket(MessageType.FILE_CANCEL, packet.payload))
                        if (packet.payload.size >= 4) {
                            val tid = java.nio.ByteBuffer.wrap(packet.payload)
                                .order(java.nio.ByteOrder.BIG_ENDIAN).getInt()
                            scope.launch(Dispatchers.Main) {
                                onFileCancel?.invoke(cid, tid)
                            }
                        }
                    }
                    else -> {} // HEARTBEAT 等忽略
                }
            }
        } catch (_: Exception) {}
        removeClient(sock)
    }

    private fun sendToAllExcept(except: Socket, data: ByteArray) {
        clients.entries.forEach { (sock, _) ->
            if (sock != except) {
                try {
                    val lock = sendLocks.getOrPut(sock) { Any() }
                    synchronized(lock) {
                        val os = BufferedOutputStream(sock.getOutputStream())
                        os.write(data)
                        os.flush()
                    }
                } catch (_: Exception) {
                    removeClient(sock)
                }
            }
        }
    }

    private fun removeClient(sock: Socket) {
        val state = clients.remove(sock)
        sendLocks.remove(sock)
        try { sock.close() } catch (_: Exception) {}
        if (state != null) {
            scope.launch(Dispatchers.Main) {
                onClientDisconnected?.invoke(state.id)
                onClientListChanged?.invoke()
            }
        }
    }

    companion object {
        fun getLocalIpAddress(): String {
            try {
                val en = NetworkInterface.getNetworkInterfaces()
                while (en.hasMoreElements()) {
                    val intf = en.nextElement()
                    if (intf.isLoopback || !intf.isUp) continue
                    val addrs = intf.inetAddresses
                    while (addrs.hasMoreElements()) {
                        val addr = addrs.nextElement()
                        if (addr is Inet4Address && !addr.isLoopbackAddress) {
                            return addr.hostAddress ?: ""
                        }
                    }
                }
            } catch (_: Exception) {}
            return "127.0.0.1"
        }
    }
}
