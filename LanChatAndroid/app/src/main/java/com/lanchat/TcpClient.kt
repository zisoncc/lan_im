package com.lanchat

import kotlinx.coroutines.*
import android.util.Log
import java.io.*
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

/** TCP 客户端 - 连接到服务端 */
class TcpClient(private val scope: CoroutineScope) {

    private var socket: Socket? = null
    private var recvJob: Job? = null
    private var heartbeatJob: Job? = null
    private val outputStream = java.util.concurrent.atomic.AtomicReference<DataOutputStream?>()
    private val connected = AtomicBoolean(false)
    private val disconnectNotified = AtomicBoolean(false)

    var serverInfo: ServerInfo? = null

    // 回调
    var onConnected: ((String) -> Unit)? = null   // 参数: 服务端名称
    var onDisconnected: ((String) -> Unit)? = null // 参数: 原因
    var onTextMessage: ((String, String) -> Unit)? = null // 发送者, 文本
    var onFileMeta: ((FileMeta) -> Unit)? = null
    var onFileData: ((Int, Int, ByteArray) -> Unit)? = null // transferId, chunkIndex, data
    var onFileComplete: ((Int) -> Unit)? = null
    var onFileCancel: ((Int) -> Unit)? = null

    suspend fun connect(server: ServerInfo, clientName: String): Boolean = withContext(Dispatchers.IO) {
        disconnect()
        serverInfo = server
        return@withContext try {
            val sock = Socket(server.ip, server.port)
            sock.soTimeout = 0  // 阻塞读
            socket = sock
            outputStream.set(DataOutputStream(BufferedOutputStream(sock.getOutputStream())))
            connected.set(true)
            disconnectNotified.set(false)

            // 发送客户端名称
            sendRaw(Protocol.encodeClientName(clientName))

            // 启动接收协程
            recvJob = scope.launch(Dispatchers.IO) {
                try {
                    val inputStream = BufferedInputStream(sock.getInputStream())
                    while (isActive && connected.get()) {
                        val packet = Protocol.readPacket(inputStream)
                            ?: break // 流关闭
                        handlePacket(packet)
                    }
                } catch (_: IOException) {
                } finally {
                    notifyDisconnected("连接断开")
                }
            }

            // 心跳
            heartbeatJob = scope.launch(Dispatchers.IO) {
                while (isActive && connected.get()) {
                    delay(HEARTBEAT_INTERVAL_MS)
                    sendRaw(Protocol.encodeHeartbeat())
                }
            }

            withContext(Dispatchers.Main) {
                onConnected?.invoke(server.name)
            }
            true
        } catch (e: Exception) {
            withContext(Dispatchers.Main) {
                onDisconnected?.invoke("连接失败: ${e.message}")
            }
            false
        }
    }

    fun disconnect() {
        connected.set(false)
        heartbeatJob?.cancel()
        recvJob?.cancel()
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        outputStream.set(null)
        serverInfo = null
    }

    fun isConnected(): Boolean = connected.get()

    fun sendText(text: String) {
        scope.launch(Dispatchers.IO) {
            sendRaw(Protocol.encodeText(text))
        }
    }

    fun sendRaw(data: ByteArray) {
        val os = outputStream.get() ?: return
        try {
            os.write(data)
            os.flush()
        } catch (_: Exception) {
            disconnect()
            notifyDisconnected("发送失败")
        }
    }

    fun sendFileData(transferId: Int, chunkIndex: Int, totalChunks: Int, data: ByteArray) {
        sendRaw(Protocol.encodeFileData(transferId, chunkIndex, totalChunks, data))
    }

    fun sendFileAck(transferId: Int, chunkIndex: Int) {
        sendRaw(Protocol.encodeFileAck(transferId, chunkIndex))
    }

    fun sendFileComplete(transferId: Int) {
        sendRaw(Protocol.encodeFileComplete(transferId))
    }

    fun sendFileCancel(transferId: Int) {
        sendRaw(Protocol.encodeFileCancel(transferId))
    }

    private fun notifyDisconnected(reason: String) {
        if (disconnectNotified.compareAndSet(false, true)) {
            disconnect()
            scope.launch(Dispatchers.Main) {
                onDisconnected?.invoke(reason)
            }
        }
    }

    private suspend fun handlePacket(packet: ParsedPacket) {
        Log.d("LanChatTcpClient", "packet=${packet.type} payload=${packet.payload.size}")
        when (packet.type) {
            MessageType.TEXT_MSG -> {
                val text = String(packet.payload, Charsets.UTF_8)
                withContext(Dispatchers.Main) {
                    onTextMessage?.invoke("", text)
                }
            }
            MessageType.FILE_META -> {
                Protocol.parseFileMeta(packet.payload)?.let { onFileMeta?.invoke(it) }
            }
            MessageType.FILE_DATA -> {
                if (packet.payload.size >= 12) {
                    val bb = java.nio.ByteBuffer.wrap(packet.payload).order(java.nio.ByteOrder.BIG_ENDIAN)
                    val transferId = bb.getInt()
                    val chunkIndex = bb.getInt()
                    val totalChunks = bb.getInt()
                    val data = packet.payload.copyOfRange(12, packet.payload.size)
                    onFileData?.invoke(transferId, chunkIndex, data)
                }
            }
            MessageType.FILE_COMPLETE -> {
                if (packet.payload.size >= 4) {
                    val transferId = java.nio.ByteBuffer.wrap(packet.payload)
                        .order(java.nio.ByteOrder.BIG_ENDIAN).getInt()
                    onFileComplete?.invoke(transferId)
                }
            }
            MessageType.FILE_CANCEL -> {
                if (packet.payload.size >= 4) {
                    val transferId = java.nio.ByteBuffer.wrap(packet.payload)
                        .order(java.nio.ByteOrder.BIG_ENDIAN).getInt()
                    onFileCancel?.invoke(transferId)
                }
            }
            MessageType.CLIENT_NAME -> {
                // 客户端收到服务端名称更新
                val name = String(packet.payload, Charsets.UTF_8)
                serverInfo?.let { serverInfo = it.copy(name = name) }
            }
            else -> {} // HEARTBEAT 等忽略
        }
    }
}
