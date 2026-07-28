package com.kernelai.app.ui.screen.process

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Clear
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Thread
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.kernelai.app.data.model.ProcessInfo
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import com.kernelai.app.ui.viewmodel.ProcessViewModel
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 进程浏览器 - 显示运行中的进程列表，支持搜索/过滤、展开详情
 */
@Composable
fun ProcessScreen(
    viewModel: ProcessViewModel = viewModel()
) {
    val filteredProcesses by viewModel.filteredProcesses.collectAsStateWithLifecycle()
    val searchQuery by viewModel.searchQuery.collectAsStateWithLifecycle()
    val isLoading by viewModel.isLoading.collectAsStateWithLifecycle()
    val expandedPid by viewModel.expandedPid.collectAsStateWithLifecycle()
    val processDetails by viewModel.processDetails.collectAsStateWithLifecycle()

    val uiMode = LocalUiMode.current
    val surfaceColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surface
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(top = 8.dp)
    ) {
        // 搜索栏
        ProcessSearchBar(
            query = searchQuery,
            onQueryChange = viewModel::updateSearchQuery,
            onClear = { viewModel.updateSearchQuery("") }
        )

        // 进程列表
        PullToRefreshBox(
            isRefreshing = isLoading,
            onRefresh = { viewModel.refresh() },
            modifier = Modifier.fillMaxSize()
        ) {
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(
                    horizontal = 12.dp,
                    vertical = 8.dp
                ),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                items(filteredProcesses, key = { it.pid }) { process ->
                    ProcessItem(
                        process = process,
                        isExpanded = expandedPid == process.pid,
                        details = processDetails[process.pid],
                        onClick = { viewModel.toggleExpand(process.pid) }
                    )
                }

                if (filteredProcesses.isEmpty() && !isLoading) {
                    item {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(32.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(text = "没有找到匹配的进程")
                        }
                    }
                }
            }
        }
    }
}

/**
 * 搜索栏
 */
@Composable
fun ProcessSearchBar(
    query: String,
    onQueryChange: (String) -> Unit,
    onClear: () -> Unit
) {
    val uiMode = LocalUiMode.current
    val bgColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant
    }
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    Surface(
        shape = RoundedCornerShape(12.dp),
        color = bgColor,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Rounded.Search,
                contentDescription = null,
                modifier = Modifier.size(20.dp)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Box(modifier = Modifier.weight(1f)) {
                if (query.isEmpty()) {
                    Text(
                        text = "搜索进程名、包名或 PID...",
                        fontSize = 14.sp,
                        color = textColor.copy(alpha = 0.5f)
                    )
                }
                BasicTextField(
                    value = query,
                    onValueChange = onQueryChange,
                    modifier = Modifier.fillMaxWidth(),
                    textStyle = androidx.compose.ui.text.TextStyle(
                        color = textColor,
                        fontSize = 14.sp
                    ),
                    cursorBrush = SolidColor(textColor),
                    singleLine = true
                )
            }
            if (query.isNotEmpty()) {
                IconButton(onClick = onClear, modifier = Modifier.size(24.dp)) {
                    Icon(
                        imageVector = Icons.Rounded.Clear,
                        contentDescription = "清除",
                        modifier = Modifier.size(16.dp)
                    )
                }
            }
        }
    }
}

/**
 * 进程列表项
 */
@Composable
fun ProcessItem(
    process: ProcessInfo,
    isExpanded: Boolean,
    details: ProcessViewModel.ProcessDetails?,
    onClick: () -> Unit
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
        shape = RoundedCornerShape(12.dp),
        color = cardColor,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            // 主行
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // 进程图标
                Surface(
                    shape = RoundedCornerShape(8.dp),
                    color = textColor.copy(alpha = 0.1f),
                    modifier = Modifier.size(40.dp)
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            imageVector = Icons.Rounded.Memory,
                            contentDescription = null,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }

                Spacer(modifier = Modifier.width(12.dp))

                // 进程信息
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = process.displayName,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        modifier = Modifier.padding(top = 2.dp)
                    ) {
                        Text(
                            text = "PID: ${process.pid}",
                            fontSize = 12.sp,
                            color = textColor.copy(alpha = 0.6f)
                        )
                        Text(
                            text = process.stateDescription,
                            fontSize = 12.sp,
                            color = textColor.copy(alpha = 0.6f)
                        )
                        Text(
                            text = "${process.threadCount} 线程",
                            fontSize = 12.sp,
                            color = textColor.copy(alpha = 0.6f)
                        )
                    }
                }

                // 展开/折叠图标
                Icon(
                    imageVector = if (isExpanded) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = if (isExpanded) "折叠" else "展开"
                )
            }

            // 展开的详情区域
            AnimatedVisibility(
                visible = isExpanded && details != null,
                enter = expandVertically(),
                exit = shrinkVertically()
            ) {
                if (details != null) {
                    Column(modifier = Modifier.padding(top = 12.dp)) {
                        HorizontalDivider(modifier = Modifier.padding(bottom = 8.dp))

                        // 模块列表
                        if (details.modules.isNotEmpty()) {
                            Text(
                                text = "加载模块 (${details.modules.size})",
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(bottom = 4.dp)
                            )
                            details.modules.take(5).forEach { module ->
                                Row(
                                    modifier = Modifier.padding(start = 8.dp, top = 2.dp, bottom = 2.dp)
                                ) {
                                    Text(
                                        text = module.name,
                                        fontSize = 11.sp,
                                        fontFamily = FontFamily.Monospace,
                                        color = textColor.copy(alpha = 0.7f),
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        modifier = Modifier.weight(1f)
                                    )
                                    Text(
                                        text = module.baseAddress,
                                        fontSize = 11.sp,
                                        fontFamily = FontFamily.Monospace,
                                        color = textColor.copy(alpha = 0.5f)
                                    )
                                }
                            }
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        // 线程列表
                        if (details.threads.isNotEmpty()) {
                            Text(
                                text = "线程 (${details.threads.size})",
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(bottom = 4.dp)
                            )
                            details.threads.take(5).forEach { thread ->
                                Row(
                                    modifier = Modifier.padding(start = 8.dp, top = 2.dp, bottom = 2.dp)
                                ) {
                                    Text(
                                        text = "${thread.tid}",
                                        fontSize = 11.sp,
                                        fontFamily = FontFamily.Monospace,
                                        modifier = Modifier.width(40.dp),
                                        color = textColor.copy(alpha = 0.5f)
                                    )
                                    Text(
                                        text = thread.name,
                                        fontSize = 11.sp,
                                        color = textColor.copy(alpha = 0.7f),
                                        modifier = Modifier.weight(1f)
                                    )
                                    Text(
                                        text = thread.state,
                                        fontSize = 11.sp,
                                        color = textColor.copy(alpha = 0.5f)
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // 加载中指示器
            if (isExpanded && details == null) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                }
            }
        }
    }
}
