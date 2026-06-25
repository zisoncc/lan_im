package com.lanchat

import android.content.Context
import android.content.ContentValues
import android.net.Uri
import android.provider.MediaStore
import android.util.Log
import kotlinx.coroutines.*
import java.io.*
import java.util.concurrent.atomic.AtomicInteger

/** 文件传输管理器 - 处理文件的分块发送与接收 */
class FileTransferManager(private val context: Context, private val scope: CoroutineScope) {

    private val nextTransferId = AtomicInteger(100)
    private val receiveStates = mutableMapOf<Int, ReceiveState>()
    var progressCallback: ((String, Int, Boolean) -> Unit)? = null

    data class ReceiveState(
        val fileName: String,
        val fileSize: Long,
        val totalChunks: Int,
        val uri: Uri,
        val outputStream: OutputStream,
        var receivedChunks: Int
    )

    /** 在系统 Download/LanChat 目录创建接收文件 */
    private fun createDownloadFile(fileName: String, fileSize: Long): Pair<Uri, OutputStream> {
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, fileName)
            put(MediaStore.Downloads.SIZE, fileSize)
            put(MediaStore.Downloads.RELATIVE_PATH, "Download/LanChat")
        }
        val resolver = context.contentResolver
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: throw IOException("无法创建下载文件")
        val outputStream = resolver.openOutputStream(uri)
            ?: throw IOException("无法打开下载文件")
        return uri to outputStream
    }

    /** 发送文件（通过 TcpClient） */
    fun sendFileFromUri(client: TcpClient, uri: Uri, progressCb: ((Int) -> Unit)? = null) {
        scope.launch(Dispatchers.IO) {
            try {
                val contentResolver = context.contentResolver
                val cursor = contentResolver.query(uri, null, null, null, null)
                val fileName = cursor?.use {
                    val nameIndex = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    it.moveToFirst()
                    if (nameIndex >= 0) it.getString(nameIndex) else "unknown"
                } ?: "unknown"
                cursor?.close()

                val fileSize = contentResolver.openFileDescriptor(uri, "r")?.use { desc ->
                    desc.statSize
                } ?: 0L

                if (fileSize == 0L) {
                    withContext(Dispatchers.Main) {
                        progressCallback?.invoke(fileName, 0, true)
                    }
                    return@launch
                }

                val transferId = nextTransferId.getAndIncrement()
                val totalChunks = Protocol.calculateChunks(fileSize)

                // 发送 FILE_META
                val meta = FileMeta(fileName, fileSize, transferId, totalChunks)
                client.sendRaw(Protocol.encodeFileMeta(meta))

                // 分块发送
                val inputStream = contentResolver.openInputStream(uri) ?: return@launch
                val buf = ByteArray(CHUNK_SIZE)
                var chunkIndex = 0
                var bytesRead: Int

                while (inputStream.read(buf).also { bytesRead = it } > 0) {
                    val chunkData = if (bytesRead < CHUNK_SIZE) buf.copyOf(bytesRead) else buf
                    client.sendFileData(transferId, chunkIndex, totalChunks, chunkData)
                    chunkIndex++

                    val progress = (chunkIndex * 100) / totalChunks
                    withContext(Dispatchers.Main) {
                        progressCallback?.invoke(fileName, progress, false)
                        progressCb?.invoke(progress)
                    }
                    delay(10) // 避免发送过快
                }

                inputStream.close()

                // 发送完成
                client.sendFileComplete(transferId)
                withContext(Dispatchers.Main) {
                    progressCallback?.invoke(fileName, 100, true)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    progressCallback?.invoke("文件发送失败: ${e.message}", 0, true)
                }
            }
        }
    }

    /** 开始接收文件 */
    fun startReceive(meta: FileMeta) {
        Log.d("LanChatFileTransfer", "startReceive name=${meta.fileName} size=${meta.fileSize} id=${meta.transferId} chunks=${meta.totalChunks}")
        try {
            val (uri, outputStream) = createDownloadFile(meta.fileName, meta.fileSize)
            val state = ReceiveState(
                fileName = meta.fileName,
                fileSize = meta.fileSize,
                totalChunks = meta.totalChunks,
                uri = uri,
                outputStream = outputStream,
                receivedChunks = 0
            )
            synchronized(receiveStates) {
                receiveStates[meta.transferId] = state
            }
            scope.launch(Dispatchers.Main) {
                progressCallback?.invoke(meta.fileName, 0, false)
            }
        } catch (e: Exception) {
            scope.launch(Dispatchers.Main) {
                progressCallback?.invoke("接收文件创建失败: ${e.message}", 0, true)
            }
        }
    }

    /** 接收文件数据块 */
    fun receiveData(transferId: Int, chunkIndex: Int, data: ByteArray) {
        Log.d("LanChatFileTransfer", "receiveData id=$transferId chunk=$chunkIndex size=${data.size}")
        val state: ReceiveState?
        synchronized(receiveStates) {
            state = receiveStates[transferId]
        }
        if (state == null) return

        try {
            state.outputStream.write(data)
            state.receivedChunks++

            val progress = (state.receivedChunks * 100) / state.totalChunks
            scope.launch(Dispatchers.Main) {
                progressCallback?.invoke(state.fileName, progress, false)
            }
        } catch (e: Exception) {
            scope.launch(Dispatchers.Main) {
                progressCallback?.invoke("接收写入失败: ${e.message}", 0, true)
            }
        }
    }

    /** 接收完成 */
    fun receiveComplete(transferId: Int) {
        Log.d("LanChatFileTransfer", "receiveComplete id=$transferId")
        val state: ReceiveState?
        synchronized(receiveStates) {
            state = receiveStates.remove(transferId)
        }
        state?.let {
            try { it.outputStream.close() } catch (_: Exception) {}
            scope.launch(Dispatchers.Main) {
                progressCallback?.invoke(it.fileName, 100, true)
            }
        }
    }

    /** 取消接收 */
    fun cancelReceive(transferId: Int) {
        val state: ReceiveState?
        synchronized(receiveStates) {
            state = receiveStates.remove(transferId)
        }
        state?.let {
            try { it.outputStream.close() } catch (_: Exception) {}
            try { context.contentResolver.delete(it.uri, null, null) } catch (_: Exception) {}
        }
    }
}
