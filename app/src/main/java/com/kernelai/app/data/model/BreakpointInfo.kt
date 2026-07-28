package com.kernelai.app.data.model

data class BreakpointInfo(
    val bpIndex: Int,
    val address: Long,
    val type: BreakpointType,
    val length: Int,
    val hitCount: Long,
    val isActive: Boolean
) {
    enum class BreakpointType(val value: Int, val displayName: String) {
        HW_EXECUTE(0, "HW Execute"),
        HW_READ(1, "HW Read"),
        HW_WRITE(2, "HW Write"),
        HW_ACCESS(3, "HW Access"),
        SW(10, "Software");
        
        companion object {
            fun fromValue(value: Int): BreakpointType {
                return entries.find { it.value == value } ?: HW_EXECUTE
            }
        }
    }
    
    val addressHex: String
        get() = "0x${address.toString(16).uppercase().padStart(8, '0')}"
}
