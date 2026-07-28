package com.kernelai.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kernelai.app.data.model.ProcessInfo
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * 进程浏览器 ViewModel - 加载进程列表、搜索过滤、展开详情
 */
class ProcessViewModel : ViewModel() {

    private val _allProcesses = MutableStateFlow<List<ProcessInfo>>(emptyList())
    val allProcesses: StateFlow<List<ProcessInfo>> = _allProcesses.asStateFlow()

    private val _filteredProcesses = MutableStateFlow<List<ProcessInfo>>(emptyList())
    val filteredProcesses: StateFlow<List<ProcessInfo>> = _filteredProcesses.asStateFlow()

    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    private val _expandedPid = MutableStateFlow<Int?>(null)
    val expandedPid: StateFlow<Int?> = _expandedPid.asStateFlow()

    private val _processDetails = MutableStateFlow<Map<Int, ProcessDetails>>(emptyMap())
    val processDetails: StateFlow<Map<Int, ProcessDetails>> = _processDetails.asStateFlow()

    private val _selectedProcess = MutableStateFlow<ProcessInfo?>(null)
    val selectedProcess: StateFlow<ProcessInfo?> = _selectedProcess.asStateFlow()

    init {
        loadProcesses()
    }

    /**
     * 加载进程列表（从驱动获取）
     */
    fun loadProcesses() {
        viewModelScope.launch {
            _isLoading.value = true
            try {
                // 模拟从驱动加载进程列表
                delay(300)
                _allProcesses.value = generateMockProcesses()
                applyFilter()
            } catch (e: Exception) {
                // 错误处理
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * 刷新进程列表（下拉刷新）
     */
    fun refresh() {
        loadProcesses()
    }

    /**
     * 更新搜索关键字
     */
    fun updateSearchQuery(query: String) {
        _searchQuery.value = query
        applyFilter()
    }

    /**
     * 展开/折叠进程详情
     */
    fun toggleExpand(pid: Int) {
        if (_expandedPid.value == pid) {
            _expandedPid.value = null
        } else {
            _expandedPid.value = pid
            // 如果还没有加载过详情，则加载
            if (!_processDetails.value.containsKey(pid)) {
                loadProcessDetails(pid)
            }
        }
    }

    /**
     * 选择进程（用于内存查看、断点设置等后续操作）
     */
    fun selectProcess(process: ProcessInfo) {
        _selectedProcess.value = process
    }

    /**
     * 加载进程详细信息（模块、VMA、线程）
     */
    private fun loadProcessDetails(pid: Int) {
        viewModelScope.launch {
            delay(200) // 模拟加载延迟
            val details = ProcessDetails(
                modules = generateMockModules(pid),
                memoryMaps = generateMockVmaEntries(pid),
                threads = generateMockThreads(pid)
            )
            _processDetails.value = _processDetails.value + (pid to details)
        }
    }

    /**
     * 应用搜索过滤
     */
    private fun applyFilter() {
        val query = _searchQuery.value.trim().lowercase()
        _filteredProcesses.value = if (query.isEmpty()) {
            _allProcesses.value
        } else {
            _allProcesses.value.filter { proc ->
                proc.displayName.lowercase().contains(query) ||
                proc.comm.lowercase().contains(query) ||
                proc.pid.toString().contains(query)
            }
        }
    }

    /**
     * 生成模拟进程数据（实际项目中从驱动 ioctl 获取）
     */
    private fun generateMockProcesses(): List<ProcessInfo> {
        return listOf(
            ProcessInfo(pid = 1, ppid = 0, comm = "init", cmdline = "/init", state = "1", threadCount = 1),
            ProcessInfo(pid = 128, ppid = 1, comm = "kthreadd", cmdline = "", state = "1", threadCount = 1),
            ProcessInfo(pid = 512, ppid = 1, comm = "system_server", cmdline = "system_server", state = "1", threadCount = 128),
            ProcessInfo(pid = 768, ppid = 512, comm = "surfaceflinger", cmdline = "/system/bin/surfaceflinger", state = "1", threadCount = 24),
            ProcessInfo(pid = 1024, ppid = 512, comm = "com.android.phone", cmdline = "com.android.phone", state = "1", threadCount = 45),
            ProcessInfo(pid = 1536, ppid = 512, comm = "com.android.systemui", cmdline = "com.android.systemui", state = "1", threadCount = 56),
            ProcessInfo(pid = 2048, ppid = 512, comm = "com.kernelai.app", cmdline = "com.kernelai.app", state = "0", threadCount = 32),
            ProcessInfo(pid = 2560, ppid = 512, comm = "com.android.launcher3", cmdline = "com.android.launcher3", state = "1", threadCount = 38),
            ProcessInfo(pid = 3072, ppid = 512, comm = "mediaserver", cmdline = "/system/bin/mediaserver", state = "1", threadCount = 16),
            ProcessInfo(pid = 3584, ppid = 1, comm = "logd", cmdline = "/system/bin/logd", state = "1", threadCount = 8),
        )
    }

    private fun generateMockModules(pid: Int): List<ModuleInfo> {
        return listOf(
            ModuleInfo("libandroid_runtime.so", "0x7F00000000", 4096000, "r-xp"),
            ModuleInfo("libart.so", "0x7F10000000", 8192000, "r-xp"),
            ModuleInfo("libc.so", "0x7F20000000", 1024000, "r-xp"),
            ModuleInfo("libm.so", "0x7F30000000", 512000, "r-xp"),
            ModuleInfo("app.odex", "0x7F40000000", 2048000, "r-xp"),
        )
    }

    private fun generateMockVmaEntries(pid: Int): List<VmaEntry> {
        return listOf(
            VmaEntry("0x7F00000000", "0x7F00400000", "r-xp", "libandroid_runtime.so"),
            VmaEntry("0x7F00400000", "0x7F00500000", "rw-p", "libandroid_runtime.so"),
            VmaEntry("0x7F10000000", "0x7F10800000", "r-xp", "libart.so"),
            VmaEntry("0x7F20000000", "0x7F20100000", "r-xp", "libc.so"),
            VmaEntry("0x7F40000000", "0x7F40200000", "r-xp", "app.odex"),
        )
    }

    private fun generateMockThreads(pid: Int): List<ThreadInfo> {
        return listOf(
            ThreadInfo(1, "main", "Running", "0x7FCE000000", "0x7F40001234"),
            ThreadInfo(2, "Jit thread pool", "Sleep", "0x7FCD000000", "0x7F10005678"),
            ThreadInfo(3, "Signal Catcher", "Sleep", "0x7FCC000000", "0x7F10009ABC"),
            ThreadInfo(4, "Binder:2048_1", "Sleep", "0x7FCB000000", "0x7F00001000"),
            ThreadInfo(5, "Binder:2048_2", "Sleep", "0x7FCA000000", "0x7F00002000"),
        )
    }

    /**
     * 进程详情数据
     */
    data class ProcessDetails(
        val modules: List<ModuleInfo> = emptyList(),
        val memoryMaps: List<VmaEntry> = emptyList(),
        val threads: List<ThreadInfo> = emptyList()
    )

    data class ModuleInfo(
        val name: String,
        val baseAddress: String,
        val size: Long,
        val permissions: String
    )

    data class VmaEntry(
        val startAddr: String,
        val endAddr: String,
        val perms: String,
        val name: String
    )

    data class ThreadInfo(
        val tid: Int,
        val name: String,
        val state: String,
        val stackPtr: String,
        val pc: String
    )
}
