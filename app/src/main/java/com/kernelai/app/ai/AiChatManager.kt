package com.kernelai.app.ai

import com.kernelai.app.data.model.ChatMessage
import com.kernelai.app.data.model.ToolCall
import com.kernelai.app.mcp.McpToolExecutor
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonObject
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * AI 聊天会话管理器。
 *
 * 职责：
 * 1. 管理多个独立的聊天会话（[ChatSession]），每个会话维护自己的消息历史。
 * 2. 协调 [OpenAiClient] 与 [McpToolExecutor] 之间的交互。
 * 3. 实现完整的 tool-call 循环：
 *    用户消息 -> AI 响应 -> 工具调用 -> 工具结果 -> AI 继续响应 -> ... -> 最终文本
 * 4. 通过 [Flow] 向 UI 层发射实时事件。
 *
 * 线程安全：会话列表通过 [ConcurrentHashMap] 管理，每个会话内部的操作
 * 通过协程调度器串行化。
 */
class AiChatManager(
    private val client: OpenAiClient,
    private val toolExecutor: McpToolExecutor,
    private val config: AiConfig = AiConfig()
) {
    private val json = Json { ignoreUnknownKeys = true }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    /** 所有活跃会话。key = sessionId */
    private val sessions = ConcurrentHashMap<String, ChatSession>()

    /** 当前活跃会话 ID。 */
    private var activeSessionId: String? = null

    // =========================================================================
    // 会话管理
    // =========================================================================

    /**
     * 创建新的聊天会话并设为活跃会话。
     *
     * @return 新会话的 ID
     */
    fun createSession(): String {
        val sessionId = UUID.randomUUID().toString()
        val session = ChatSession(
            id = sessionId,
            messages = mutableListOf(),
            createdAt = System.currentTimeMillis()
        )
        // 注入系统提示词
        if (config.systemPrompt.isNotBlank()) {
            session.messages.add(ChatMessage(role = "system", content = config.systemPrompt))
        }
        sessions[sessionId] = session
        activeSessionId = sessionId
        _sessionsFlow.value = sessions.values.map { it.toSessionInfo() }
        return sessionId
    }

    /** 切换到指定会话。 */
    fun switchSession(sessionId: String): Boolean {
        return if (sessions.containsKey(sessionId)) {
            activeSessionId = sessionId
            true
        } else {
            false
        }
    }

    /** 删除会话。 */
    fun deleteSession(sessionId: String) {
        sessions.remove(sessionId)
        if (activeSessionId == sessionId) {
            activeSessionId = sessions.keys.firstOrNull()
        }
        _sessionsFlow.value = sessions.values.map { it.toSessionInfo() }
    }

    /** 获取当前活跃会话 ID。如果没有则自动创建。 */
    fun getActiveSessionId(): String {
        return activeSessionId ?: createSession()
    }

    /** 获取指定会话的消息历史（只读副本）。 */
    fun getMessages(sessionId: String? = null): List<ChatMessage> {
        val id = sessionId ?: activeSessionId ?: return emptyList()
        return sessions[id]?.messages?.toList() ?: emptyList()
    }

    /** 所有会话的响应式列表。 */
    private val _sessionsFlow = MutableStateFlow<List<SessionInfo>>(emptyList())
    val sessionsFlow: StateFlow<List<SessionInfo>> = _sessionsFlow.asStateFlow()

    // =========================================================================
    // 聊天事件（UI 观察）
    // =========================================================================

    private val _chatEvents = MutableSharedFlow<ChatEvent>(
        replay = 0,
        extraBufferCapacity = 64
    )
    val chatEvents: SharedFlow<ChatEvent> = _chatEvents.asSharedFlow()

    /** 当前是否正在生成回复。 */
    private val _isGenerating = MutableStateFlow(false)
    val isGenerating: StateFlow<Boolean> = _isGenerating.asStateFlow()

    /** 当前活跃的 Job，用于取消。 */
    private var currentJob: Job? = null

    // =========================================================================
    // 发送消息 & 工具调用循环
    // =========================================================================

    /**
     * 发送用户消息并启动 AI 响应流程。
     *
     * 完整的处理流程：
     * 1. 将用户消息加入历史
     * 2. 调用 AI（流式）
     * 3. 如果 AI 返回 tool_calls：
     *    a. 逐个执行工具调用
     *    b. 将工具结果加入历史
     *    c. 带着更新后的历史再次调用 AI
     *    d. 重复直到 AI 返回纯文本响应或达到最大迭代次数
     * 4. 通过 [chatEvents] 实时发射流式事件
     */
    fun sendMessage(content: String, sessionId: String? = null) {
        val id = sessionId ?: getActiveSessionId()
        val session = sessions[id] ?: return

        // 取消上一次生成
        currentJob?.cancel()

        // 添加用户消息
        val userMessage = ChatMessage(role = "user", content = content)
        session.messages.add(userMessage)
        _chatEvents.tryEmit(ChatEvent.MessageAdded(userMessage))

        currentJob = scope.launch {
            _isGenerating.value = true
            try {
                runToolCallLoop(session)
            } catch (e: CancellationException) {
                _chatEvents.emit(ChatEvent.GenerationCancelled)
                throw e
            } catch (e: Exception) {
                _chatEvents.emit(ChatEvent.Error("生成失败: ${e.message}"))
            } finally {
                _isGenerating.value = false
            }
        }
    }

    /** 取消当前正在进行的生成。 */
    fun cancelGeneration() {
        currentJob?.cancel()
        currentJob = null
        _isGenerating.value = false
    }

    /**
     * 工具调用循环核心逻辑。
     *
     * 每次 AI 返回 tool_calls 时，执行工具并将结果反馈给 AI，
     * 直到 AI 返回纯文本响应或达到最大迭代次数。
     */
    private suspend fun runToolCallLoop(session: ChatSession) {
        val tools = toolExecutor.getToolDefinitions()
        var iteration = 0

        while (iteration < MAX_TOOL_CALL_ITERATIONS) {
            iteration++

            // 通知 UI 开始新一轮 AI 请求
            _chatEvents.emit(ChatEvent.RequestStarted(iteration))

            val accumulator = OpenAiClient.StreamAccumulator()
            var hasError = false

            // 流式调用 AI
            client.streamChatCompletion(session.messages, tools)
                .collect { event ->
                    // 累积事件
                    accumulator.process(event)

                    // 转发给 UI
                    when (event) {
                        is StreamEvent.ContentDelta -> {
                            _chatEvents.emit(ChatEvent.StreamingText(event.delta))
                        }
                        is StreamEvent.ToolCallDelta -> {
                            _chatEvents.emit(ChatEvent.ToolCallStreaming(
                                index = event.index,
                                name = event.functionName,
                                argumentsDelta = event.argumentsDelta
                            ))
                        }
                        is StreamEvent.Error -> {
                            _chatEvents.emit(ChatEvent.Error(event.message))
                            hasError = true
                        }
                        is StreamEvent.Done -> {
                            // 流结束
                        }
                        is StreamEvent.FinishReason -> {
                            // finish reason 信息
                        }
                    }
                }

            if (hasError) {
                break
            }

            // 构建完整的 assistant 消息
            val assistantMessage = accumulator.buildMessage()

            if (assistantMessage.toolCalls != null && assistantMessage.toolCalls!!.isNotEmpty()) {
                // AI 请求调用工具
                // 将 assistant 消息（含 tool_calls）加入历史
                session.messages.add(assistantMessage)
                _chatEvents.emit(ChatEvent.MessageAdded(assistantMessage))

                // 通知 UI 开始执行工具
                _chatEvents.emit(ChatEvent.ToolCallsStarting(assistantMessage.toolCalls!!))

                // 逐个执行工具调用
                for (toolCall in assistantMessage.toolCalls!!) {
                    executeToolCall(session, toolCall)
                }

                // 继续循环，将工具结果反馈给 AI
                _chatEvents.emit(ChatEvent.ContinuingWithToolResults)
            } else {
                // AI 返回了纯文本响应，循环结束
                session.messages.add(assistantMessage)
                _chatEvents.emit(ChatEvent.MessageAdded(assistantMessage))
                _chatEvents.emit(ChatEvent.GenerationComplete)
                break
            }
        }

        if (iteration >= MAX_TOOL_CALL_ITERATIONS) {
            _chatEvents.emit(ChatEvent.Error(
                "达到最大工具调用迭代次数 ($MAX_TOOL_CALL_ITERATIONS)，停止继续调用。"
            ))
        }
    }

    /**
     * 执行单个工具调用并将结果加入消息历史。
     */
    private suspend fun executeToolCall(session: ChatSession, toolCall: ToolCall) {
        val toolName = toolCall.function.name
        val argumentsStr = toolCall.function.arguments

        _chatEvents.emit(ChatEvent.ToolCallExecuting(toolName, argumentsStr))

        val toolResult: String = try {
            // 解析参数 JSON
            val arguments: JsonObject = try {
                json.parseToJsonElement(argumentsStr).jsonObject
            } catch (e: Exception) {
                JsonObject(emptyMap())
            }

            // 通过 McpToolExecutor 执行
            withContext(Dispatchers.IO) {
                toolExecutor.executeTool(toolName, arguments)
            }
        } catch (e: Exception) {
            // 工具执行失败，将错误信息作为结果返回给 AI
            "Error executing tool '$toolName': ${e.message}"
        }

        // 构建 tool 响应消息
        val toolMessage = ChatMessage(
            role = "tool",
            content = toolResult,
            toolCallId = toolCall.id,
            name = toolName
        )
        session.messages.add(toolMessage)
        _chatEvents.emit(ChatEvent.ToolCallResult(toolCall.id, toolName, toolResult))
    }

    // =========================================================================
    // 重新发送 / 编辑
    // =========================================================================

    /**
     * 重新生成最后一条 assistant 回复。
     *
     * 移除最后一条 assistant 消息（及其后续的 tool 消息），
     * 然后重新向 AI 发送请求。
     */
    fun regenerateLastResponse(sessionId: String? = null) {
        val id = sessionId ?: activeSessionId ?: return
        val session = sessions[id] ?: return

        currentJob?.cancel()

        // 从末尾向前移除 tool 和 assistant 消息，直到遇到 user 消息
        while (session.messages.isNotEmpty()) {
            val last = session.messages.last()
            if (last.role == "assistant" || last.role == "tool") {
                session.messages.removeAt(session.messages.size - 1)
            } else {
                break
            }
        }

        if (session.messages.isEmpty()) return

        currentJob = scope.launch {
            _isGenerating.value = true
            try {
                runToolCallLoop(session)
            } catch (e: CancellationException) {
                _chatEvents.emit(ChatEvent.GenerationCancelled)
                throw e
            } catch (e: Exception) {
                _chatEvents.emit(ChatEvent.Error("重新生成失败: ${e.message}"))
            } finally {
                _isGenerating.value = false
            }
        }
    }

    /**
     * 清空当前会话的消息历史（保留系统提示词）。
     */
    fun clearSession(sessionId: String? = null) {
        val id = sessionId ?: activeSessionId ?: return
        val session = sessions[id] ?: return

        currentJob?.cancel()

        // 保留 system 消息
        val systemMessages = session.messages.filter { it.role == "system" }
        session.messages.clear()
        session.messages.addAll(systemMessages)

        _chatEvents.tryEmit(ChatEvent.SessionCleared)
    }

    /** 释放所有资源。 */
    fun destroy() {
        currentJob?.cancel()
        scope.cancel()
        sessions.clear()
        client.close()
    }

    // =========================================================================
    // 数据类
    // =========================================================================

    /**
     * 单个聊天会话。
     */
    data class ChatSession(
        val id: String,
        val messages: MutableList<ChatMessage>,
        val createdAt: Long,
        var title: String = "新会话"
    ) {
        fun toSessionInfo(): SessionInfo = SessionInfo(
            id = id,
            title = title,
            messageCount = messages.size,
            createdAt = createdAt
        )
    }

    /**
     * 会话摘要信息，用于 UI 列表展示。
     */
    data class SessionInfo(
        val id: String,
        val title: String,
        val messageCount: Int,
        val createdAt: Long
    )

    companion object {
        /** 工具调用循环最大迭代次数，防止无限循环。 */
        const val MAX_TOOL_CALL_ITERATIONS = 10
    }
}

// =============================================================================
// 聊天事件密封类 — UI 层观察
// =============================================================================

/**
 * AI 聊天过程中向 UI 发射的事件。
 *
 * UI 层通过 [AiChatManager.chatEvents] 收集这些事件来更新界面：
 * - 流式文本显示逐字输出效果
 * - 工具调用状态显示执行进度
 * - 错误信息弹出提示
 */
sealed class ChatEvent {

    /** 新消息已加入历史（用户消息或完成的 assistant 消息）。 */
    data class MessageAdded(val message: ChatMessage) : ChatEvent()

    /** 一轮 AI 请求开始。[iteration] 表示当前是第几轮（从 1 开始）。 */
    data class RequestStarted(val iteration: Int) : ChatEvent()

    /** 流式文本增量。UI 应将 [delta] 追加到当前 assistant 消息末尾。 */
    data class StreamingText(val delta: String) : ChatEvent()

    /** 工具调用参数正在流式传输（AI 正在生成工具调用参数）。 */
    data class ToolCallStreaming(
        val index: Int,
        val name: String?,
        val argumentsDelta: String?
    ) : ChatEvent()

    /** 即将开始执行工具调用。 */
    data class ToolCallsStarting(val toolCalls: List<ToolCall>) : ChatEvent()

    /** 正在执行某个工具调用。 */
    data class ToolCallExecuting(val toolName: String, val arguments: String) : ChatEvent()

    /** 工具调用完成，返回结果。 */
    data class ToolCallResult(
        val toolCallId: String,
        val toolName: String,
        val result: String
    ) : ChatEvent()

    /** 工具结果已收集完毕，将继续请求 AI 生成最终回复。 */
    data object ContinuingWithToolResults : ChatEvent()

    /** 生成完成（AI 返回了最终文本响应）。 */
    data object GenerationComplete : ChatEvent()

    /** 生成被取消。 */
    data object GenerationCancelled : ChatEvent()

    /** 会话已清空。 */
    data object SessionCleared : ChatEvent()

    /** 发生错误。 */
    data class Error(val message: String) : ChatEvent()
}
