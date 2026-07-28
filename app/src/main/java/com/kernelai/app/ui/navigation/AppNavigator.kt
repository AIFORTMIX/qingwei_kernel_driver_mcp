package com.kernelai.app.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.navigation3.runtime.NavKey
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow

/**
 * 导航管理器 - 管理返回栈和页面间结果传递
 */
class AppNavigator(
    initialKey: NavKey
) {
    val backStack: SnapshotStateList<NavKey> = mutableStateListOf(initialKey)

    private val resultBus = mutableMapOf<String, MutableSharedFlow<Any>>()

    /** 压入新页面 */
    fun push(key: NavKey) {
        backStack.add(key)
    }

    /** 替换栈顶页面 */
    fun replace(key: NavKey) {
        if (backStack.isNotEmpty()) {
            backStack[backStack.lastIndex] = key
        } else {
            backStack.add(key)
        }
    }

    /** 弹出栈顶页面 */
    fun pop() {
        backStack.removeLastOrNull()
    }

    /** 弹出直到满足条件 */
    fun popUntil(predicate: (NavKey) -> Boolean) {
        while (backStack.isNotEmpty() && !predicate(backStack.last())) {
            backStack.removeAt(backStack.lastIndex)
        }
    }

    /** 导航并期望返回结果 */
    fun navigateForResult(route: Route, requestKey: String) {
        ensureChannel(requestKey)
        push(route)
    }

    /** 设置返回结果并弹出 */
    fun <T : Any> setResult(requestKey: String, value: T) {
        ensureChannel(requestKey).tryEmit(value)
        pop()
    }

    /** 观察返回结果 */
    @Suppress("UNCHECKED_CAST")
    fun <T : Any> observeResult(requestKey: String): SharedFlow<T> {
        return ensureChannel(requestKey) as SharedFlow<T>
    }

    /** 清除结果缓存 */
    @OptIn(ExperimentalCoroutinesApi::class)
    fun clearResult(requestKey: String) {
        ensureChannel(requestKey).resetReplayCache()
    }

    /** 获取当前页面 */
    fun current(): NavKey? = backStack.lastOrNull()

    /** 获取返回栈大小 */
    fun backStackSize(): Int = backStack.size

    private fun ensureChannel(key: String): MutableSharedFlow<Any> {
        return resultBus.getOrPut(key) { MutableSharedFlow(replay = 1, extraBufferCapacity = 0) }
    }

    companion object {
        val Saver: Saver<AppNavigator, Any> = listSaver(save = { navigator ->
            navigator.backStack.toList()
        }, restore = { savedList ->
            val initialKey = savedList.firstOrNull() ?: Route.Main
            val navigator = AppNavigator(initialKey)
            navigator.backStack.clear()
            navigator.backStack.addAll(savedList)
            navigator
        })
    }
}

/**
 * 创建可保存的导航器实例
 */
@Composable
fun rememberAppNavigator(startRoute: NavKey): AppNavigator {
    return rememberSaveable(startRoute, saver = AppNavigator.Saver) {
        AppNavigator(startRoute)
    }
}

/**
 * CompositionLocal 提供导航器
 */
val LocalAppNavigator = staticCompositionLocalOf<AppNavigator> {
    error("LocalAppNavigator not provided")
}
