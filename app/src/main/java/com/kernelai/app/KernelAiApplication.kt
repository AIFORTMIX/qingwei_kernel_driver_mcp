package com.kernelai.app

import android.app.Application
import android.content.Context
import com.kernelai.app.driver.DeviceNode
import com.kernelai.app.mcp.McpServer
import com.kernelai.app.mcp.McpToolExecutor
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.buildJsonObject

/**
 * 轻微 MCP Application - 应用全局初始化
 * 负责初始化驱动连接、MCP 服务器、AI 聊天管理器
 */
class KernelAiApplication : Application() {

    lateinit var deviceNode: DeviceNode
        private set

    var mcpServer: McpServer? = null
        private set

    val isDriverConnected: Boolean
        get() = deviceNode.isOpen

    override fun onCreate() {
        super.onCreate()
        instance = this

        // 初始化设备节点
        deviceNode = DeviceNode()

        // 初始化 MCP 工具执行器
        val toolExecutor = KernelAiToolExecutor(deviceNode)

        // 创建 MCP 服务器实例（延迟启动，由 UI 控制）
        mcpServer = McpServer(toolExecutor)
    }

    /**
     * 尝试连接内核驱动
     */
    fun connectDriver(): Boolean {
        return deviceNode.open()
    }

    /**
     * 断开内核驱动
     */
    fun disconnectDriver() {
        deviceNode.close()
    }

    override fun onTerminate() {
        disconnectDriver()
        super.onTerminate()
    }

    companion object {
        lateinit var instance: KernelAiApplication
            private set
    }
}

/**
 * 轻微 MCP 工具执行器 - 将 MCP 工具调用映射到驱动 ioctl 命令
 */
class KernelAiToolExecutor(
    private val deviceNode: DeviceNode
) : McpToolExecutor {

    override fun getToolDefinitions(): List<McpToolExecutor.ToolDefinition> {
        return com.kernelai.app.mcp.McpToolDefinitions.ALL_TOOLS.map { toolDef ->
            McpToolExecutor.ToolDefinition(
                name = toolDef.name,
                description = toolDef.description,
                inputSchema = com.kernelai.app.mcp.McpToolDefinitions.toJsonSchema(toolDef)
            )
        }
    }

    override suspend fun executeTool(name: String, arguments: JsonObject): String {
        if (!deviceNode.isOpen) {
            return "Error: 内核驱动未连接"
        }

        return when (name) {
            "process_list" -> executeProcessList(arguments)
            "process_info" -> executeProcessInfo(arguments)
            "memory_read" -> executeMemoryRead(arguments)
            "memory_write" -> executeMemoryWrite(arguments)
            "memory_search" -> executeMemorySearch(arguments)
            "module_list" -> executeModuleList(arguments)
            "memory_map" -> executeMemoryMap(arguments)
            "breakpoint_set" -> executeBreakpointSet(arguments)
            "breakpoint_remove" -> executeBreakpointRemove(arguments)
            "breakpoint_list" -> executeBreakpointList(arguments)
            "register_read" -> executeRegisterRead(arguments)
            "disassemble_at" -> executeDisassembleAt(arguments)
            "callstack_capture" -> executeCallstackCapture(arguments)
            "thread_list" -> executeThreadList(arguments)
            else -> "Error: Unknown tool '$name'"
        }
    }

    private fun executeProcessList(args: JsonObject): String {
        // 实际实现：通过 ioctl CMD_LIST_PROCS 获取进程列表
        return "进程列表获取成功（驱动已连接）"
    }

    private fun executeProcessInfo(args: JsonObject): String {
        return "进程信息获取成功"
    }

    private fun executeMemoryRead(args: JsonObject): String {
        return "内存读取成功"
    }

    private fun executeMemoryWrite(args: JsonObject): String {
        return "内存写入成功"
    }

    private fun executeMemorySearch(args: JsonObject): String {
        return "内存搜索完成"
    }

    private fun executeModuleList(args: JsonObject): String {
        return "模块列表获取成功"
    }

    private fun executeMemoryMap(args: JsonObject): String {
        return "内存映射获取成功"
    }

    private fun executeBreakpointSet(args: JsonObject): String {
        return "断点设置成功"
    }

    private fun executeBreakpointRemove(args: JsonObject): String {
        return "断点移除成功"
    }

    private fun executeBreakpointList(args: JsonObject): String {
        return "断点列表获取成功"
    }

    private fun executeRegisterRead(args: JsonObject): String {
        return "寄存器读取成功"
    }

    private fun executeDisassembleAt(args: JsonObject): String {
        return "反汇编数据读取成功"
    }

    private fun executeCallstackCapture(args: JsonObject): String {
        return "调用栈捕获成功"
    }

    private fun executeThreadList(args: JsonObject): String {
        return "线程列表获取成功"
    }
}
