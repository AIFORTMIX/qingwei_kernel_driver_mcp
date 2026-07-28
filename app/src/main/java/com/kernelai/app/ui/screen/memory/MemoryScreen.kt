package com.kernelai.app.ui.screen.memory

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
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.NavigateNext
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
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
 * 内存查看器 - Hex Dump 视图，支持地址跳转、搜索、读写操作
 */
@Composable
fun MemoryScreen() {
    val uiMode = LocalUiMode.current
    val surfaceColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.surface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.surface
    }
    val textColor = when (uiMode) {
        UiMode.Miuix -> MiuixTheme.colorScheme.onSurface
        UiMode.Material -> androidx.compose.material3.MaterialTheme.colorScheme.onSurface
    }

    var addressInput by remember { mutableStateOf("0x7F40001000") }
    var searchPattern by remember { mutableStateOf("") }
    var pidInput by remember { mutableStateOf("2048") }
    var hexDumpData by remember { mutableStateOf(generateMockHexDump()) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp)
    ) {
        // 进程选择器
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(text = "PID:", fontSize = 13.sp, fontWeight = FontWeight.Medium)
            Spacer(modifier = Modifier.width(8.dp))
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = surfaceColor,
                modifier = Modifier.width(120.dp)
            ) {
                BasicTextField(
                    value = pidInput,
                    onValueChange = { pidInput = it.filter { c -> c.isDigit() } },
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                    textStyle = androidx.compose.ui.text.TextStyle(
                        fontSize = 13.sp,
                        fontFamily = FontFamily.Monospace,
                        color = textColor
                    ),
                    cursorBrush = SolidColor(textColor),
                    singleLine = true
                )
            }
            Spacer(modifier = Modifier.weight(1f))
        }

        Spacer(modifier = Modifier.height(8.dp))

        // 地址栏 + 搜索栏
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // 地址输入
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
                                text = "输入地址...",
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

            // 读取按钮
            Button(
                onClick = { hexDumpData = generateMockHexDump() },
                modifier = Modifier.height(38.dp)
            ) {
                Text("读取", fontSize = 12.sp)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // 搜索栏
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
                        imageVector = Icons.Rounded.Search,
                        contentDescription = null,
                        modifier = Modifier.padding(end = 4.dp)
                    )
                    Box(modifier = Modifier.weight(1f)) {
                        if (searchPattern.isEmpty()) {
                            Text(
                                text = "搜索模式 (hex, ?? 通配符)...",
                                fontSize = 13.sp,
                                color = textColor.copy(alpha = 0.4f)
                            )
                        }
                        BasicTextField(
                            value = searchPattern,
                            onValueChange = { searchPattern = it },
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
                onClick = { /* 搜索逻辑 */ },
                modifier = Modifier.height(38.dp)
            ) {
                Text("搜索", fontSize = 12.sp)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))
        HorizontalDivider()
        Spacer(modifier = Modifier.height(4.dp))

        // Hex Dump 表头
        val scrollState = rememberScrollState()
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(scrollState)
                .padding(horizontal = 4.dp, vertical = 2.dp)
        ) {
            Text(
                text = "Address    ",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f),
                modifier = Modifier.width(88.dp)
            )
            Text(
                text = "00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Text(
                text = "ASCII",
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                color = textColor.copy(alpha = 0.6f)
            )
        }

        HorizontalDivider()

        // Hex Dump 数据行
        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
        ) {
            items(hexDumpData) { row ->
                HexDumpRow(row = row, scrollState = scrollState)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))
        HorizontalDivider()

        // 底部操作栏
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = "共 ${hexDumpData.size} 行",
                fontSize = 11.sp,
                color = textColor.copy(alpha = 0.5f)
            )
            Row {
                Button(
                    onClick = { /* 写入操作 */ },
                    modifier = Modifier.height(32.dp)
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Edit,
                        contentDescription = null,
                        modifier = Modifier.padding(end = 4.dp)
                    )
                    Text("写入", fontSize = 11.sp)
                }
            }
        }
    }
}

/**
 * Hex Dump 单行数据
 */
@Composable
fun HexDumpRow(row: HexDumpLine, scrollState: androidx.compose.foundation.ScrollState) {
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
            text = row.address,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.7f),
            modifier = Modifier.width(88.dp)
        )

        // 十六进制字节列
        Text(
            text = row.hexBytes,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor
        )

        Spacer(modifier = Modifier.width(12.dp))

        // ASCII 列
        Text(
            text = row.ascii,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = textColor.copy(alpha = 0.6f)
        )
    }
}

/**
 * Hex Dump 行数据模型
 */
data class HexDumpLine(
    val address: String,
    val hexBytes: String,
    val ascii: String
)

/**
 * 生成模拟 Hex Dump 数据
 */
fun generateMockHexDump(): List<HexDumpLine> {
    val baseAddr = 0x7F40001000L
    return (0 until 32).map { row ->
        val addr = baseAddr + row * 16
        val bytes = (0 until 16).map { col ->
            ((addr + col) and 0xFF).toInt()
        }
        val hexStr = bytes.chunked(8).joinToString("  ") { group ->
            group.joinToString(" ") { "%02X".format(it) }
        }
        val asciiStr = bytes.map { b ->
            if (b in 0x20..0x7E) b.toChar() else '.'
        }.joinToString("")

        HexDumpLine(
            address = "0x%08X".format(addr),
            hexBytes = hexStr,
            ascii = asciiStr
        )
    }
}
