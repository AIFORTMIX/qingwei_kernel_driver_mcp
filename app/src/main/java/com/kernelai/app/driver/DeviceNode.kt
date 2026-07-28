package com.kernelai.app.driver

import java.io.File

/**
 * 管理 /dev/qingwei_mcp 设备节点的文件描述符
 */
class DeviceNode {
    companion object {
        const val DEVICE_PATH = "/dev/qingwei_mcp"

        init {
            try {
                System.loadLibrary("qingwei_mcp_jni")
            } catch (e: UnsatisfiedLinkError) {
                // JNI library not available, running in mock mode
            }
        }
    }

    private var fileDescriptor: Int = -1

    val isOpen: Boolean get() = fileDescriptor >= 0

    fun open(): Boolean {
        return try {
            val deviceFile = File(DEVICE_PATH)
            if (!deviceFile.exists()) return false
            fileDescriptor = nativeOpen(DEVICE_PATH)
            fileDescriptor >= 0
        } catch (e: Exception) {
            false
        }
    }

    fun close() {
        if (fileDescriptor >= 0) {
            nativeClose(fileDescriptor)
            fileDescriptor = -1
        }
    }

    fun ioctl(command: Int, arg: ByteArray): Int {
        if (fileDescriptor < 0) return -1
        return nativeIoctl(fileDescriptor, command, arg)
    }

    private external fun nativeOpen(path: String): Int
    private external fun nativeClose(fd: Int): Int
    private external fun nativeIoctl(fd: Int, command: Int, arg: ByteArray): Int
}
