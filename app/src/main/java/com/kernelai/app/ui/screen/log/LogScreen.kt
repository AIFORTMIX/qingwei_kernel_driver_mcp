package com.kernelai.app.ui.screen.log

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoScroll
import androidx.compose.material.icons.rounded.ClearAll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.ToggleButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import com.kernelai.app.ui.viewmodel.DriverViewModel
import com.kernelai.app.ui.viewmodel.DriverViewModel.LogCategory
import com.kernelai.app.ui.viewmodel.DriverViewModel.LogEntry
import top.yukonga.miuix.kmp.theme.MiuixTheme
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 实时日志查看器 - 支持按级别/类别过滤、自动滚动、清除
 */
@Composable
fun LogScreen(
    logEntries: List<LogEntry> = emptyList(),
    onClear: () -> Unit = {}
) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    var selectedLevels by remember { mutableStateOf(setOf(LogEntry.LogLevel.INFO, LogEntry.LogLevel.WARN, LogEntry.LogLevel.ERROR)) }
    var selectedCategories by remember { mutableStateOf(LogCategory.entries.toSet()) }
    var autoScroll by remember { mutableStateOf(true) }
    val listState = rememberLazyListState()

    // 过滤日志
    val filteredLogs = remember(logEntries, selectedLevels, selectedCategories) {
        logEntries.filter { entry ->
            entry.level in selectedLevels && entry.category in selectedCategories
        }
    }

    // 自动滚动到最新
    LaunchedEffect(filteredLogs.size) {
        if (autoScroll && filteredLogs.isNotEmpty()) {
            listState.animateScrollToItem(filteredLogs.size - 1)
        }
    }

    Column(modifier = Modifier.fillMaxSize()) {
        // 过滤栏
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 4.dp)
        ) {
            // 日志级别过滤
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                LogEntry.LogLevel.entries.forEach { level ->
                    val isSelected = level in selectedLevels
                    val levelColor = getLogLevelColor(level)
                    FilterChip(
                        selected = isSelected,
                        onClick = {
                            selectedLevels = if (isSelected) {
                                selectedLevels - level
                            } else {
                                selectedLevels + level
                            }
                        },
                        label = {
                            Text(level.name.take(1), fontSize = 10.sp, fontWeight = FontWeight.Bold)
                        },
                        modifier = Modifier.height(28.dp)
                    )
                }
                Spacer(modifier = Modifier.weight(1f))
                // 自动滚动开关
                IconButton(
                    onClick = { autoScroll = !autoScroll },
                    modifier = Modifier.height(28.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.AutoScroll,
                        contentDescription = "自动滚动",
                        tint = if (autoScroll) Color(0xFF2196F3) else Color.Gray,
                        modifier = Modifier.padding(4.dp)
                    )
                }
                // 清除按钮
                IconButton(
                    onClick = onClear,
                    modifier = Modifier.height(28.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.ClearAll,
                        contentDescription = "清除日志",
                        modifier = Modifier.padding(4.dp)
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            // 日志类别过滤
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                LogCategory.entries.forEach { category ->
                    val isSelected = category in selectedCategories
                    FilterChip(
                        selected = isSelected,
                        onClick = {
                            selectedCategories = if (isSelected) {
                                selectedCategories - category
                            } else {
                                selectedCategories + category
                            }
                        },
                        label = {
                            Text(category.displayName, fontSize = 10.sp)
                        },
                        modifier = Modifier.height(28.dp)
                    )
                }
            }
        }

        HorizontalDivider()

        // 日志列表
        if (filteredLogs.isEmpty()) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = if (logEntries.isEmpty()) "暂无日志" else "没有匹配的日志",
                    fontSize = 13.sp,
                    color = textColor.copy(alpha = 0.5f)
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                state = listState,
                contentPadding = androidx.compose.foundation.layout.PaddingValues(
                    horizontal = 8.dp, vertical = 4.dp
                )
            ) {
                items(filteredLogs) { entry ->
                    LogEntryRow(entry)
                }
            }
        }

        // 底部状态栏
        HorizontalDivider()
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = "共 ${filteredLogs.size} 条 / 总 ${logEntries.size} 条",
                fontSize = 10.sp,
                color = textColor.copy(alpha = 0.5f)
            )
            Text(
                text = if (autoScroll) "自动滚动: 开" else "自动滚动: 关",
                fontSize = 10.sp,
                color = textColor.copy(alpha = 0.5f)
            )
        }
    }
}

/**
 * 单条日志行
 */
@Composable
fun LogEntryRow(entry: LogEntry) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    val levelColor = getLogLevelColor(entry.level)
    val timeFormat = remember { SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()) }
    val timeStr = timeFormat.format(Date(entry.timestamp))

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 1.dp),
        verticalAlignment = Alignment.Top
    ) {
        // 时间戳
        Text(
            text = timeStr,
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.4f),
            modifier = Modifier.width(72.dp)
        )

        // 级别标签
        Surface(
            shape = RoundedCornerShape(2.dp),
            color = levelColor.copy(alpha = 0.15f),
            modifier = Modifier.padding(end = 4.dp)
        ) {
            Text(
                text = entry.level.name.take(1),
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold,
                color = levelColor,
                modifier = Modifier.padding(horizontal = 3.dp, vertical = 1.dp)
            )
        }

        // 类别
        Text(
            text = "[${entry.category.displayName}]",
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.5f),
            modifier = Modifier.width(36.dp)
        )

        // 消息内容
        Text(
            text = entry.message,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor,
            modifier = Modifier.weight(1f)
        )
    }
}

/**
 * 获取日志级别对应的颜色
 */
@Composable
fun getLogLevelColor(level: LogEntry.LogLevel): Color {
    return when (level) {
        LogEntry.LogLevel.VERBOSE -> Color(0xFF9E9E9E)
        LogEntry.LogLevel.DEBUG -> Color(0xFF2196F3)
        LogEntry.LogLevel.INFO -> Color(0xFF4CAF50)
        LogEntry.LogLevel.WARN -> Color(0xFFFF9800)
        LogEntry.LogLevel.ERROR -> Color(0xFFF44336)
    }
}
