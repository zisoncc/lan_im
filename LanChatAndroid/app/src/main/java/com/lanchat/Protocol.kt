package com.lanchat

import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

// ---- 协议常量 ----
const val UDP_DISCOVER_PORT = 54320
const val TCP_DATA_PORT = 54321
const val DISCOVER_REQ_STR = "LANCHAT_DISCOVER_REQ"
const val DISCOVER_RSP_PREFIX = "LANCHAT_DISCOVER_RSP|"
const val HEARTBEAT_INTERVAL_MS = 5000L
const val CHUNK_SIZE = 65536

// ---- 消息类型 ----
enum class MessageType(val value: Byte) {
    HEARTBEAT(0x01),
    TEXT_MSG(0x02),
    FILE_META(0x03),
    FILE_ACK(0x04),
    FILE_DATA(0x05),
    FILE_COMPLETE(0x06),
    FILE_CANCEL(0x07),
    CLIENT_NAME(0x08);

    companion object {
        fun fromByte(b: Byte): MessageType? =
            entries.find { it.value == b }
    }
}

// ---- 数据模型 ----
data class FileMeta(
    val fileName: String,
    val fileSize: Long,
    val transferId: Int,
    val totalChunks: Int
)

data class ParsedPacket(
    val type: MessageType,
    val payload: ByteArray
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other == null || this::class != other::class) return false
        other as ParsedPacket
        return type == other.type && payload.contentEquals(other.payload)
    }
    override fun hashCode(): Int {
        var result = type.hashCode()
        result = 31 * result + payload.contentHashCode()
        return result
    }
}

data class ServerInfo(
    val name: String,
    val ip: String,
    val port: Int
)

// ---- 协议编解码 ----
object Protocol {

    /** 编码一个完整的帧: [4B payloadLen(net)] [1B type] [payload] */
    fun encodePacket(type: MessageType, payload: ByteArray = byteArrayOf()): ByteArray {
        val buf = ByteBuffer.allocate(5 + payload.size).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(payload.size)
            put(type.value)
            put(payload)
        }
        return buf.array()
    }

    fun encodeText(text: String): ByteArray =
        encodePacket(MessageType.TEXT_MSG, text.toByteArray(Charsets.UTF_8))

    fun encodeFileMeta(meta: FileMeta): ByteArray {
        val str = "${meta.fileName}|${meta.fileSize}|${meta.transferId}|${meta.totalChunks}"
        return encodePacket(MessageType.FILE_META, str.toByteArray(Charsets.UTF_8))
    }

    fun encodeFileAck(transferId: Int, chunkIndex: Int): ByteArray {
        val buf = ByteBuffer.allocate(8).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(transferId)
            putInt(chunkIndex)
        }
        return encodePacket(MessageType.FILE_ACK, buf.array())
    }

    fun encodeFileData(transferId: Int, chunkIndex: Int, totalChunks: Int, data: ByteArray): ByteArray {
        val buf = ByteBuffer.allocate(12 + data.size).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(transferId)
            putInt(chunkIndex)
            putInt(totalChunks)
            put(data)
        }
        return encodePacket(MessageType.FILE_DATA, buf.array())
    }

    fun encodeFileComplete(transferId: Int): ByteArray {
        val buf = ByteBuffer.allocate(4).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(transferId)
        }
        return encodePacket(MessageType.FILE_COMPLETE, buf.array())
    }

    fun encodeFileCancel(transferId: Int): ByteArray {
        val buf = ByteBuffer.allocate(4).apply {
            order(ByteOrder.BIG_ENDIAN)
            putInt(transferId)
        }
        return encodePacket(MessageType.FILE_CANCEL, buf.array())
    }

    fun encodeHeartbeat(): ByteArray =
        encodePacket(MessageType.HEARTBEAT)

    fun encodeClientName(name: String): ByteArray =
        encodePacket(MessageType.CLIENT_NAME, name.toByteArray(Charsets.UTF_8))

    /** 从缓冲区解析一个包，返回解析结果和消耗的字节数 */
    data class ParseResult(val packet: ParsedPacket, val consumed: Int)

    fun tryParse(buffer: ByteArray): ParseResult? {
        if (buffer.size < 5) return null
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.BIG_ENDIAN)
        val payloadLen = bb.getInt()
        val typeByte = bb.get()
        val totalLen = 5 + payloadLen
        if (buffer.size < totalLen) return null
        val type = MessageType.fromByte(typeByte) ?: return null
        val payload = buffer.copyOfRange(5, totalLen)
        return ParseResult(ParsedPacket(type, payload), totalLen)
    }

    fun calculateChunks(fileSize: Long): Int =
        ((fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE).toInt()

    /** 从输入流读取一个完整帧，返回 null 表示流关闭 */
    fun readPacket(stream: InputStream): ParsedPacket? {
        val header = ByteArray(5)
        var offset = 0
        while (offset < 5) {
            val n = stream.read(header, offset, 5 - offset)
            if (n < 0) return null
            offset += n
        }
        val bb = ByteBuffer.wrap(header).order(ByteOrder.BIG_ENDIAN)
        val payloadLen = bb.getInt()
        val typeByte = bb.get()
        val type = MessageType.fromByte(typeByte) ?: return null

        val payload = if (payloadLen > 0) {
            val buf = ByteArray(payloadLen)
            var off = 0
            while (off < payloadLen) {
                val n = stream.read(buf, off, payloadLen - off)
                if (n < 0) return null
                off += n
            }
            buf
        } else byteArrayOf()

        return ParsedPacket(type, payload)
    }

    /** 从 FILE_META 的 payload 解析 FileMeta */
    fun parseFileMeta(payload: ByteArray): FileMeta? {
        val str = String(payload, Charsets.UTF_8)
        val parts = str.split("|")
        if (parts.size < 4) return null
        return try {
            FileMeta(
                fileName = parts[0],
                fileSize = parts[1].toLong(),
                transferId = parts[2].toInt(),
                totalChunks = parts[3].toInt()
            )
        } catch (e: NumberFormatException) { null }
    }
}
