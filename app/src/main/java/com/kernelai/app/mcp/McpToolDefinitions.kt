package com.kernelai.app.mcp

import kotlinx.serialization.json.*

/**
 * MCP 工具定义 - 14 个工具对应内核驱动的 ioctl 命令
 */
object McpToolDefinitions {
    
    val ALL_TOOLS = listOf(
        ToolDef(
            name = "process_list",
            description = "列出设备上所有正在运行的进程。返回每个进程的 PID、包名、命令行、状态和线程数。",
            params = listOf(
                Param("offset", "integer", "分页偏移量", false),
                Param("max_count", "integer", "最大返回数量", false),
                Param("filter", "string", "包名过滤关键字", false)
            )
        ),
        ToolDef(
            name = "process_info",
            description = "获取指定进程的详细信息，包括加载的模块列表、内存映射概览、线程列表。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名（与 pid 二选一）", false)
            )
        ),
        ToolDef(
            name = "memory_read",
            description = "读取目标进程指定地址的内存内容。返回十六进制字节数据。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名", false),
                Param("address", "string", "目标地址（十六进制字符串）", true),
                Param("size", "integer", "读取字节数", true)
            )
        ),
        ToolDef(
            name = "memory_write",
            description = "向目标进程指定地址写入数据。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名", false),
                Param("address", "string", "目标地址（十六进制）", true),
                Param("data", "string", "写入数据（十六进制字符串）", true)
            )
        ),
        ToolDef(
            name = "memory_search",
            description = "在目标进程地址空间中搜索字节模式。支持通配符掩码（?? 表示忽略）。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pattern", "string", "搜索模式（十六进制，?? 表示通配符）", true),
                Param("start_addr", "string", "搜索起始地址", false),
                Param("end_addr", "string", "搜索结束地址", false),
                Param("max_results", "integer", "最大结果数", false)
            )
        ),
        ToolDef(
            name = "module_list",
            description = "列出目标进程加载的所有共享库/模块。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名", false)
            )
        ),
        ToolDef(
            name = "memory_map",
            description = "列出目标进程的完整虚拟内存映射（VMA 列表）。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名", false),
                Param("filter_executable", "boolean", "仅返回可执行段", false)
            )
        ),
        ToolDef(
            name = "breakpoint_set",
            description = "在目标地址设置断点。支持硬件断点（执行/读/写/读写）和软件断点。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("address", "string", "断点地址（十六进制）", true),
                Param("type", "string", "断点类型: hw_execute/hw_read/hw_write/hw_access/sw", true),
                Param("length", "integer", "监视长度（仅数据断点）", false)
            )
        ),
        ToolDef(
            name = "breakpoint_remove",
            description = "移除指定断点。",
            params = listOf(
                Param("bp_id", "integer", "断点 ID/索引", true),
                Param("type", "string", "断点类型: hw/sw", true)
            )
        ),
        ToolDef(
            name = "breakpoint_list",
            description = "列出所有已设置的断点及其状态。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false)
            )
        ),
        ToolDef(
            name = "register_read",
            description = "读取目标进程在断点命中时的寄存器快照（ARM64 X0-X30, SP, PC, PSTATE）。",
            params = listOf(
                Param("bp_id", "integer", "断点 ID", true)
            )
        ),
        ToolDef(
            name = "disassemble_at",
            description = "读取目标地址的原始字节数据，用于外部反汇编。返回十六进制字节。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("address", "string", "起始地址（十六进制）", true),
                Param("count", "integer", "读取指令数量", false)
            )
        ),
        ToolDef(
            name = "callstack_capture",
            description = "捕获目标进程在断点命中时的调用栈（ARM64 FP 链回溯）。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("bp_id", "integer", "触发断点 ID", true),
                Param("max_frames", "integer", "最大帧数", false)
            )
        ),
        ToolDef(
            name = "thread_list",
            description = "列出目标进程的所有线程。",
            params = listOf(
                Param("pid", "integer", "目标进程 PID", false),
                Param("pkg_name", "string", "目标包名", false)
            )
        )
    )
    
    data class ToolDef(
        val name: String,
        val description: String,
        val params: List<Param>
    )
    
    data class Param(
        val name: String,
        val type: String,
        val description: String,
        val required: Boolean
    )
    
    fun toJsonSchema(tool: ToolDef): JsonObject {
        return buildJsonObject {
            put("type", "object")
            putJsonObject("properties") {
                tool.params.forEach { param ->
                    putJsonObject(param.name) {
                        put("type", param.type)
                        put("description", param.description)
                    }
                }
            }
            val required = tool.params.filter { it.required }.map { it.name }
            if (required.isNotEmpty()) {
                putJsonArray("required") {
                    required.forEach { add(it) }
                }
            }
        }
    }
}
