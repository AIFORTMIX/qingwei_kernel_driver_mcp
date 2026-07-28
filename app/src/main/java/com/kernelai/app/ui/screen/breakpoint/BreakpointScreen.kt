package com.kernelai.app.ui.screen.breakpoint

import androidx.compose.foundation.background
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
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberSwipeToDismissBoxState
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
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.kernelai.app.data.model.BreakpointInfo
import com.kernelai.app.data.model.BreakpointInfo.BreakpointType
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import com.kernelai.app.ui.viewmodel.BreakpointViewModel
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 断点管理器 - 显示/管理断点列表，支持添加/删除/查看寄存器快照
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BreakpointScreen(
    viewModel: BreakpointViewModel = viewModel()
) {
    val breakpoints by viewModel.breakpoints.collectAsStateWithLifecycle()
    val isLoading by viewModel.isLoading.collectAsStateWithLifecycle()
    val selectedBreakpoint by viewModel.selectedBreakpoint.collectAsStateWithLifecycle()
    val registerSnapshot by viewModel.registerSnapshot.collectAsStateWithLifecycle()
    val showAddDialog by viewModel.showAddDialog.collectAsStateWithLifecycle()

    val uiMode = LocalUiMode.current

    Box(modifier = Modifier.fillMaxSize()) {
        Column(modifier = Modifier.fillMaxSize()) {
            // 断点列表
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(
                    horizontal = 12.dp, vertical = 8.dp
                ),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                if (breakpoints.isEmpty() && !isLoading) {
                    item {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(32.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(text = "暂无断点，点击 + 添加")
                        }
                    }
                }

                items(breakpoints, key = { it.bpIndex }) { bp ->
                    val dismissState = rememberSwipeToDismissBoxState(
                        confirmValueChange = { value ->
                            if (value == SwipeToDismissBoxValue.EndToStart) {
                                viewModel.removeBreakpoint(bp)
                                true
                            } else false
                        }
                    )

                    SwipeToDismissBox(
                        state = dismissState,
                        backgroundContent = {
                            // 滑动删除背景
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = Color(0xFFF44336),
                                modifier = Modifier.fillMaxSize()
                            ) {
                                Box(
                                    modifier = Modifier
                                        .fillMaxSize()
                                        .padding(end = 20.dp),
                                    contentAlignment = Alignment.CenterEnd
                                ) {
                                    Icon(
                                        imageVector = Icons.Rounded.Delete,
                                        contentDescription = "删除",
                                        tint = Color.White
                                    )
                                }
                            }
                        },
                        enableDismissFromStartToEnd = false
                    ) {
                        BreakpointItem(
                            breakpoint = bp,
                            onClick = { viewModel.selectBreakpoint(bp) },
                            onToggle = { viewModel.toggleBreakpoint(bp) }
                        )
                    }
                }

                // 寄存器快照区域
                if (selectedBreakpoint != null && registerSnapshot != null) {
                    item {
                        Spacer(modifier = Modifier.height(8.dp))
                        RegisterSnapshotView(
                            breakpoint = selectedBreakpoint!!,
                            snapshot = registerSnapshot!!,
                            onClose = { viewModel.clearSelection() }
                        )
                    }
                }
            }
        }

        // 添加断点 FAB
        FloatingActionButton(
            onClick = { viewModel.showAddDialog() },
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(16.dp),
            containerColor = Color(0xFF2196F3),
            contentColor = Color.White
        ) {
            Icon(imageVector = Icons.Rounded.Add, contentDescription = "添加断点")
        }
    }

    // 添加断点对话框
    if (showAddDialog) {
        AddBreakpointDialog(
            onDismiss = { viewModel.hideAddDialog() },
            onConfirm = { address, type, pid ->
                viewModel.addBreakpoint(address, type, pid)
            }
        )
    }
}

/**
 * 断点列表项
 */
@Composable
fun BreakpointItem(
    breakpoint: BreakpointInfo,
    onClick: () -> Unit,
    onToggle: () -> Unit
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
    val statusColor = if (breakpoint.isActive) Color(0xFF4CAF50) else Color(0xFF9E9E9E)

    Surface(
        shape = RoundedCornerShape(12.dp),
        color = cardColor,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // 激活状态指示器
            Checkbox(
                checked = breakpoint.isActive,
                onCheckedChange = { onToggle() }
            )

            Spacer(modifier = Modifier.width(8.dp))

            // 断点信息
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    // 类型标签
                    Surface(
                        shape = RoundedCornerShape(4.dp),
                        color = statusColor.copy(alpha = 0.15f)
                    ) {
                        Text(
                            text = breakpoint.type.displayName,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            color = statusColor,
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "#${breakpoint.bpIndex}",
                        fontSize = 12.sp,
                        color = textColor.copy(alpha = 0.5f)
                    )
                }

                Spacer(modifier = Modifier.height(4.dp))

                Text(
                    text = breakpoint.addressHex,
                    fontSize = 14.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Medium
                )

                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.padding(top = 2.dp)
                ) {
                    Text(
                        text = "长度: ${breakpoint.length}",
                        fontSize = 11.sp,
                        color = textColor.copy(alpha = 0.5f)
                    )
                    Text(
                        text = "命中: ${breakpoint.hitCount}",
                        fontSize = 11.sp,
                        color = if (breakpoint.hitCount > 0) Color(0xFFFF9800) else textColor.copy(alpha = 0.5f)
                    )
                }
            }

            // 点击查看详情
            IconButton(onClick = onClick) {
                Icon(
                    imageVector = Icons.Rounded.Memory,
                    contentDescription = "查看寄存器",
                    tint = textColor.copy(alpha = 0.5f)
                )
            }
        }
    }
}

/**
 * 寄存器快照视图
 */
@Composable
fun RegisterSnapshotView(
    breakpoint: BreakpointInfo,
    snapshot: BreakpointViewModel.RegisterSnapshot,
    onClose: () -> Unit
) {
    val uiMode = LocalUiMode.current
    val cardColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surfaceVariant
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant
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
            .padding(horizontal = 4.dp)
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "寄存器快照 - ${breakpoint.addressHex}",
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )
                TextButton(onClick = onClose) {
                    Text("关闭", fontSize = 12.sp)
                }
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            // 寄存器网格
            val regValues = (0..30).map { snapshot.getRegisterValue(it) } +
                listOf(snapshot.sp, snapshot.pc, snapshot.pstate)
            val regNames = BreakpointViewModel.RegisterSnapshot.REGISTER_NAMES

            regNames.chunked(2).forEach { pair ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    pair.forEachIndexed { idx, name ->
                        val regIdx = regNames.indexOf(name)
                        val value = regValues[regIdx]
                        Row(
                            modifier = Modifier.weight(1f),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "$name:",
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Medium,
                                color = textColor.copy(alpha = 0.6f),
                                modifier = Modifier.width(40.dp)
                            )
                            Text(
                                text = "0x${value.toString(16).uppercase().padStart(16, '0')}",
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * 添加断点对话框
 */
@Composable
fun AddBreakpointDialog(
    onDismiss: () -> Unit,
    onConfirm: (address: Long, type: BreakpointType, pid: Int) -> Unit
) {
    var addressText by remember { mutableStateOf("") }
    var selectedType by remember { mutableStateOf(BreakpointType.HW_EXECUTE) }
    var pidText by remember { mutableStateOf("") }
    var typeMenuExpanded by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("添加断点") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = addressText,
                    onValueChange = { addressText = it },
                    label = { Text("地址 (十六进制)") },
                    placeholder = { Text("0x7F40001234") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                // 断点类型选择
                Box {
                    OutlinedTextField(
                        value = selectedType.displayName,
                        onValueChange = {},
                        label = { Text("断点类型") },
                        readOnly = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    DropdownMenu(
                        expanded = typeMenuExpanded,
                        onDismissRequest = { typeMenuExpanded = false }
                    ) {
                        BreakpointType.entries.forEach { type ->
                            DropdownMenuItem(
                                text = { Text(type.displayName) },
                                onClick = {
                                    selectedType = type
                                    typeMenuExpanded = false
                                }
                            )
                        }
                    }
                }

                OutlinedTextField(
                    value = pidText,
                    onValueChange = { pidText = it.filter { c -> c.isDigit() } },
                    label = { Text("PID") },
                    placeholder = { Text("进程 ID") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    val address = addressText.removePrefix("0x").removePrefix("0X").toLongOrNull(16)
                    val pid = pidText.toIntOrNull() ?: 0
                    if (address != null) {
                        onConfirm(address, selectedType, pid)
                    }
                },
                enabled = addressText.isNotEmpty()
            ) {
                Text("添加")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("取消")
            }
        }
    )
}
