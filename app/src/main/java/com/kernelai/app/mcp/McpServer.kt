package com.kernelai.app.mcp

import kotlinx.serialization.json.*
import kotlinx.coroutines.*
import java.util.concurrent.ConcurrentHashMap

/**
 * MCP Server - Model Context Protocol 服务器
 * 内嵌于 App 进程，通过 Streamable HTTP 暴露驱动能力
 */
class McpServer(
    private val toolExecutor: McpToolExecutor
) {
    private val sessions = ConcurrentHashMap<String, McpSession>()
    
    data class McpSession(
        val id: String,
        val createdAt: Long = System.currentTimeMillis(),
        var lastActive: Long = System.currentTimeMillis()
    )
    
    // MCP 能力声明
    val capabilities = buildJsonObject {
        putJsonObject("tools") {
            put("listChanged", true)
        }
    }
    
    // 处理 JSON-RPC 消息
    suspend fun handleMessage(message: String): String? {
        val json = Json { ignoreUnknownKeys = true }
        val msg = json.parseToJsonElement(message).jsonObject
        
        val method = msg["method"]?.jsonPrimitive?.content ?: return null
        val id = msg["id"]
        val params = msg["params"]?.jsonObject
        
        return when (method) {
            "initialize" -> handleInitialize(id, params)
            "tools/list" -> handleToolsList(id)
            "tools/call" -> handleToolsCall(id, params)
            "ping" -> buildJsonRpcResponse(id, buildJsonObject { })
            else -> buildJsonRpcError(id, -32601, "Method not found: $method")
        }
    }
    
    private fun handleInitialize(id: JsonElement?, params: JsonObject?): String {
        val result = buildJsonObject {
            putJsonObject("protocolVersion") { }
            put("protocolVersion", "2025-03-26")
            putJsonObject("capabilities") {
                putJsonObject("tools") {
                    put("listChanged", true)
                }
            }
            putJsonObject("serverInfo") {
                put("name", "qingwei-mcp")
                put("version", "1.0.0")
            }
        }
        return buildJsonRpcResponse(id, result)
    }
    
    private fun handleToolsList(id: JsonElement?): String {
        val tools = toolExecutor.getToolDefinitions()
        val result = buildJsonObject {
            putJsonArray("tools") {
                tools.forEach { tool ->
                    addJsonObject {
                        put("name", tool.name)
                        put("description", tool.description)
                        put("inputSchema", tool.inputSchema)
                    }
                }
            }
        }
        return buildJsonRpcResponse(id, result)
    }
    
    private suspend fun handleToolsCall(id: JsonElement?, params: JsonObject?): String {
        if (params == null) {
            return buildJsonRpcError(id, -32602, "Missing params")
        }
        
        val toolName = params["name"]?.jsonPrimitive?.content
            ?: return buildJsonRpcError(id, -32602, "Missing tool name")
        val arguments = params["arguments"]?.jsonObject
            ?: JsonObject(emptyMap())
        
        return try {
            val result = toolExecutor.executeTool(toolName, arguments)
            buildJsonRpcResponse(id, buildJsonObject {
                putJsonArray("content") {
                    addJsonObject {
                        put("type", "text")
                        put("text", result)
                    }
                }
            })
        } catch (e: Exception) {
            buildJsonRpcResponse(id, buildJsonObject {
                putJsonArray("content") {
                    addJsonObject {
                        put("type", "text")
                        put("text", "Error: ${e.message}")
                    }
                }
                put("isError", true)
            })
        }
    }
    
    private fun buildJsonRpcResponse(id: JsonElement?, result: JsonObject): String {
        return buildJsonObject {
            put("jsonrpc", "2.0")
            put("id", id)
            put("result", result)
        }.toString()
    }
    
    private fun buildJsonRpcError(id: JsonElement?, code: Int, message: String): String {
        return buildJsonObject {
            put("jsonrpc", "2.0")
            put("id", id)
            putJsonObject("error") {
                put("code", code)
                put("message", message)
            }
        }.toString()
    }
}

/**
 * MCP 工具执行器接口
 */
interface McpToolExecutor {
    data class ToolDefinition(
        val name: String,
        val description: String,
        val inputSchema: JsonObject
    )
    
    fun getToolDefinitions(): List<ToolDefinition>
    suspend fun executeTool(name: String, arguments: JsonObject): String
}
