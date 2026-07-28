package com.kernelai.app.data.model

/**
 * App 层进程信息模型
 */
data class ProcessInfo(
    val pid: Int,
    val ppid: Int,
    val comm: String,
    val cmdline: String,
    val state: String,
    val threadCount: Int,
    val isSelected: Boolean = false
) {
    val displayName: String
        get() = cmdline.ifEmpty { comm }
    
    val stateDescription: String
        get() = when (state.toLongOrNull() ?: -1L) {
            0L -> "Running"
            1L -> "Sleeping"
            2L -> "Disk Sleep"
            4L -> "Stopped"
            8L -> "Tracing Stop"
            16L -> "Zombie"
            else -> "Unknown"
        }
}
