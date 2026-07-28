package com.kernelai.app.ui.navigation

import android.os.Parcelable
import androidx.navigation3.runtime.NavKey
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.Serializable

/**
 * 类型安全的导航路由定义 - 基于 Navigation3
 * 每个目的地是一个 NavKey (data object/data class)，可保存/恢复到返回栈中
 */
sealed interface Route : NavKey, Parcelable {

    /** 主页面容器（包含底部导航） */
    @Parcelize
    @Serializable
    data object Main : Route

    /** 首页 - AI 聊天 */
    @Parcelize
    @Serializable
    data object Chat : Route

    /** 进程浏览器 */
    @Parcelize
    @Serializable
    data object ProcessBrowser : Route

    /** 断点管理器 */
    @Parcelize
    @Serializable
    data object BreakpointManager : Route

    /** 设置页面 */
    @Parcelize
    @Serializable
    data object Settings : Route

    /** 内存查看器（侧边导航进入） */
    @Parcelize
    @Serializable
    data object MemoryViewer : Route

    /** 反汇编查看器（侧边导航进入） */
    @Parcelize
    @Serializable
    data object DisasmViewer : Route

    /** 日志查看器（侧边导航进入） */
    @Parcelize
    @Serializable
    data object LogViewer : Route
}
