package com.lanchat.ui

import android.content.ActivityNotFoundException
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.lanchat.*

/** 时间格式化 */
private fun formatTime(ts: Long): String {
    val cal = java.util.Calendar.getInstance().apply { timeInMillis = ts }
    return String.format("%02d:%02d:%02d", cal.get(java.util.Calendar.HOUR_OF_DAY),
        cal.get(java.util.Calendar.MINUTE), cal.get(java.util.Calendar.SECOND))
}

@Composable
fun ChatScreen(
    title: String,
    messages: List<ChatMessage>,
    onSendText: (String) -> Unit,
    onSendFile: (Uri) -> Unit,
    onBack: () -> Unit
) {
    var inputText by remember { mutableStateOf("") }
    val listState = rememberLazyListState()
    val context = LocalContext.current
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let { onSendFile(it) }
    }

    // 自动滚动到底部
    val lastMsgCount = messages.size
    LaunchedEffect(lastMsgCount) {
        if (messages.isNotEmpty()) {
            listState.animateScrollToItem(messages.size - 1)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        // 顶部栏
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colorScheme.primary,
            shadowElevation = 4.dp
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 8.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回", tint = Color.White)
                }
                Text(
                    text = title,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                    modifier = Modifier.weight(1f)
                )
            }
        }

        // 消息列表
        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            state = listState,
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            items(messages) { msg ->
                MessageItem(msg)
            }
        }

        // 底部输入栏
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shadowElevation = 8.dp,
            color = Color.White
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // 文件按钮
                IconButton(onClick = {
                    try {
                        filePickerLauncher.launch(arrayOf("*/*"))
                    } catch (_: ActivityNotFoundException) {
                        Toast.makeText(context, "\u672A\u627E\u5230\u53EF\u7528\u7684\u6587\u4EF6\u9009\u62E9\u5668", Toast.LENGTH_SHORT).show()
                    }
                }) {
                    Icon(
                        Icons.Default.AttachFile,
                        contentDescription = "发送文件",
                        tint = MaterialTheme.colorScheme.primary
                    )
                }

                // 文本输入框
                OutlinedTextField(
                    value = inputText,
                    onValueChange = { inputText = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("输入消息...") },
                    singleLine = true,
                    shape = RoundedCornerShape(24.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        unfocusedBorderColor = Color.Transparent,
                        focusedBorderColor = Color.Transparent
                    )
                )

                // 发送按钮
                IconButton(
                    onClick = {
                        val text = inputText.trim()
                        if (text.isNotEmpty()) {
                            onSendText(text)
                            inputText = ""
                        }
                    },
                    enabled = inputText.trim().isNotEmpty()
                ) {
                    Icon(
                        Icons.Default.Send,
                        contentDescription = "发送",
                        tint = if (inputText.trim().isNotEmpty())
                            MaterialTheme.colorScheme.primary else Color.Gray
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun MessageItem(msg: ChatMessage) {
    when (msg) {
        is ChatMessage.TextMessage -> {
            val isLocal = msg.isLocal
            val clipboardManager = LocalClipboardManager.current
            val context = LocalContext.current
            var isFocused by remember { mutableStateOf(false) }
            val bgColor = if (isLocal)
                MaterialTheme.colorScheme.primaryContainer
            else
                Color(0xFFE0E0E0)
            val bubbleShape = RoundedCornerShape(
                topStart = 16.dp, topEnd = 16.dp,
                bottomStart = if (isLocal) 16.dp else 4.dp,
                bottomEnd = if (isLocal) 4.dp else 16.dp
            )
            fun copyText() {
                clipboardManager.setText(AnnotatedString(msg.text))
                Toast.makeText(context, "\u5DF2\u590D\u5236", Toast.LENGTH_SHORT).show()
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 2.dp),
                horizontalAlignment = if (isLocal) Alignment.End else Alignment.Start
            ) {
                // 发送者
                if (msg.sender.isNotEmpty() && !isLocal) {
                    Text(
                        text = msg.sender,
                        fontSize = 12.sp,
                        color = Color.Gray,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)
                    )
                }
                Surface(
                    shape = bubbleShape,
                    color = bgColor,
                    modifier = Modifier
                        .widthIn(max = 520.dp)
                        .onFocusChanged { isFocused = it.isFocused }
                        .border(
                            width = if (isFocused) 2.dp else 0.dp,
                            color = if (isFocused) MaterialTheme.colorScheme.primary else Color.Transparent,
                            shape = bubbleShape
                        )
                        .combinedClickable(
                            onClick = { copyText() },
                            onLongClick = { copyText() }
                        )
                        .focusable()
                ) {
                    Column(modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp)) {
                        Text(
                            text = msg.text,
                            fontSize = 16.sp,
                            color = Color.Black
                        )
                        Text(
                            text = formatTime(msg.timestamp),
                            fontSize = 10.sp,
                            color = Color.Gray,
                            modifier = Modifier.align(Alignment.End)
                        )
                    }
                }
            }
        }

        is ChatMessage.FileProgress -> {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 2.dp),
                shape = RoundedCornerShape(8.dp),
                color = Color(0xFFF3E5F5)
            ) {
                Row(
                    modifier = Modifier.padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Default.Description,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = if (msg.isComplete)
                                "\u2714 ${msg.fileName} \u4f20\u8f93\u5b8c\u6210"
                            else
                                "${if (msg.isLocal) "\u53d1\u9001" else "\u63a5\u6536"} ${msg.fileName}  ${msg.progress}%",
                            fontSize = 14.sp,
                            color = if (msg.isComplete) Color(0xFF2E7D32) else Color.Black
                        )
                        if (!msg.isComplete) {
                            Spacer(modifier = Modifier.height(4.dp))
                            LinearProgressIndicator(
                                progress = msg.progress / 100f,
                                modifier = Modifier.fillMaxWidth(),
                                color = MaterialTheme.colorScheme.primary,
                                trackColor = Color(0xFFE0E0E0)
                            )
                        }
                    }
                }
            }
        }
    }
}
