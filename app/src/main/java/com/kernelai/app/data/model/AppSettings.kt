package com.kernelai.app.data.model

/**
 * 应用设置数据模型
 */
data class AppSettings(
    val aiConfig: AiConfig = AiConfig(),
    val themeMode: ThemeMode = ThemeMode.SYSTEM,
    val uiStyle: UiStyle = UiStyle.MIUIX,
    val driverSettings: DriverSettings = DriverSettings()
)

/**
 * AI 配置
 */
data class AiConfig(
    val baseUrl: String = "http://localhost:11434/v1",
    val apiKey: String = "",
    val model: String = "qwen2.5:7b",
    val maxTokens: Int = 4096,
    val temperature: Double = 0.7
)

/**
 * 主题模式
 */
enum class ThemeMode(val value: Int, val displayName: String) {
    SYSTEM(0, "跟随系统"),
    LIGHT(1, "浅色模式"),
    DARK(2, "深色模式");

    companion object {
        fun fromValue(value: Int): ThemeMode =
            entries.find { it.value == value } ?: SYSTEM
    }
}

/**
 * UI 风格
 */
enum class UiStyle(val value: String, val displayName: String) {
    MIUIX("miuix", "Miuix"),
    MATERIAL("material", "Material");

    companion object {
        fun fromValue(value: String): UiStyle =
            entries.find { it.value == value } ?: MIUIX
    }
}

/**
 * 驱动设置
 */
data class DriverSettings(
    val autoConnect: Boolean = true,
    val mcpPort: Int = 8080,
    val autoStartMcp: Boolean = true
)

/**
 * 连接状态
 */
data class ConnectionStatus(
    val driverConnected: Boolean = false,
    val mcpServerRunning: Boolean = false,
    val aiConnected: Boolean = false,
    val mcpPort: Int = 8080,
    val errorMessage: String? = null
)
