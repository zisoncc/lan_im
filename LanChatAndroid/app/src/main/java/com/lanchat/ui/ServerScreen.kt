package com.lanchat.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.lanchat.TcpServer
import com.lanchat.UDP_DISCOVER_PORT
import com.lanchat.TCP_DATA_PORT

@Composable
fun ServerScreen(
    localIp: String,
    isRunning: Boolean,
    clientList: List<TcpServer.ClientState>,
    selectedClientId: String?,
    onStop: () -> Unit,
    onClientClick: (TcpServer.ClientState) -> Unit,
    onBack: () -> Unit
) {
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
                    .padding(horizontal = 16.dp, vertical = 16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextButton(onClick = onBack) {
                    Text("< \u8FD4\u56DE", color = Color.White, fontSize = 16.sp)
                }
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = "\u670D\u52A1\u7AEF\u6A21\u5F0F",
                    fontSize = 20.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White
                )
                Spacer(modifier = Modifier.weight(1f))
                Button(
                    onClick = onStop,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFE53935)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("\u505C\u6B62", color = Color.White)
                }
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            // 运行状态
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(
                    containerColor = if (isRunning) Color(0xFFE8F5E9) else Color(0xFFFFEBEE)
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(12.dp)
                                .background(
                                    if (isRunning) Color(0xFF4CAF50) else Color(0xFFE53935),
                                    shape = RoundedCornerShape(6.dp)
                                )
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (isRunning) "\u670D\u52A1\u7AEF\u8FD0\u884C\u4E2D" else "\u670D\u52A1\u7AEF\u5DF2\u505C\u6B62",
                            fontSize = 18.sp,
                            fontWeight = FontWeight.Bold,
                            color = if (isRunning) Color(0xFF2E7D32) else Color(0xFFC62828)
                        )
                    }
                    if (isRunning) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(text = "\u672C\u673A IP: $localIp", fontSize = 14.sp, color = Color.DarkGray)
                        Text(text = "\u53D1\u73B0\u7AEF\u53E3: $UDP_DISCOVER_PORT", fontSize = 14.sp, color = Color.DarkGray)
                        Text(text = "\u6570\u636E\u7AEF\u53E3: $TCP_DATA_PORT", fontSize = 14.sp, color = Color.DarkGray)
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // 客户端列表
            Text(
                text = "\u5DF2\u8FDE\u63A5\u5BA2\u6237\u7AEF (${clientList.size})",
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(bottom = 8.dp)
            )

            if (clientList.isEmpty()) {
                Box(
                    modifier = Modifier.fillMaxWidth().height(100.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(text = "\u6682\u65E0\u5BA2\u6237\u7AEF\u8FDE\u63A5", fontSize = 14.sp, color = Color.Gray)
                }
            } else {
                LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    items(clientList) { client ->
                        val isSelected = client.id == selectedClientId
                        Card(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onClientClick(client) }
                                .border(
                                    width = if (isSelected) 2.dp else 0.dp,
                                    color = if (isSelected) MaterialTheme.colorScheme.primary else Color.Transparent,
                                    shape = RoundedCornerShape(8.dp)
                                ),
                            shape = RoundedCornerShape(8.dp),
                            elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
                            colors = CardDefaults.cardColors(
                                containerColor = if (isSelected)
                                    MaterialTheme.colorScheme.primaryContainer
                                else
                                    MaterialTheme.colorScheme.surface
                            )
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth().padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.Person, contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary)
                                Spacer(modifier = Modifier.width(8.dp))
                                Column {
                                    Text(text = client.name, fontSize = 16.sp, fontWeight = FontWeight.Medium)
                                    Text(text = "IP: ${client.ip}", fontSize = 12.sp, color = Color.Gray)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
