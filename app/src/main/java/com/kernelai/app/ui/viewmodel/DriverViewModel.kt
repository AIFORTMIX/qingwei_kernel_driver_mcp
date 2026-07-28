package com.kernelai.app.ui.viewmodel

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kernelai.app.data.model.ConnectionStatus
import com.kernelai.app.data.model.AppSettings
import com.kernelai.app.data.model.AiConfig
import com.kernelai.app.data.model.DriverSettings
import com.kernelai.app.data.model.ThemeMode
import com.kernelai.app.data.model.UiStyle
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * 驱动 ViewModel - 管理驱动连接状态、MCP 服务器状态、全局驱动操作
 */
class DriverViewModel : ViewModel() {

    private val _connectionStatus = MutableStateFlow(ConnectionStatus())
    val connectionStatus: StateFlow<ConnectionStatus> = _connectionStatus.asStateFlow()

    private val _appSettings = MutableStateFlow(AppSettings())
    val appSettings: StateFlow<AppSettings> = _appSettings.asStateFlow()

    private val _isInitializing = MutableStateFlow(false)
    val isInitializing: StateFlow<Boolean> = _isInitializing.asStateFlow()

    private val _logEntries = MutableStateFlow<List<LogEntry>>(emptyList())
    val logEntries: StateFlow<List<LogEntry>> = _logEntries.asStateFlow()

    /**
     * 初始化驱动连接和 MCP 服务器
     */
    fun initialize(context: Context) {
        viewModelScope.launch {
            _isInitializing.value = true
            addLog(LogEntry.LogLevel.INFO, LogCategory.SYSTEM, "正在初始化轻微 MCP...")

            try {
                // 加载设置
                loadSettings(context)

                // 尝试连接驱动
                addLog(LogEntry.LogLevel.INFO, LogCategory.DRIVER, "正在连接内核驱动...")
                val driverConnected = connectDriver()
                _connectionStatus.value = _connectionStatus.value.copy(driverConnected = driverConnected)

                if (driverConnected) {
                    addLog(LogEntry.LogLevel.INFO, LogCategory.DRIVER, "内核驱动连接成功")
                } else {
                    addLog(LogEntry.LogLevel.WARN, LogCategory.DRIVER, "内核驱动连接失败 - 设备未 root 或驱动未加载")
                }

                // 启动 MCP 服务器
                if (_appSettings.value.driverSettings.autoStartMcp) {
                    addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "正在启动 MCP 服务器...")
                    val mcpStarted = startMcpServer(_appSettings.value.driverSettings.mcpPort)
                    _connectionStatus.value = _connectionStatus.value.copy(
                        mcpServerRunning = mcpStarted,
                        mcpPort = _appSettings.value.driverSettings.mcpPort
                    )
                    if (mcpStarted) {
                        addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "MCP 服务器已启动，端口: ${_appSettings.value.driverSettings.mcpPort}")
                    } else {
                        addLog(LogEntry.LogLevel.ERROR, LogCategory.MCP, "MCP 服务器启动失败")
                    }
                }

                // 测试 AI 连接
                addLog(LogEntry.LogLevel.INFO, LogCategory.AI, "正在测试 AI 连接...")
                val aiConnected = testAiConnection(_appSettings.value.aiConfig)
                _connectionStatus.value = _connectionStatus.value.copy(aiConnected = aiConnected)
                if (aiConnected) {
                    addLog(LogEntry.LogLevel.INFO, LogCategory.AI, "AI 服务连接成功: ${_appSettings.value.aiConfig.model}")
                } else {
                    addLog(LogEntry.LogLevel.WARN, LogCategory.AI, "AI 服务连接失败 - 请检查配置")
                }

            } catch (e: Exception) {
                addLog(LogEntry.LogLevel.ERROR, LogCategory.SYSTEM, "初始化失败: ${e.message}")
                _connectionStatus.value = _connectionStatus.value.copy(
                    errorMessage = e.message
                )
            } finally {
                _isInitializing.value = false
            }
        }
    }

    /**
     * 重新连接驱动
     */
    fun reconnectDriver() {
        viewModelScope.launch {
            addLog(LogEntry.LogLevel.INFO, LogCategory.DRIVER, "正在重新连接驱动...")
            val connected = connectDriver()
            _connectionStatus.value = _connectionStatus.value.copy(driverConnected = connected)
            if (connected) {
                addLog(LogEntry.LogLevel.INFO, LogCategory.DRIVER, "驱动重连成功")
            } else {
                addLog(LogEntry.LogLevel.ERROR, LogCategory.DRIVER, "驱动重连失败")
            }
        }
    }

    /**
     * 启动/停止 MCP 服务器
     */
    fun toggleMcpServer() {
        viewModelScope.launch {
            if (_connectionStatus.value.mcpServerRunning) {
                addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "正在停止 MCP 服务器...")
                stopMcpServer()
                _connectionStatus.value = _connectionStatus.value.copy(mcpServerRunning = false)
                addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "MCP 服务器已停止")
            } else {
                addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "正在启动 MCP 服务器...")
                val started = startMcpServer(_appSettings.value.driverSettings.mcpPort)
                _connectionStatus.value = _connectionStatus.value.copy(mcpServerRunning = started)
                if (started) {
                    addLog(LogEntry.LogLevel.INFO, LogCategory.MCP, "MCP 服务器已启动")
                } else {
                    addLog(LogEntry.LogLevel.ERROR, LogCategory.MCP, "MCP 服务器启动失败")
                }
            }
        }
    }

    /**
     * 更新 AI 配置
     */
    fun updateAiConfig(config: AiConfig) {
        _appSettings.value = _appSettings.value.copy(aiConfig = config)
    }

    /**
     * 更新主题设置
     */
    fun updateThemeMode(mode: ThemeMode) {
        _appSettings.value = _appSettings.value.copy(themeMode = mode)
    }

    /**
     * 更新 UI 风格
     */
    fun updateUiStyle(style: UiStyle) {
        _appSettings.value = _appSettings.value.copy(uiStyle = style)
    }

    /**
     * 清空日志
     */
    fun clearLogs() {
        _logEntries.value = emptyList()
    }

    /**
     * 添加日志条目
     */
    fun addLog(entry: LogEntry) {
        _logEntries.value = _logEntries.value + entry
    }

    // --- 模拟驱动操作 ---

    private suspend fun connectDriver(): Boolean {
        delay(500)
        // 实际项目中调用 DeviceNode.open()
        return false // 模拟未连接状态
    }

    private suspend fun startMcpServer(port: Int): Boolean {
        delay(300)
        return true
    }

    private suspend fun stopMcpServer() {
        delay(100)
    }

    private suspend fun testAiConnection(config: AiConfig): Boolean {
        delay(500)
        // 实际项目中发送 HTTP 请求测试
        return false
    }

    private fun loadSettings(context: Context) {
        val prefs = context.getSharedPreferences("kernel_ai_settings", Context.MODE_PRIVATE)
        _appSettings.value = AppSettings(
            aiConfig = AiConfig(
                baseUrl = prefs.getString("ai_base_url", "http://localhost:11434/v1") ?: "http://localhost:11434/v1",
                apiKey = prefs.getString("ai_api_key", "") ?: "",
                model = prefs.getString("ai_model", "qwen2.5:7b") ?: "qwen2.5:7b",
            ),
            themeMode = ThemeMode.fromValue(prefs.getInt("color_mode", 0)),
            uiStyle = UiStyle.fromValue(prefs.getString("ui_mode", "miuix") ?: "miuix"),
            driverSettings = DriverSettings(
                autoConnect = prefs.getBoolean("auto_connect", true),
                mcpPort = prefs.getInt("mcp_port", 8080),
                autoStartMcp = prefs.getBoolean("auto_start_mcp", true),
            )
        )
    }

    /**
     * 日志条目
     */
    data class LogEntry(
        val timestamp: Long = System.currentTimeMillis(),
        val level: LogLevel,
        val category: LogCategory,
        val message: String
    ) {
        enum class LogLevel { VERBOSE, DEBUG, INFO, WARN, ERROR }
    }

    enum class LogCategory(val displayName: String) {
        DRIVER("驱动"),
        MCP("MCP"),
        AI("AI"),
        SYSTEM("系统")
    }
}
