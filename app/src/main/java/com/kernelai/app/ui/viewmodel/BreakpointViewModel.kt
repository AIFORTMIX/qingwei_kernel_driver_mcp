package com.kernelai.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kernelai.app.data.model.BreakpointInfo
import com.kernelai.app.data.model.BreakpointInfo.BreakpointType
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * 断点管理器 ViewModel - 管理断点列表、添加/删除断点、监控命中计数
 */
class BreakpointViewModel : ViewModel() {

    private val _breakpoints = MutableStateFlow<List<BreakpointInfo>>(emptyList())
    val breakpoints: StateFlow<List<BreakpointInfo>> = _breakpoints.asStateFlow()

    private val _isLoading = MutableStateFlow(false)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    private val _selectedBreakpoint = MutableStateFlow<BreakpointInfo?>(null)
    val selectedBreakpoint: StateFlow<BreakpointInfo?> = _selectedBreakpoint.asStateFlow()

    private val _registerSnapshot = MutableStateFlow<RegisterSnapshot?>(null)
    val registerSnapshot: StateFlow<RegisterSnapshot?> = _registerSnapshot.asStateFlow()

    private val _showAddDialog = MutableStateFlow(false)
    val showAddDialog: StateFlow<Boolean> = _showAddDialog.asStateFlow()

    init {
        loadBreakpoints()
    }

    /**
     * 加载断点列表
     */
    fun loadBreakpoints() {
        viewModelScope.launch {
            _isLoading.value = true
            try {
                delay(200) // 模拟加载延迟
                // 实际项目中从驱动 CMD_HWBP_LIST / CMD_SWBP_LIST 获取
                _breakpoints.value = listOf(
                    BreakpointInfo(
                        bpIndex = 0,
                        address = 0x7F40001234L,
                        type = BreakpointType.HW_EXECUTE,
                        length = 4,
                        hitCount = 15,
                        isActive = true
                    ),
                    BreakpointInfo(
                        bpIndex = 1,
                        address = 0x7F20000100L,
                        type = BreakpointType.HW_WRITE,
                        length = 8,
                        hitCount = 3,
                        isActive = true
                    ),
                    BreakpointInfo(
                        bpIndex = 2,
                        address = 0x7F10005678L,
                        type = BreakpointType.SW,
                        length = 4,
                        hitCount = 0,
                        isActive = false
                    )
                )
            } finally {
                _isLoading.value = false
            }
        }
    }

    /**
     * 显示添加断点对话框
     */
    fun showAddDialog() {
        _showAddDialog.value = true
    }

    /**
     * 隐藏添加断点对话框
     */
    fun hideAddDialog() {
        _showAddDialog.value = false
    }

    /**
     * 添加断点
     */
    fun addBreakpoint(address: Long, type: BreakpointType, pid: Int, length: Int = 4) {
        viewModelScope.launch {
            val newBp = BreakpointInfo(
                bpIndex = (_breakpoints.value.maxOfOrNull { it.bpIndex } ?: -1) + 1,
                address = address,
                type = type,
                length = length,
                hitCount = 0,
                isActive = true
            )
            _breakpoints.value = _breakpoints.value + newBp
            _showAddDialog.value = false
        }
    }

    /**
     * 删除断点
     */
    fun removeBreakpoint(breakpoint: BreakpointInfo) {
        viewModelScope.launch {
            _breakpoints.value = _breakpoints.value.filter { it.bpIndex != breakpoint.bpIndex }
        }
    }

    /**
     * 切换断点激活状态
     */
    fun toggleBreakpoint(breakpoint: BreakpointInfo) {
        viewModelScope.launch {
            _breakpoints.value = _breakpoints.value.map { bp ->
                if (bp.bpIndex == breakpoint.bpIndex) {
                    bp.copy(isActive = !bp.isActive)
                } else {
                    bp
                }
            }
        }
    }

    /**
     * 选择断点并查看寄存器快照
     */
    fun selectBreakpoint(breakpoint: BreakpointInfo) {
        _selectedBreakpoint.value = breakpoint
        loadRegisterSnapshot(breakpoint.bpIndex)
    }

    /**
     * 加载寄存器快照
     */
    private fun loadRegisterSnapshot(bpIndex: Int) {
        viewModelScope.launch {
            delay(100) // 模拟从驱动 CMD_QUERY_SNAPSHOT 获取
            _registerSnapshot.value = RegisterSnapshot(
                x0 = 0x0L, x1 = 0x1L, x2 = 0x2L, x3 = 0x3L,
                x4 = 0x4L, x5 = 0x5L, x6 = 0x6L, x7 = 0x7L,
                x8 = 0x8L, x9 = 0x9L, x10 = 0xAL, x11 = 0xBL,
                x12 = 0xCL, x13 = 0xDL, x14 = 0xEL, x15 = 0xFL,
                x16 = 0x10L, x17 = 0x11L, x18 = 0x12L, x19 = 0x13L,
                x20 = 0x14L, x21 = 0x15L, x22 = 0x16L, x23 = 0x17L,
                x24 = 0x18L, x25 = 0x19L, x26 = 0x1AL, x27 = 0x1BL,
                x28 = 0x1CL, x29 = 0x7FCE000FF0L, // FP
                x30 = 0x7F40001300L, // LR
                sp = 0x7FCE000000L,
                pc = 0x7F40001234L,
                pstate = 0x60000000L
            )
        }
    }

    /**
     * 清除选择
     */
    fun clearSelection() {
        _selectedBreakpoint.value = null
        _registerSnapshot.value = null
    }

    /**
     * ARM64 寄存器快照
     */
    data class RegisterSnapshot(
        val x0: Long, val x1: Long, val x2: Long, val x3: Long,
        val x4: Long, val x5: Long, val x6: Long, val x7: Long,
        val x8: Long, val x9: Long, val x10: Long, val x11: Long,
        val x12: Long, val x13: Long, val x14: Long, val x15: Long,
        val x16: Long, val x17: Long, val x18: Long, val x19: Long,
        val x20: Long, val x21: Long, val x22: Long, val x23: Long,
        val x24: Long, val x25: Long, val x26: Long, val x27: Long,
        val x28: Long, val x29: Long, val x30: Long,
        val sp: Long, val pc: Long, val pstate: Long
    ) {
        fun getRegisterValue(index: Int): Long = when (index) {
            0 -> x0; 1 -> x1; 2 -> x2; 3 -> x3
            4 -> x4; 5 -> x5; 6 -> x6; 7 -> x7
            8 -> x8; 9 -> x9; 10 -> x10; 11 -> x11
            12 -> x12; 13 -> x13; 14 -> x14; 15 -> x15
            16 -> x16; 17 -> x17; 18 -> x18; 19 -> x19
            20 -> x20; 21 -> x21; 22 -> x22; 23 -> x23
            24 -> x24; 25 -> x25; 26 -> x26; 27 -> x27
            28 -> x28; 29 -> x29; 30 -> x30
            else -> 0L
        }

        companion object {
            val REGISTER_NAMES = (0..30).map { "X$it" } + listOf("SP", "PC", "PSTATE")
        }
    }
}
