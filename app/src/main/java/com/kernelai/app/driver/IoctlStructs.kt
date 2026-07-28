package com.kernelai.app.driver

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 数据结构映射 - 与内核侧 qingwei_mcp.h 中的 struct 对应
 * 使用 ByteBuffer 进行序列化/反序列化
 */
object IoctlStructs {

    data class ProcessInfo(
        val pid: Int,
        val ppid: Int,
        val tgid: Int,
        val comm: String,
        val cmdline: String,
        val state: Long,
        val threadCount: Int
    )

    data class ModuleInfo(
        val baseAddr: Long,
        val endAddr: Long,
        val size: Long,
        val flags: Int,
        val offset: Long,
        val path: String,
        val isExecutable: Boolean
    )

    data class VmaEntry(
        val start: Long,
        val end: Long,
        val flags: Int,
        val pgoff: Long,
        val name: String
    )

    data class ThreadInfo(
        val tid: Int,
        val comm: String,
        val state: Long,
        val stackPtr: Long,
        val pc: Long
    )

    data class BreakpointInfo(
        val bpIndex: Int,
        val bpAddr: Long,
        val bpType: Int,
        val bpLen: Int,
        val hitCount: Long,
        val active: Boolean
    )

    data class StackFrame(
        val pc: Long,
        val sp: Long,
        val fp: Long,
        val lr: Long,
        val symbol: String
    )

    // 构建进程列表请求
    fun buildProcListPacket(maxCount: Int, offset: Int = 0): ByteBuffer {
        val buf = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN)
        buf.putInt(maxCount)     // max_count
        buf.putInt(0)            // actual_count (kernel fills)
        buf.putInt(0)            // total_count (kernel fills)
        buf.putInt(offset)       // offset
        buf.putLong(0L)          // out_buf (set by JNI)
        buf.putLong(0L)          // out_buf_size
        buf.flip()
        return buf
    }

    // 解析进程信息
    fun parseProcessInfo(data: ByteArray, count: Int): List<ProcessInfo> {
        val buf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val result = mutableListOf<ProcessInfo>()
        val entrySize = 4 + 4 + 4 + 16 + 256 + 8 + 4  // 292 bytes per entry

        for (i in 0 until count) {
            val pos = i * entrySize
            if (pos + entrySize > data.size) break
            buf.position(pos)
            val pid = buf.int
            val ppid = buf.int
            val tgid = buf.int
            val commBytes = ByteArray(16)
            buf.get(commBytes)
            val cmdlineBytes = ByteArray(256)
            buf.get(cmdlineBytes)
            val state = buf.long
            val threadCount = buf.int

            result.add(ProcessInfo(
                pid = pid,
                ppid = ppid,
                tgid = tgid,
                comm = String(commBytes).trimEnd('\u0000'),
                cmdline = String(cmdlineBytes).trimEnd('\u0000'),
                state = state,
                threadCount = threadCount
            ))
        }
        return result
    }
}
