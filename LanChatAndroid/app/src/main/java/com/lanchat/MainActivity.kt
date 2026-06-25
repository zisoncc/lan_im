package com.lanchat

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.lanchat.ui.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class Screen {
    MODE_SELECTION, CLIENT_SCAN, SERVER_RUNNING, CHAT_CLIENT, CHAT_SERVER
}

class MainActivity : ComponentActivity() {

    private lateinit var discoveryClient: DiscoveryClient
    private lateinit var tcpClient: TcpClient
    private lateinit var tcpServer: TcpServer
    private lateinit var fileTransfer: FileTransferManager
    private val messages = mutableStateListOf<ChatMessage>()
    private val discoveredServers = mutableStateListOf<ServerInfo>()
    private var isScanning by mutableStateOf(false)
    private var isServerRunning by mutableStateOf(false)
    private var localIp by mutableStateOf("")
    private var currentScreen by mutableStateOf(Screen.MODE_SELECTION)
    private var chatTitle by mutableStateOf("\u804A\u5929")
    private var clientName: String by mutableStateOf("")
    private val serverClients = mutableStateListOf<TcpServer.ClientState>()
    private var selectedServerClientId by mutableStateOf<String?>(null)
    private val serverConversations = mutableMapOf<String, SnapshotStateList<ChatMessage>>()
    private val serverFileOwnersByName = mutableMapOf<String, String>()
    private val nextServerTransferId = java.util.concurrent.atomic.AtomicInteger(1000)
    private val startupPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { grants ->
            val denied = grants.filterValues { !it }.keys
            if (denied.isNotEmpty()) {
                Toast.makeText(this, "\u90E8\u5206\u6743\u9650\u672A\u6388\u4E88\uFF0C\u6587\u4EF6\u6536\u53D1\u6216\u5C40\u57DF\u7F51\u53D1\u73B0\u53EF\u80FD\u53D7\u5F71\u54CD", Toast.LENGTH_LONG).show()
            }
        }
    private val networkPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { grants ->
            if (grants.values.all { it }) {
                enterClientMode()
            } else {
                Toast.makeText(this, "\u9700\u8981\u5c40\u57df\u7f51\u53d1\u73b0\u6743\u9650\u624d\u80fd\u626b\u63cf\u670d\u52a1\u7aef", Toast.LENGTH_SHORT).show()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val scope = lifecycleScope
        discoveryClient = DiscoveryClient(this, scope)
        tcpClient = TcpClient(scope)
        tcpServer = TcpServer(scope)
        fileTransfer = FileTransferManager(this, scope)

        // 设备名称
        clientName = android.os.Build.MODEL

        setupCallbacks()
        requestStartupPermissionsIfNeeded()

        setContent {
            LanChatTheme {
                when (currentScreen) {
                    Screen.MODE_SELECTION -> ModeSelectionScreen(
                        onSelectServer = {
                            currentScreen = Screen.SERVER_RUNNING
                            startServer()
                        },
                        onSelectClient = {
                            requestClientPermissionsThenStart()
                        }
                    )
                    Screen.CLIENT_SCAN -> ClientScreen(
                        discoveredServers = discoveredServers,
                        isScanning = isScanning,
                        onStartScan = { startScan() },
                        onStopScan = { stopScan() },
                        onConnect = { server -> connectToServer(server) },
                        onBack = {
                            stopScan()
                            currentScreen = Screen.MODE_SELECTION
                        }
                    )
                    Screen.SERVER_RUNNING -> ServerScreen(
                        localIp = localIp,
                        isRunning = isServerRunning,
                        clientList = serverClients,
                        selectedClientId = selectedServerClientId,
                        onStop = {
                            stopServer()
                            currentScreen = Screen.MODE_SELECTION
                        },
                        onClientClick = { client -> openServerChat(client.id) },
                        onBack = {
                            stopServer()
                            currentScreen = Screen.MODE_SELECTION
                        }
                    )
                    Screen.CHAT_CLIENT -> ChatScreen(
                        title = chatTitle,
                        messages = messages,
                        onSendText = { text -> sendText(text) },
                        onSendFile = { uri -> sendFile(uri) },
                        onBack = {
                            tcpClient.disconnect()
                            messages.clear()
                            currentScreen = Screen.CLIENT_SCAN
                        }
                    )
                    Screen.CHAT_SERVER -> ChatScreen(
                        title = chatTitle,
                        messages = selectedServerClientId?.let { serverConversation(it) } ?: emptyList(),
                        onSendText = { text -> sendTextToSelectedServerClient(text) },
                        onSendFile = { uri -> serverSendFile(uri) },
                        onBack = {
                            currentScreen = Screen.SERVER_RUNNING
                        }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        tcpClient.disconnect()
        tcpServer.stop()
        discoveryClient.stopDiscovery()
    }

    // ---- 设置回调 ----
    private fun setupCallbacks() {
        discoveryClient.onServerDiscovered = { server ->
            if (discoveredServers.none { it.ip == server.ip }) {
                discoveredServers.add(server)
            }
        }
        discoveryClient.onDiscoveryStarted = { isScanning = true }
        discoveryClient.onDiscoveryStopped = { isScanning = false }
        discoveryClient.onDiscoveryError = { error ->
            Toast.makeText(this, "\u626b\u63cf\u5931\u8d25: $error", Toast.LENGTH_SHORT).show()
            isScanning = false
        }

        // TCP 客户端回调
        tcpClient.onConnected = { name ->
            chatTitle = "\u5DF2\u8FDE\u63A5 - $name"
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = true, sender = "",
                text = "\u5DF2\u8FDE\u63A5\u5230\u670D\u52A1\u7AEF: $name"
            ))
            currentScreen = Screen.CHAT_CLIENT
        }
        tcpClient.onDisconnected = { reason ->
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = true, sender = "",
                text = "\u8FDE\u63A5\u65AD\u5F00: $reason"
            ))
            if (currentScreen == Screen.CHAT_CLIENT) {
                currentScreen = Screen.CLIENT_SCAN
            }
        }
        tcpClient.onTextMessage = { sender, text ->
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = false, sender = sender, text = text
            ))
        }
        tcpClient.onFileMeta = { meta -> fileTransfer.startReceive(meta) }
        tcpClient.onFileData = { transferId, chunkIndex, data ->
            fileTransfer.receiveData(transferId, chunkIndex, data)
        }
        tcpClient.onFileComplete = { transferId -> fileTransfer.receiveComplete(transferId) }
        tcpClient.onFileCancel = { transferId -> fileTransfer.cancelReceive(transferId) }

        // 文件传输进度
        fileTransfer.progressCallback = { fileName, progress, isComplete ->
            val targetMessages = serverFileOwnersByName[fileName]?.let { serverConversation(it) } ?: messages
            val idx = targetMessages.indexOfLast { it is ChatMessage.FileProgress && it.fileName == fileName }
            val msg = ChatMessage.FileProgress(
                timestamp = System.currentTimeMillis(), isLocal = true,
                fileName = fileName, progress = progress, isComplete = isComplete
            )
            if (idx >= 0) targetMessages[idx] = msg else targetMessages.add(msg)
        }

        // 服务端回调
        tcpServer.onServerStarted = { ip ->
            localIp = ip
            isServerRunning = true
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = true, sender = "",
                text = "\u670D\u52A1\u7AEF\u5DF2\u542F\u52A8, IP: $ip"
            ))
        }
        tcpServer.onServerStopped = {
            isServerRunning = false
            serverClients.clear()
            selectedServerClientId = null
            serverConversations.clear()
            serverFileOwnersByName.clear()
        }
        tcpServer.onClientConnected = { _, name ->
            refreshServerClients()
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = true, sender = "",
                text = "\u5BA2\u6237\u7AEF\u5DF2\u8FDE\u63A5: $name"
            ))
        }
        tcpServer.onClientDisconnected = { id ->
            refreshServerClients()
            if (selectedServerClientId == id) {
                selectedServerClientId = null
                currentScreen = Screen.SERVER_RUNNING
            }
            messages.add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = true, sender = "",
                text = "\u5BA2\u6237\u7AEF\u5DF2\u65AD\u5F00: $id"
            ))
        }
        tcpServer.onTextMessage = { clientId, text ->
            val client = tcpServer.getClients().find { it.id == clientId }
            val name = client?.name ?: clientId
            serverConversation(clientId).add(ChatMessage.TextMessage(
                timestamp = System.currentTimeMillis(), isLocal = false, sender = name, text = text
            ))
        }
        tcpServer.onFileMeta = { clientId, meta ->
            serverFileOwnersByName[meta.fileName] = clientId
            fileTransfer.startReceive(meta)
        }
        tcpServer.onFileData = { _, transferId, chunkIndex, data ->
            fileTransfer.receiveData(transferId, chunkIndex, data)
        }
        tcpServer.onFileComplete = { _, transferId -> fileTransfer.receiveComplete(transferId) }
        tcpServer.onFileCancel = { _, transferId -> fileTransfer.cancelReceive(transferId) }
    }

    private fun refreshServerClients() {
        serverClients.clear()
        serverClients.addAll(tcpServer.getClients())
    }

    private fun serverConversation(clientId: String): SnapshotStateList<ChatMessage> {
        return serverConversations.getOrPut(clientId) { mutableStateListOf() }
    }

    private fun openServerChat(clientId: String) {
        val client = tcpServer.getClients().find { it.id == clientId }
        selectedServerClientId = clientId
        chatTitle = "\u4E0E ${client?.name ?: clientId} \u804A\u5929"
        serverConversation(clientId)
        currentScreen = Screen.CHAT_SERVER
    }

    // ---- 操作函数 ----
    private fun requestClientPermissionsThenStart() {
        val missingPermissions = requiredClientPermissions().filter { permission ->
            ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED
        }
        if (missingPermissions.isEmpty()) {
            enterClientMode()
        } else {
            networkPermissionLauncher.launch(missingPermissions.toTypedArray())
        }
    }

    private fun requestStartupPermissionsIfNeeded() {
        val missingPermissions = requiredStartupPermissions().filter { permission ->
            ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED
        }
        if (missingPermissions.isNotEmpty()) {
            startupPermissionLauncher.launch(missingPermissions.toTypedArray())
        }
    }

    private fun requiredStartupPermissions(): List<String> {
        val permissions = mutableListOf<String>()
        permissions.addAll(requiredClientPermissions())
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
            permissions.add(Manifest.permission.READ_EXTERNAL_STORAGE)
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            permissions.add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }
        return permissions.distinct()
    }

    private fun requiredClientPermissions(): List<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            listOf(Manifest.permission.NEARBY_WIFI_DEVICES)
        } else {
            listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private fun enterClientMode() {
        currentScreen = Screen.CLIENT_SCAN
        startScan()
    }

    private fun startScan() {
        discoveredServers.clear()
        discoveryClient.startDiscovery()
    }

    private fun stopScan() {
        discoveryClient.stopDiscovery()
    }

    private fun connectToServer(server: ServerInfo) {
        lifecycleScope.launch {
            val success = tcpClient.connect(server, clientName)
            if (!success) {
                Toast.makeText(this@MainActivity, "\u8FDE\u63A5\u5931\u8D25", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun startServer() {
        messages.clear()
        selectedServerClientId = null
        serverConversations.clear()
        serverFileOwnersByName.clear()
        tcpServer.start("AndroidServer-$clientName")
        chatTitle = "\u670D\u52A1\u7AEF"
    }

    private fun stopServer() {
        tcpServer.stop()
        messages.clear()
        selectedServerClientId = null
        serverConversations.clear()
        serverFileOwnersByName.clear()
    }

    private fun sendText(text: String) {
        messages.add(ChatMessage.TextMessage(
            timestamp = System.currentTimeMillis(), isLocal = true, sender = "", text = text
        ))
        tcpClient.sendText(text)
    }

    private fun sendTextToSelectedServerClient(text: String) {
        val clientId = selectedServerClientId
        if (clientId == null) {
            Toast.makeText(this, "\u8BF7\u5148\u9009\u62E9\u5BA2\u6237\u7AEF", Toast.LENGTH_SHORT).show()
            return
        }
        serverConversation(clientId).add(ChatMessage.TextMessage(
            timestamp = System.currentTimeMillis(), isLocal = true, sender = "", text = text
        ))
        lifecycleScope.launch(Dispatchers.IO) {
            tcpServer.sendToClient(clientId, Protocol.encodeText(text))
        }
    }

    private fun sendFile(uri: android.net.Uri) {
        fileTransfer.sendFileFromUri(tcpClient, uri)
    }

    private fun serverSendFile(uri: android.net.Uri) {
        val clientId = selectedServerClientId
        if (clientId == null) {
            Toast.makeText(this, "\u8BF7\u5148\u9009\u62E9\u5BA2\u6237\u7AEF", Toast.LENGTH_SHORT).show()
            return
        }
        val targetMessages = serverConversation(clientId)
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val cursor = contentResolver.query(uri, null, null, null, null)
                val fileName = cursor?.use {
                    val nameIdx = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    it.moveToFirst()
                    if (nameIdx >= 0) it.getString(nameIdx) else "unknown"
                } ?: "unknown"
                cursor?.close()

                val fileSize = contentResolver.openFileDescriptor(uri, "r")?.use { it.statSize } ?: 0L
                if (fileSize == 0L) return@launch

                val transferId = nextServerTransferId.getAndIncrement()
                val totalChunks = Protocol.calculateChunks(fileSize)
                val meta = FileMeta(fileName, fileSize, transferId, totalChunks)

                tcpServer.sendToClient(clientId, Protocol.encodeFileMeta(meta))

                val inputStream = contentResolver.openInputStream(uri) ?: return@launch
                val buf = ByteArray(CHUNK_SIZE)
                var chunkIndex = 0
                var bytesRead: Int

                while (inputStream.read(buf).also { bytesRead = it } > 0) {
                    val chunkData = if (bytesRead < CHUNK_SIZE) buf.copyOf(bytesRead) else buf
                    tcpServer.sendToClient(clientId, Protocol.encodeFileData(transferId, chunkIndex, totalChunks, chunkData))
                    chunkIndex++

                    val progress = (chunkIndex * 100) / totalChunks
                    withContext(Dispatchers.Main) {
                        val idx = targetMessages.indexOfLast { it is ChatMessage.FileProgress && it.fileName == fileName }
                        val msg = ChatMessage.FileProgress(
                            timestamp = System.currentTimeMillis(), isLocal = true,
                            fileName = fileName, progress = progress, isComplete = false
                        )
                        if (idx >= 0) targetMessages[idx] = msg else targetMessages.add(msg)
                    }
                    delay(10)
                }
                inputStream.close()

                tcpServer.sendToClient(clientId, Protocol.encodeFileComplete(transferId))
                withContext(Dispatchers.Main) {
                    val idx = targetMessages.indexOfLast { it is ChatMessage.FileProgress && it.fileName == fileName }
                    if (idx >= 0) {
                        targetMessages[idx] = ChatMessage.FileProgress(
                            timestamp = System.currentTimeMillis(), isLocal = true,
                            fileName = fileName, progress = 100, isComplete = true
                        )
                    }
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    targetMessages.add(ChatMessage.FileProgress(
                        timestamp = System.currentTimeMillis(), isLocal = true,
                        fileName = "\u53D1\u9001\u5931\u8D25: ${e.message}", progress = 0, isComplete = true
                    ))
                }
            }
        }
    }

    companion object {
        /** Kotlin 协程 delay 工具 */
        private suspend fun delay(ms: Long) = kotlinx.coroutines.delay(ms)
    }
}
