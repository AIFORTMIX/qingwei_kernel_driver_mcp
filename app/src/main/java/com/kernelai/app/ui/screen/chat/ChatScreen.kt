package com.kernelai.app.ui.screen.chat

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.Circle
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.kernelai.app.data.model.ChatMessage
import com.kernelai.app.data.model.ConnectionStatus
import com.kernelai.app.data.model.MessageRole
import com.kernelai.app.data.model.ToolCallInfo
import com.kernelai.app.data.model.ToolCallStatus
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import com.kernelai.app.ui.viewmodel.ChatViewModel
import kotlinx.coroutines.delay
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * AI 聊天界面 - 支持消息气泡、Markdown 渲染、工具调用指示器、连接状态
 */
@Composable
fun ChatScreen(
    viewModel: ChatViewModel = viewModel(),
    contentPadding: PaddingValues = PaddingValues(0.dp)
) {
    val messages by viewModel.messages.collectAsStateWithLifecycle()
    val isStreaming by viewModel.isStreaming.collectAsStateWithLifecycle()
    val inputText by viewModel.inputText.collectAsStateWithLifecycle()
    val connectionStatus by viewModel.connectionStatus.collectAsStateWithLifecycle()
    val listState = rememberLazyListState()

    // 自动滚动到最新消息
    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) {
            listState.animateScrollToItem(messages.size - 1)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(contentPadding)
            .imePadding()
    ) {
        // 连接状态栏
        ConnectionStatusBar(connectionStatus)

        // 消息列表
        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
            state = listState,
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (messages.isEmpty()) {
                item {
                    EmptyChatPlaceholder()
                }
            }

            items(messages, key = { it.id }) { message ->
                ChatMessageBubble(message)
            }
        }

        // 输入区域
        ChatInputBar(
            text = inputText,
            onTextChange = viewModel::updateInputText,
            onSend = viewModel::sendMessage,
            onStop = viewModel::stopStreaming,
            isStreaming = isStreaming,
            isEnabled = true
        )
    }
}

/**
 * 连接状态指示器
 */
@Composable
fun ConnectionStatusBar(status: ConnectionStatus) {
    val uiMode = LocalUiMode.current
    val bgColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = bgColor,
        tonalElevation = 1.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.SpaceEvenly,
            verticalAlignment = Alignment.CenterVertically
        ) {
            StatusIndicator(
                label = "驱动",
                connected = status.driverConnected
            )
            StatusIndicator(
                label = "MCP",
                connected = status.mcpServerRunning
            )
            StatusIndicator(
                label = "AI",
                connected = status.aiConnected
            )
        }
    }
}

@Composable
fun StatusIndicator(label: String, connected: Boolean) {
    val color = if (connected) Color(0xFF4CAF50) else Color(0xFFF44336)
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(
            imageVector = Icons.Rounded.Circle,
            contentDescription = null,
            tint = color,
            modifier = Modifier.size(10.dp)
        )
        Spacer(modifier = Modifier.width(4.dp))
        Text(
            text = "$label ${if (connected) "已连接" else "未连接"}",
            fontSize = 11.sp,
            fontWeight = FontWeight.Medium
        )
    }
}

/**
 * 空聊天占位符
 */
@Composable
fun EmptyChatPlaceholder() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 80.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "轻微 MCP",
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "内核调试 AI 助手",
            fontSize = 14.sp
        )
        Spacer(modifier = Modifier.height(24.dp))
        Text(
            text = "你可以问我关于进程分析、内存查看、\n断点设置等内核调试问题",
            fontSize = 12.sp,
            lineHeight = 18.sp
        )
    }
}

/**
 * 聊天消息气泡
 */
@Composable
fun ChatMessageBubble(message: ChatMessage) {
    val isUser = message.role == MessageRole.USER
    val isTool = message.role == MessageRole.TOOL

    val uiMode = LocalUiMode.current
    val bubbleColor = when {
        isUser -> when (uiMode) {
            UiMode.Miuix -> MiuixTheme.colorScheme.primary.copy(alpha = 0.15f)
            UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.primaryContainer
        }
        isTool -> when (uiMode) {
            UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
            UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.tertiaryContainer
        }
        else -> when (uiMode) {
            UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant
            UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant
        }
    }

    val alignment = when {
        isUser -> Alignment.End
        else -> Alignment.Start
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = alignment
    ) {
        // 工具调用指示器
        if (message.toolCalls != null && message.toolCalls.isNotEmpty()) {
            ToolCallIndicators(message.toolCalls)
            Spacer(modifier = Modifier.height(4.dp))
        }

        // 消息内容
        Surface(
            shape = RoundedCornerShape(
                topStart = 16.dp,
                topEnd = 16.dp,
                bottomStart = if (isUser) 16.dp else 4.dp,
                bottomEnd = if (isUser) 4.dp else 16.dp
            ),
            color = bubbleColor,
            modifier = Modifier.widthIn(max = 320.dp)
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                if (message.isStreaming && message.content.isEmpty()) {
                    // 流式加载中动画
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(text = "思考中...", fontSize = 13.sp)
                    }
                } else {
                    // 消息文本（支持 Markdown 格式展示）
                    Text(
                        text = message.content,
                        fontSize = 14.sp,
                        lineHeight = 20.sp,
                        fontFamily = if (isTool) FontFamily.Monospace else FontFamily.Default
                    )
                }

                // 流式指示器
                if (message.isStreaming && message.content.isNotEmpty()) {
                    StreamingCursor()
                }
            }
        }
    }
}

/**
 * 工具调用指示器
 */
@Composable
fun ToolCallIndicators(toolCalls: List<ToolCallInfo>) {
    Column(
        modifier = Modifier.widthIn(max = 320.dp)
    ) {
        toolCalls.forEach { toolCall ->
            ToolCallChip(toolCall)
            Spacer(modifier = Modifier.height(4.dp))
        }
    }
}

@Composable
fun ToolCallChip(toolCall: ToolCallInfo) {
    val uiMode = LocalUiMode.current
    val bgColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.secondaryContainer.copy(alpha = 0.3f)
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.3f)
    }
    val iconColor = when (toolCall.status) {
        ToolCallStatus.PENDING -> Color(0xFFFF9800)
        ToolCallStatus.RUNNING -> Color(0xFF2196F3)
        ToolCallStatus.COMPLETED -> Color(0xFF4CAF50)
        ToolCallStatus.FAILED -> Color(0xFFF44336)
    }

    Surface(
        shape = RoundedCornerShape(8.dp),
        color = bgColor,
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (toolCall.status == ToolCallStatus.RUNNING) {
                CircularProgressIndicator(
                    modifier = Modifier.size(14.dp),
                    strokeWidth = 2.dp,
                    color = iconColor
                )
            } else {
                Icon(
                    imageVector = Icons.Rounded.Circle,
                    contentDescription = null,
                    tint = iconColor,
                    modifier = Modifier.size(14.dp)
                )
            }
            Spacer(modifier = Modifier.width(8.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = toolCall.name,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium
                )
                if (toolCall.result != null) {
                    Text(
                        text = toolCall.result.take(100) + if (toolCall.result.length > 100) "..." else "",
                        fontSize = 11.sp,
                        maxLines = 2,
                        color = Color.Gray
                    )
                }
            }
            Text(
                text = when (toolCall.status) {
                    ToolCallStatus.PENDING -> "等待"
                    ToolCallStatus.RUNNING -> "执行中"
                    ToolCallStatus.COMPLETED -> "完成"
                    ToolCallStatus.FAILED -> "失败"
                },
                fontSize = 10.sp,
                color = iconColor
            )
        }
    }
}

/**
 * 流式光标动画
 */
@Composable
fun StreamingCursor() {
    var visible by remember { androidx.compose.runtime.mutableStateOf(true) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(500)
            visible = !visible
        }
    }
    AnimatedVisibility(visible = visible, enter = fadeIn(), exit = fadeOut()) {
        Box(
            modifier = Modifier
                .size(width = 2.dp, height = 16.dp)
                .background(Color.Gray)
        )
    }
}

/**
 * 聊天输入栏
 */
@Composable
fun ChatInputBar(
    text: String,
    onTextChange: (String) -> Unit,
    onSend: () -> Unit,
    onStop: () -> Unit,
    isStreaming: Boolean,
    isEnabled: Boolean
) {
    val uiMode = LocalUiMode.current
    val surfaceColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surface
    }
    val inputBgColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant
    }
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }
    val hintColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurfaceVariant
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurfaceVariant
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = surfaceColor,
        tonalElevation = 3.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.Bottom
        ) {
            // 输入框
            Surface(
                shape = RoundedCornerShape(24.dp),
                color = inputBgColor,
                modifier = Modifier.weight(1f)
            ) {
                if (text.isEmpty()) {
                    Box(
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                        contentAlignment = Alignment.CenterStart
                    ) {
                        Text(
                            text = "输入消息...",
                            color = hintColor,
                            fontSize = 14.sp
                        )
                    }
                }
                BasicTextField(
                    value = text,
                    onValueChange = onTextChange,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 10.dp),
                    textStyle = TextStyle(
                        color = textColor,
                        fontSize = 14.sp
                    ),
                    cursorBrush = SolidColor(textColor),
                    maxLines = 4,
                    enabled = isEnabled
                )
            }

            Spacer(modifier = Modifier.width(8.dp))

            // 发送/停止按钮
            if (isStreaming) {
                IconButton(onClick = onStop) {
                    Icon(
                        imageVector = Icons.Rounded.Stop,
                        contentDescription = "停止",
                        tint = Color(0xFFF44336)
                    )
                }
            } else {
                IconButton(
                    onClick = onSend,
                    enabled = text.isNotBlank() && isEnabled
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.Send,
                        contentDescription = "发送",
                        tint = if (text.isNotBlank()) Color(0xFF2196F3) else Color.Gray
                    )
                }
            }
        }
    }
}
