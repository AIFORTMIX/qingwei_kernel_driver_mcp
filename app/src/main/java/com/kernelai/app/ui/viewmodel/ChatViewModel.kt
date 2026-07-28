package com.kernelai.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kernelai.app.data.model.ChatMessage
import com.kernelai.app.data.model.MessageRole
import com.kernelai.app.data.model.ToolCallInfo
import com.kernelai.app.data.model.ToolCallStatus
import com.kernelai.app.data.model.ConnectionStatus
import com.kernelai.app.data.model.AiConfig
import com.kernelai.app.data.model.toApiMessage
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.JsonObject
import java.util.UUID

/**
 * 聊天 ViewModel - 管理聊天消息列表、发送/接收消息、工具调用处理
 */
class ChatViewModel : ViewModel() {

    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages.asStateFlow()

    private val _isStreaming = MutableStateFlow(false)
    val isStreaming: StateFlow<Boolean> = _isStreaming.asStateFlow()

    private val _connectionStatus = MutableStateFlow(ConnectionStatus())
    val connectionStatus: StateFlow<ConnectionStatus> = _connectionStatus.asStateFlow()

    private val _inputText = MutableStateFlow("")
    val inputText: StateFlow<String> = _inputText.asStateFlow()

    private var aiConfig = AiConfig()
    private val json = Json { ignoreUnknownKeys = true }

    /**
     * 更新 AI 配置
     */
    fun updateAiConfig(config: AiConfig) {
        aiConfig = config
    }

    /**
     * 更新输入文本
     */
    fun updateInputText(text: String) {
        _inputText.value = text
    }

    /**
     * 发送用户消息
     */
    fun sendMessage() {
        val text = _inputText.value.trim()
        if (text.isEmpty() || _isStreaming.value) return

        val userMessage = ChatMessage(
            role = MessageRole.USER,
            content = text
        )

        _messages.value = _messages.value + userMessage
        _inputText.value = ""

        // 模拟 AI 响应（实际项目中应连接真实的 AI API）
        viewModelScope.launch {
            _isStreaming.value = true

            // 创建流式响应的 assistant 消息占位
            val assistantId = UUID.randomUUID().toString()
            val assistantMessage = ChatMessage(
                id = assistantId,
                role = MessageRole.ASSISTANT,
                content = "",
                isStreaming = true
            )
            _messages.value = _messages.value + assistantMessage

            try {
                // 模拟 AI 处理过程
                val response = processWithAi(text)

                // 检查是否有工具调用
                if (response.hasToolCalls) {
                    // 更新 assistant 消息，添加工具调用
                    updateMessage(assistantId) { msg ->
                        msg.copy(
                            content = response.textBeforeToolCalls,
                            toolCalls = response.toolCalls,
                            isStreaming = false
                        )
                    }

                    // 执行工具调用
                    response.toolCalls.forEach { toolCall ->
                        val toolResult = executeToolCall(toolCall)

                        // 添加工具结果消息
                        val toolMessage = ChatMessage(
                            role = MessageRole.TOOL,
                            content = toolResult,
                            toolCalls = listOf(toolCall.copy(
                                result = toolResult,
                                status = ToolCallStatus.COMPLETED
                            ))
                        )
                        _messages.value = _messages.value + toolMessage
                    }

                    // 继续生成最终回复
                    val finalMessage = ChatMessage(
                        role = MessageRole.ASSISTANT,
                        content = response.textAfterToolCalls,
                        isStreaming = false
                    )
                    _messages.value = _messages.value + finalMessage
                } else {
                    // 直接更新为最终内容
                    updateMessage(assistantId) { msg ->
                        msg.copy(
                            content = response.textBeforeToolCalls,
                            isStreaming = false
                        )
                    }
                }
            } catch (e: Exception) {
                updateMessage(assistantId) { msg ->
                    msg.copy(
                        content = "Error: ${e.message}",
                        isStreaming = false
                    )
                }
            } finally {
                _isStreaming.value = false
            }
        }
    }

    /**
     * 清空聊天历史
     */
    fun clearChat() {
        _messages.value = emptyList()
    }

    /**
     * 更新连接状态
     */
    fun updateConnectionStatus(status: ConnectionStatus) {
        _connectionStatus.value = status
    }

    /**
     * 停止流式响应
     */
    fun stopStreaming() {
        _isStreaming.value = false
        // 更新当前流式消息为完成状态
        val lastMsg = _messages.value.lastOrNull()
        if (lastMsg != null && lastMsg.isStreaming) {
            updateMessage(lastMsg.id) { it.copy(isStreaming = false) }
        }
    }

    private fun updateMessage(id: String, transform: (ChatMessage) -> ChatMessage) {
        _messages.value = _messages.value.map { msg ->
            if (msg.id == id) transform(msg) else msg
        }
    }

    /**
     * 模拟 AI 处理（实际项目中替换为真实的 API 调用）
     */
    private suspend fun processWithAi(userInput: String): AiResponse {
        // 模拟网络延迟
        delay(500)

        // 简单的命令解析 - 模拟工具调用场景
        return when {
            userInput.contains("进程列表", ignoreCase = true) ||
            userInput.contains("process list", ignoreCase = true) -> {
                AiResponse(
                    textBeforeToolCalls = "正在查询进程列表...",
                    hasToolCalls = true,
                    toolCalls = listOf(
                        ToolCallInfo(
                            id = UUID.randomUUID().toString(),
                            name = "process_list",
                            arguments = buildJsonObject { }.toString(),
                            status = ToolCallStatus.RUNNING
                        )
                    ),
                    textAfterToolCalls = "以上是当前系统中运行的进程列表。你可以通过点击某个进程来查看详细信息，或者告诉我你想进一步分析哪个进程。"
                )
            }
            userInput.contains("断点", ignoreCase = true) ||
            userInput.contains("breakpoint", ignoreCase = true) -> {
                AiResponse(
                    textBeforeToolCalls = "正在查询断点信息...",
                    hasToolCalls = true,
                    toolCalls = listOf(
                        ToolCallInfo(
                            id = UUID.randomUUID().toString(),
                            name = "breakpoint_list",
                            arguments = buildJsonObject { }.toString(),
                            status = ToolCallStatus.RUNNING
                        )
                    ),
                    textAfterToolCalls = "以上是当前已设置的断点列表。你可以添加新的断点或移除现有的断点。"
                )
            }
            else -> {
                AiResponse(
                    textBeforeToolCalls = "我是轻微 MCP 助手，可以帮助你进行内核调试和进程分析。你可以问我关于进程、内存、断点等方面的问题，或者让我执行特定的调试操作。\n\n目前支持的功能包括：\n- 查看进程列表和详细信息\n- 读取/写入进程内存\n- 设置和管理断点\n- 反汇编分析\n- 调用栈捕获",
                    hasToolCalls = false,
                    toolCalls = emptyList(),
                    textAfterToolCalls = ""
                )
            }
        }
    }

    /**
     * 模拟执行工具调用
     */
    private suspend fun executeToolCall(toolCall: ToolCallInfo): String {
        delay(300) // 模拟执行时间
        return when (toolCall.name) {
            "process_list" -> """
                |PID   | 包名                        | 状态     | 线程数
                |------|---------------------------|---------|------
                |1     | init                      | Running | 1
                |512   | system_server             | Sleep   | 128
                |1024  | com.android.phone         | Sleep   | 45
                |2048  | com.kernelai.app          | Running | 32
            """.trimMargin()
            "breakpoint_list" -> "当前没有活动的断点。"
            else -> "工具 ${toolCall.name} 执行完成"
        }
    }

    private data class AiResponse(
        val textBeforeToolCalls: String,
        val hasToolCalls: Boolean,
        val toolCalls: List<ToolCallInfo>,
        val textAfterToolCalls: String
    )
}
