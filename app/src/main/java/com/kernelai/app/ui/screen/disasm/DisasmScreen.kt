package com.kernelai.app.ui.screen.disasm

import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.NavigateNext
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kernelai.app.ui.LocalUiMode
import com.kernelai.app.ui.UiMode
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 反汇编查看器 - 显示原始字节数据，用于外部反汇编分析
 */
@Composable
fun DisasmScreen() {
    val uiMode = LocalUiMode.current
    val surfaceColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surface
    }
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    var addressInput by remember { mutableStateOf("0x7F40001200") }
    var pidInput by remember { mutableStateOf("2048") }
    var moduleSelector by remember { mutableStateOf("app.odex") }
    var disasmData by remember { mutableStateOf(generateMockDisasmData()) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp)
    ) {
        // 进程和模块选择
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // PID
            Text(text = "PID:", fontSize = 13.sp, fontWeight = FontWeight.Medium)
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = surfaceColor,
                modifier = Modifier.width(80.dp)
            ) {
                BasicTextField(
                    value = pidInput,
                    onValueChange = { pidInput = it.filter { c -> c.isDigit() } },
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                    textStyle = androidx.compose.ui.text.TextStyle(
                        fontSize = 13.sp,
                        fontFamily = FontFamily.Monospace,
                        color = textColor
                    ),
                    cursorBrush = SolidColor(textColor),
                    singleLine = true
                )
            }

            // 模块选择
            Text(text = "模块:", fontSize = 13.sp, fontWeight = FontWeight.Medium)
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = surfaceColor,
                modifier = Modifier.weight(1f)
            ) {
                BasicTextField(
                    value = moduleSelector,
                    onValueChange = { moduleSelector = it },
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                    textStyle = androidx.compose.ui.text.TextStyle(
                        fontSize = 13.sp,
                        color = textColor
                    ),
                    cursorBrush = SolidColor(textColor),
                    singleLine = true
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // 地址栏
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = surfaceColor,
                modifier = Modifier.weight(1f)
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Rounded.NavigateNext,
                        contentDescription = null,
                        modifier = Modifier.padding(end = 4.dp)
                    )
                    Box(modifier = Modifier.weight(1f)) {
                        if (addressInput.isEmpty()) {
                            Text(
                                text = "跳转到地址...",
                                fontSize = 13.sp,
                                color = textColor.copy(alpha = 0.4f)
                            )
                        }
                        BasicTextField(
                            value = addressInput,
                            onValueChange = { addressInput = it },
                            modifier = Modifier.fillMaxWidth(),
                            textStyle = androidx.compose.ui.text.TextStyle(
                                fontSize = 13.sp,
                                fontFamily = FontFamily.Monospace,
                                color = textColor
                            ),
                            cursorBrush = SolidColor(textColor),
                            singleLine = true
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.width(8.dp))

            Button(
                onClick = { disasmData = generateMockDisasmData() },
                modifier = Modifier.height(38.dp)
            ) {
                Text("读取", fontSize = 12.sp)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))
        HorizontalDivider()
        Spacer(modifier = Modifier.height(4.dp))

        // 列标题
        val scrollState = rememberScrollState()
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(scrollState)
                .padding(horizontal = 4.dp, vertical = 2.dp)
        ) {
            Text(
                text = "Address",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f),
                modifier = Modifier.width(100.dp)
            )
            Text(
                text = "Bytes",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f),
                modifier = Modifier.width(120.dp)
            )
            Text(
                text = "Offset",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f)
            )
        }

        HorizontalDivider()

        // 原始字节数据行
        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
        ) {
            items(disasmData) { line ->
                DisasmRow(line = line, scrollState = scrollState)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))
        HorizontalDivider()

        // 底部信息
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = "共 ${disasmData.size} 条指令",
                fontSize = 11.sp,
                color = textColor.copy(alpha = 0.5f)
            )
            Text(
                text = "ARM64 (AArch64)",
                fontSize = 11.sp,
                color = textColor.copy(alpha = 0.5f)
            )
        }
    }
}

/**
 * 反汇编数据行
 */
@Composable
fun DisasmRow(line: DisasmLine, scrollState: androidx.compose.foundation.ScrollState) {
    val uiMode = LocalUiMode.current
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(scrollState)
            .padding(horizontal = 4.dp, vertical = 1.dp)
    ) {
        // 地址列
        Text(
            text = line.address,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.7f),
            modifier = Modifier.width(100.dp)
        )

        // 字节列
        Text(
            text = line.bytes,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor,
            modifier = Modifier.width(120.dp)
        )

        // 偏移列
        Text(
            text = line.offset,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.5f)
        )
    }
}

/**
 * 反汇编行数据模型
 */
data class DisasmLine(
    val address: String,
    val bytes: String,
    val offset: String
)

/**
 * 生成模拟反汇编数据（ARM64 指令字节）
 */
fun generateMockDisasmData(): List<DisasmLine> {
    val baseAddr = 0x7F40001200L
    // 模拟 ARM64 指令（每条 4 字节）
    val mockInstructions = listOf(
        "FD7BBFA9" to "STP X29, X30, [SP, #-0x10]!",
        "FD030091" to "MOV X29, SP",
        "E80300AA" to "MOV X8, X0",
        "00000094" to "BL sub_XXXX",
        "E80700F9" to "STR X8, [SP, #0x8]",
        "08008052" to "MOV W8, #0x0",
        "E80F00F9" to "STR X8, [SP, #0x18]",
        "1F2003D5" to "NOP",
        "FD7BC1A8" to "LDP X29, X30, [SP], #0x10",
        "C0035FD6" to "RET",
        "E00300AA" to "MOV X0, X0",
        "01000014" to "B #0x4",
        "FF8300D1" to "SUB SP, SP, #0x20",
        "E80700F9" to "STR X8, [SP, #0x8]",
        "00000094" to "BL sub_YYYY",
        "FF030091" to "ADD SP, SP, #0x20",
    )

    return mockInstructions.mapIndexed { index, (bytes, _) ->
        val addr = baseAddr + index * 4
        DisasmLine(
            address = "0x%010X".format(addr),
            bytes = bytes.chunked(2).joinToString(" "),
            offset = "+0x%04X".format(index * 4)
        )
    }
}
