package com.kernelai.app.data.model

import kotlinx.serialization.Serializable
import java.util.UUID

/**
 * UI 层聊天消息模型
 */
data class ChatMessage(
    val id: String = UUID.randomUUID().toString(),
    val role: MessageRole,
    val content: String,
    val timestamp: Long = System.currentTimeMillis(),
    val toolCalls: List<ToolCallInfo>? = null,
    val isStreaming: Boolean = false
)

/**
 * 消息角色枚举
 */
enum class MessageRole {
    USER,
    ASSISTANT,
    SYSTEM,
    TOOL
}

/**
 * 工具调用信息（UI 展示用）
 */
data class ToolCallInfo(
    val id: String,
    val name: String,
    val arguments: String,
    val result: String? = null,
    val status: ToolCallStatus = ToolCallStatus.PENDING
)

/**
 * 工具调用状态
 */
enum class ToolCallStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED
}

/**
 * 网络层序列化模型 - 发送到 API 的消息格式
 */
@Serializable
data class ApiChatMessage(
    val role: String,       // "user", "assistant", "system", "tool"
    val content: String?,
    val toolCalls: List<ApiToolCall>? = null,
    val toolCallId: String? = null,
    val name: String? = null
)

@Serializable
data class ApiToolCall(
    val id: String,
    val type: String = "function",
    val function: ApiFunctionCall
)

@Serializable
data class ApiFunctionCall(
    val name: String,
    val arguments: String  // JSON string
)

@Serializable
data class ChatResponse(
    val id: String,
    val choices: List<Choice>,
    val usage: Usage? = null
)

@Serializable
data class Choice(
    val index: Int,
    val message: ApiChatMessage,
    val finishReason: String? = null
)

@Serializable
data class Usage(
    val promptTokens: Int = 0,
    val completionTokens: Int = 0,
    val totalTokens: Int = 0
)

/**
 * 将 UI 模型转换为 API 模型
 */
fun ChatMessage.toApiMessage(): ApiChatMessage {
    return ApiChatMessage(
        role = when (role) {
            MessageRole.USER -> "user"
            MessageRole.ASSISTANT -> "assistant"
            MessageRole.SYSTEM -> "system"
            MessageRole.TOOL -> "tool"
        },
        content = content,
        toolCalls = toolCalls?.map { tc ->
            ApiToolCall(
                id = tc.id,
                function = ApiFunctionCall(
                    name = tc.name,
                    arguments = tc.arguments
                )
            )
        },
        toolCallId = if (role == MessageRole.TOOL) id else null,
        name = if (role == MessageRole.TOOL) toolCalls?.firstOrNull()?.name else null
    )
}
