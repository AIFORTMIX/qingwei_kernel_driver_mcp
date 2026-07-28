package com.kernelai.app.ai

import com.kernelai.app.data.model.ChatMessage
import com.kernelai.app.data.model.ToolCall
import com.kernelai.app.data.model.FunctionCall
import com.kernelai.app.mcp.McpToolDefinitions
import com.kernelai.app.mcp.McpToolExecutor
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * OpenAI 兼容 API 客户端。
 *
 * 支持标准 Chat Completions 端点，包括：
 * - 流式 SSE 响应（`stream: true`）
 * - Function calling / Tool use
 * - 可配置的 base URL、API key、model
 *
 * 适用于 OpenAI、Azure OpenAI、Ollama、vLLM、LM Studio、DeepSeek 等
 * 任何兼容 OpenAI 格式的服务端。
 */
class OpenAiClient(
    private var config: AiConfig
) {
    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = false
        isLenient = true
    }

    private val httpClient: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)  // SSE 流需要较长读取超时
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    /** 更新配置（例如用户切换模型或 API key）。 */
    fun updateConfig(newConfig: AiConfig) {
        config = newConfig
    }

    // -------------------------------------------------------------------------
    // 公共 API
    // -------------------------------------------------------------------------

    /**
     * 流式聊天补全。
     *
     * 返回 [Flow]，按顺序发射 [StreamEvent]：
     * - [StreamEvent.ContentDelta] — 增量文本片段
     * - [StreamEvent.ToolCallDelta] — 增量工具调用片段
     * - [StreamEvent.Done] — 流结束
     * - [StreamEvent.Error] — 发生错误
     *
     * @param messages 完整的消息历史（含 system / user / assistant / tool）
     * @param tools    可用工具列表（OpenAI function 格式），为空则不启用 function calling
     */
    fun streamChatCompletion(
        messages: List<ChatMessage>,
        tools: List<McpToolExecutor.ToolDefinition> = emptyList()
    ): Flow<StreamEvent> = callbackFlow {
        val requestBody = buildRequestBody(messages, tools, stream = true)
        val request = buildHttpRequest(requestBody)

        val cancelled = AtomicBoolean(false)
        val call = httpClient.newCall(request)

        // 当 Flow collector 取消时，同步取消 HTTP 请求
        invokeOnClose {
            cancelled.set(true)
            call.cancel()
        }

        call.enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                if (!cancelled.get()) {
                    trySend(StreamEvent.Error("网络请求失败: ${e.message}", e))
                    trySend(StreamEvent.Done(null))
                    close()
                }
            }

            override fun onResponse(call: Call, response: Response) {
                response.use { resp ->
                    if (!resp.isSuccessful) {
                        val errorBody = resp.body?.string() ?: "Unknown error"
                        trySend(StreamEvent.Error(
                            "HTTP ${resp.code}: $errorBody", null
                        ))
                        trySend(StreamEvent.Done(null))
                        close()
                        return
                    }

                    val body = resp.body ?: run {
                        trySend(StreamEvent.Error("响应体为空", null))
                        trySend(StreamEvent.Done(null))
                        close()
                        return
                    }

                    try {
                        val reader = BufferedReader(InputStreamReader(body.byteStream()))
                        parseSseStream(reader) { event ->
                            if (!cancelled.get()) {
                                trySend(event)
                            }
                        }
                        if (!cancelled.get()) {
                            trySend(StreamEvent.Done("stop"))
                            close()
                        }
                    } catch (e: Exception) {
                        if (!cancelled.get()) {
                            trySend(StreamEvent.Error("SSE 解析失败: ${e.message}", e))
                            trySend(StreamEvent.Done(null))
                            close()
                        }
                    }
                }
            }
        })

        awaitClose { /* invokeOnClose 已处理取消 */ }
    }

    /**
     * 非流式聊天补全。直接返回完整的 [ChatMessage]。
     */
    suspend fun chatCompletion(
        messages: List<ChatMessage>,
        tools: List<McpToolExecutor.ToolDefinition> = emptyList()
    ): Result<ChatMessage> = withContext(Dispatchers.IO) {
        val requestBody = buildRequestBody(messages, tools, stream = false)
        val request = buildHttpRequest(requestBody)

        try {
            val response = httpClient.newCall(request).execute()
            response.use { resp ->
                val body = resp.body?.string()
                    ?: return@withContext Result.failure(IOException("响应体为空"))

                if (!resp.isSuccessful) {
                    return@withContext Result.failure(
                        IOException("HTTP ${resp.code}: $body")
                    )
                }

                val message = parseNonStreamResponse(body)
                Result.success(message)
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    /**
     * 将 MCP 工具定义转换为 OpenAI function calling 格式。
     *
     * 返回可直接放入请求体 `tools` 字段的 [JsonArray]。
     */
    fun convertMcpTools(tools: List<McpToolExecutor.ToolDefinition>): JsonArray {
        return buildJsonArray {
            tools.forEach { tool ->
                addJsonObject {
                    put("type", "function")
                    putJsonObject("function") {
                        put("name", tool.name)
                        put("description", tool.description)
                        put("parameters", tool.inputSchema)
                    }
                }
            }
        }
    }

    /**
     * 将 [McpToolDefinitions.ToolDef] 列表转换为 OpenAI function calling 格式。
     * 适用于直接使用 [McpToolDefinitions.ALL_TOOLS] 的场景。
     */
    fun convertMcpToolDefs(tools: List<McpToolDefinitions.ToolDef>): JsonArray {
        return buildJsonArray {
            tools.forEach { tool ->
                addJsonObject {
                    put("type", "function")
                    putJsonObject("function") {
                        put("name", tool.name)
                        put("description", tool.description)
                        put("parameters", McpToolDefinitions.toJsonSchema(tool))
                    }
                }
            }
        }
    }

    /** 释放 HTTP 连接池资源。 */
    fun close() {
        httpClient.dispatcher.executorService.shutdown()
        httpClient.connectionPool.evictAll()
    }

    // -------------------------------------------------------------------------
    // 请求构建
    // -------------------------------------------------------------------------

    private fun buildRequestBody(
        messages: List<ChatMessage>,
        tools: List<McpToolExecutor.ToolDefinition>,
        stream: Boolean
    ): String {
        return buildJsonObject {
            put("model", config.model)
            put("stream", stream)

            if (config.maxTokens > 0) {
                put("max_tokens", config.maxTokens)
            }
            put("temperature", config.temperature.toDouble())

            // 消息列表
            putJsonArray("messages") {
                messages.forEach { msg ->
                    addJsonObject {
                        put("role", msg.role)
                        when (msg.role) {
                            "assistant" -> {
                                // content 可能为 null（纯 tool_calls 响应）
                                if (msg.content != null) {
                                    put("content", msg.content)
                                } else {
                                    put("content", JsonNull)
                                }
                                // 如果有 tool_calls，附加上
                                msg.toolCalls?.let { calls ->
                                    putJsonArray("tool_calls") {
                                        calls.forEach { tc ->
                                            addJsonObject {
                                                put("id", tc.id)
                                                put("type", tc.type)
                                                putJsonObject("function") {
                                                    put("name", tc.function.name)
                                                    put("arguments", tc.function.arguments)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            "tool" -> {
                                // tool 响应消息
                                put("content", msg.content ?: "")
                                msg.toolCallId?.let { put("tool_call_id", it) }
                            }
                            else -> {
                                // system / user
                                put("content", msg.content ?: "")
                            }
                        }
                    }
                }
            }

            // 工具定义
            if (tools.isNotEmpty()) {
                put("tools", convertMcpTools(tools))
                put("tool_choice", "auto")
            }
        }.toString()
    }

    private fun buildHttpRequest(bodyJson: String): Request {
        return Request.Builder()
            .url(config.chatCompletionsUrl)
            .addHeader("Authorization", "Bearer ${config.apiKey}")
            .addHeader("Accept", "text/event-stream")
            .addHeader("Content-Type", "application/json")
            .post(bodyJson.toRequestBody("application/json".toMediaType()))
            .build()
    }

    // -------------------------------------------------------------------------
    // SSE 解析
    // -------------------------------------------------------------------------

    /**
     * 解析 SSE 事件流。
     *
     * SSE 格式：
     * ```
     * data: {"id":"...","choices":[{"delta":{...}}]}
     * data: {"id":"...","choices":[{"delta":{...}}]}
     * data: [DONE]
     * ```
     */
    private fun parseSseStream(reader: BufferedReader, onEvent: (StreamEvent) -> Unit) {
        var line: String?
        while (reader.readLine().also { line = it } != null) {
            val currentLine = line ?: break

            // 跳过空行和注释行
            if (currentLine.isBlank() || currentLine.startsWith(":")) {
                continue
            }

            // 只处理 data: 行
            if (!currentLine.startsWith("data:")) {
                continue
            }

            val data = currentLine.removePrefix("data:").trim()

            // 流结束标记
            if (data == "[DONE]") {
                break
            }

            try {
                val chunk = json.parseToJsonElement(data).jsonObject
                val choices = chunk["choices"]?.jsonArray ?: continue
                if (choices.isEmpty()) continue

                val choice = choices[0].jsonObject
                val delta = choice["delta"]?.jsonObject ?: continue
                val finishReason = choice["finish_reason"]?.takeIf { it !is JsonNull }
                    ?.jsonPrimitive?.content

                // 解析文本增量
                delta["content"]?.let { contentElement ->
                    if (contentElement !is JsonNull) {
                        val text = contentElement.jsonPrimitive.content
                        if (text.isNotEmpty()) {
                            onEvent(StreamEvent.ContentDelta(text))
                        }
                    }
                }

                // 解析工具调用增量
                delta["tool_calls"]?.jsonArray?.forEach { tcDelta ->
                    val tcObj = tcDelta.jsonObject
                    val index = tcObj["index"]?.jsonPrimitive?.int ?: 0
                    val id = tcObj["id"]?.jsonPrimitive?.contentOrNull
                    val type = tcObj["type"]?.jsonPrimitive?.contentOrNull
                    val functionDelta = tcObj["function"]?.jsonObject

                    val funcName = functionDelta?.get("name")?.jsonPrimitive?.contentOrNull
                    val argsDelta = functionDelta?.get("arguments")?.jsonPrimitive?.contentOrNull

                    onEvent(StreamEvent.ToolCallDelta(
                        index = index,
                        id = id,
                        type = type,
                        functionName = funcName,
                        argumentsDelta = argsDelta
                    ))
                }

                // finish_reason 信号
                if (finishReason != null) {
                    onEvent(StreamEvent.FinishReason(finishReason))
                }
            } catch (e: Exception) {
                // 单个 chunk 解析失败不应中断整个流
                onEvent(StreamEvent.Error("解析 chunk 失败: ${e.message}", e))
            }
        }
    }

    // -------------------------------------------------------------------------
    // 非流式响应解析
    // -------------------------------------------------------------------------

    private fun parseNonStreamResponse(body: String): ChatMessage {
        val responseObj = json.parseToJsonElement(body).jsonObject
        val choices = responseObj["choices"]?.jsonArray
            ?: throw IOException("响应中缺少 choices 字段")

        if (choices.isEmpty()) {
            throw IOException("choices 数组为空")
        }

        val message = choices[0].jsonObject["message"]?.jsonObject
            ?: throw IOException("响应中缺少 message 字段")

        return parseAssistantMessage(message)
    }

    private fun parseAssistantMessage(msgObj: JsonObject): ChatMessage {
        val role = msgObj["role"]?.jsonPrimitive?.content ?: "assistant"
        val content = msgObj["content"]?.takeIf { it !is JsonNull }?.jsonPrimitive?.content

        val toolCalls = msgObj["tool_calls"]?.jsonArray?.map { tcElem ->
            val tc = tcElem.jsonObject
            ToolCall(
                id = tc["id"]?.jsonPrimitive?.content ?: "",
                type = tc["type"]?.jsonPrimitive?.content ?: "function",
                function = FunctionCall(
                    name = tc["function"]?.jsonObject?.get("name")?.jsonPrimitive?.content ?: "",
                    arguments = tc["function"]?.jsonObject?.get("arguments")?.jsonPrimitive?.content ?: "{}"
                )
            )
        }

        return ChatMessage(
            role = role,
            content = content,
            toolCalls = toolCalls?.takeIf { it.isNotEmpty() }
        )
    }

    // -------------------------------------------------------------------------
    // 流式响应累积器
    // -------------------------------------------------------------------------

    /**
     * 将流式事件累积为完整的 [ChatMessage]。
     *
     * 在 [AiChatManager] 中使用：一边通过 Flow 向 UI 发射增量内容，
     * 一边累积完整响应以便后续处理 tool_calls。
     */
    class StreamAccumulator {
        private val contentBuilder = StringBuilder()
        private val toolCallAccumulators = mutableMapOf<Int, ToolCallAccumulator>()
        private var finishReason: String? = null

        /** 处理一个流事件，返回同一事件供上游继续发射。 */
        fun process(event: StreamEvent): StreamEvent {
            when (event) {
                is StreamEvent.ContentDelta -> contentBuilder.append(event.delta)
                is StreamEvent.ToolCallDelta -> {
                    val acc = toolCallAccumulators.getOrPut(event.index) {
                        ToolCallAccumulator(event.index)
                    }
                    event.id?.let { acc.id = it }
                    event.type?.let { acc.type = it }
                    event.functionName?.let { acc.name = it }
                    event.argumentsDelta?.let { acc.argumentsBuilder.append(it) }
                }
                is StreamEvent.FinishReason -> finishReason = event.reason
                is StreamEvent.Done -> { /* 标记结束 */ }
                is StreamEvent.Error -> { /* 错误已上报 */ }
            }
            return event
        }

        /** 构建累积完成的 [ChatMessage]。 */
        fun buildMessage(): ChatMessage {
            val content = contentBuilder.toString().ifEmpty { null }
            val toolCalls = toolCallAccumulators.values
                .sortedBy { it.index }
                .map { acc ->
                    ToolCall(
                        id = acc.id ?: "call_${acc.index}",
                        type = acc.type ?: "function",
                        function = FunctionCall(
                            name = acc.name ?: "",
                            arguments = acc.argumentsBuilder.toString().ifEmpty { "{}" }
                        )
                    )
                }

            return ChatMessage(
                role = "assistant",
                content = content,
                toolCalls = toolCalls.takeIf { it.isNotEmpty() }
            )
        }

        fun getFinishReason(): String? = finishReason

        private data class ToolCallAccumulator(
            val index: Int,
            var id: String? = null,
            var type: String? = null,
            var name: String? = null,
            val argumentsBuilder: StringBuilder = StringBuilder()
        )
    }
}

// =============================================================================
// 流事件密封类
// =============================================================================

/**
 * SSE 流式响应事件。
 */
sealed class StreamEvent {
    /** 增量文本内容。 */
    data class ContentDelta(val delta: String) : StreamEvent()

    /** 增量工具调用片段。 */
    data class ToolCallDelta(
        val index: Int,
        val id: String?,
        val type: String?,
        val functionName: String?,
        val argumentsDelta: String?
    ) : StreamEvent()

    /** 结束原因（stop / tool_calls / length 等）。 */
    data class FinishReason(val reason: String) : StreamEvent()

    /** 流正常结束。 */
    data class Done(val finishReason: String?) : StreamEvent()

    /** 发生错误。 */
    data class Error(val message: String, val exception: Throwable?) : StreamEvent()
}
