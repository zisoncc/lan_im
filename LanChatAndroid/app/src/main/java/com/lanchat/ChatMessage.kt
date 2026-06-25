package com.lanchat

import java.io.File

/** 聊天消息数据模型 */
sealed class ChatMessage {
    abstract val timestamp: Long
    abstract val isLocal: Boolean

    data class TextMessage(
        override val timestamp: Long,
        override val isLocal: Boolean,
        val sender: String,
        val text: String
    ) : ChatMessage()

    data class FileProgress(
        override val timestamp: Long,
        override val isLocal: Boolean,
        val fileName: String,
        val progress: Int,  // 0-100
        val isComplete: Boolean = false
    ) : ChatMessage()
}
