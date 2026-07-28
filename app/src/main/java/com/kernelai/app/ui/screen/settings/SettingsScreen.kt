package com.kernelai.app.ui.screen.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Circle
import androidx.compose.material.icons.rounded.ColorLens
import androidx.compose.material.icons.rounded.Computer
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.SmartToy
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kernelai.app.data.model.AiConfig
import com.kernelai.app.data.model.ConnectionStatus
import com.kernelai.app.data.model.ThemeMode
import com.kernelai.app.data.model.UiStyle
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import com.kernelai.app.ui.viewmodel.DriverViewModel
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 设置页面 - AI 配置、驱动连接、MCP 服务器、主题、关于
 */
@Composable
fun SettingsScreen(
    connectionStatus: ConnectionStatus = ConnectionStatus(),
    appSettings: com.kernelai.app.data.model.AppSettings = com.kernelai.app.data.model.AppSettings(),
    onAiConfigChange: (AiConfig) -> Unit = {},
    onThemeModeChange: (ThemeMode) -> Unit = {},
    onUiStyleChange: (UiStyle) -> Unit = {},
    onReconnectDriver: () -> Unit = {},
    onToggleMcp: () -> Unit = {}
) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        // AI 配置区域
        SettingsSection(title = "AI 配置", icon = Icons.Rounded.SmartToy) {
            SettingsItem(
                title = "Base URL",
                subtitle = appSettings.aiConfig.baseUrl,
                onClick = { /* 编辑对话框 */ }
            )
            SettingsItem(
                title = "模型",
                subtitle = appSettings.aiConfig.model,
                onClick = { /* 编辑对话框 */ }
            )
            SettingsItem(
                title = "API Key",
                subtitle = if (appSettings.aiConfig.apiKey.isNotEmpty()) "****已设置" else "未设置",
                onClick = { /* 编辑对话框 */ }
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 连接状态区域
        SettingsSection(title = "连接状态", icon = Icons.Rounded.Link) {
            ConnectionStatusItem(
                label = "内核驱动",
                connected = connectionStatus.driverConnected,
                onAction = onReconnectDriver
            )
            ConnectionStatusItem(
                label = "MCP 服务器",
                connected = connectionStatus.mcpServerRunning,
                subtitle = if (connectionStatus.mcpServerRunning) "端口: ${connectionStatus.mcpPort}" else "已停止",
                onAction = onToggleMcp
            )
            ConnectionStatusItem(
                label = "AI 服务",
                connected = connectionStatus.aiConnected,
                subtitle = if (connectionStatus.aiConnected) appSettings.aiConfig.model else "未连接"
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 主题设置
        SettingsSection(title = "主题设置", icon = Icons.Rounded.ColorLens) {
            // UI 风格选择
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                UiStyle.entries.forEach { style ->
                    val isSelected = appSettings.uiStyle == style
                    Surface(
                        shape = RoundedCornerShape(12.dp),
                        color = if (isSelected) Color(0xFF2196F3).copy(alpha = 0.15f)
                                else textColor.copy(alpha = 0.05f),
                        modifier = Modifier
                            .weight(1f)
                            .clickable { onUiStyleChange(style) }
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp),
                            horizontalAlignment = Alignment.CenterHorizontally
                        ) {
                            Text(
                                text = style.displayName,
                                fontSize = 14.sp,
                                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal
                            )
                            if (isSelected) {
                                Text(
                                    text = "当前",
                                    fontSize = 10.sp,
                                    color = Color(0xFF2196F3)
                                )
                            }
                        }
                    }
                }
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            // 深色模式选择
            ThemeMode.entries.forEach { mode ->
                val isSelected = appSettings.themeMode == mode
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onThemeModeChange(mode) }
                        .padding(vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Circle,
                        contentDescription = null,
                        tint = if (isSelected) Color(0xFF2196F3) else Color.Gray,
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Text(
                        text = mode.displayName,
                        fontSize = 14.sp,
                        modifier = Modifier.weight(1f)
                    )
                    if (isSelected) {
                        Icon(
                            imageVector = Icons.Rounded.Circle,
                            contentDescription = null,
                            tint = Color(0xFF2196F3),
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 关于
        SettingsSection(title = "关于", icon = Icons.Rounded.Info) {
            SettingsItem(
                title = "轻微 MCP",
                subtitle = "版本 1.0.0",
                onClick = {}
            )
            SettingsItem(
                title = "内核驱动",
                subtitle = "qingwei_mcp v1.0",
                onClick = {}
            )
            SettingsItem(
                title = "MCP 协议",
                subtitle = "Streamable HTTP v2025-03-26",
                onClick = {}
            )
        }

        Spacer(modifier = Modifier.height(32.dp))
    }
}

/**
 * 设置区域容器
 */
@Composable
fun SettingsSection(
    title: String,
    icon: ImageVector,
    content: @Composable () -> Unit
) {
    val uiMode = LocalUiMode.current
    val cardColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
    }
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    Surface(
        shape = RoundedCornerShape(16.dp),
        color = cardColor
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.padding(bottom = 8.dp)
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = Color(0xFF2196F3),
                    modifier = Modifier.size(20.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = title,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Bold,
                    color = textColor
                )
            }
            content()
        }
    }
}

/**
 * 设置项
 */
@Composable
fun SettingsItem(
    title: String,
    subtitle: String,
    onClick: () -> Unit
) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(text = title, fontSize = 14.sp, color = textColor)
            Text(
                text = subtitle,
                fontSize = 12.sp,
                color = textColor.copy(alpha = 0.5f)
            )
        }
    }
}

/**
 * 连接状态项
 */
@Composable
fun ConnectionStatusItem(
    label: String,
    connected: Boolean,
    subtitle: String = "",
    onAction: (() -> Unit)? = null
) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }
    val statusColor = if (connected) Color(0xFF4CAF50) else Color(0xFFF44336)

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (onAction != null) Modifier.clickable(onClick = onAction) else Modifier)
            .padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = Icons.Rounded.Circle,
            contentDescription = null,
            tint = statusColor,
            modifier = Modifier.size(12.dp)
        )
        Spacer(modifier = Modifier.width(8.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(text = label, fontSize = 14.sp, color = textColor)
            if (subtitle.isNotEmpty()) {
                Text(
                    text = subtitle,
                    fontSize = 12.sp,
                    color = textColor.copy(alpha = 0.5f)
                )
            }
        }
        Text(
            text = if (connected) "已连接" else "未连接",
            fontSize = 12.sp,
            color = statusColor,
            fontWeight = FontWeight.Medium
        )
    }
}
