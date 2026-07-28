package com.kernelai.app.driver

/**
 * ioctl 命令常量 - 与内核侧 qingwei_mcp.h 对应
 */
object IoctlCommands {
    const val IOCTL_MAGIC = 'M'.code

    // 辅助函数：计算 _IOWR 值
    private fun iowr(type: Int, nr: Int, size: Int): Int {
        return (0xC0000000.toInt() or (size shl 16) or (type shl 8) or nr)
    }
    private fun iow(type: Int, nr: Int, size: Int): Int {
        return (0x40000000 or (size shl 16) or (type shl 8) or nr)
    }
    private fun ior(type: Int, nr: Int, size: Int): Int {
        return (0x80000000.toInt() or (size shl 16) or (type shl 8) or nr)
    }

    // 原有命令（向后兼容）
    val CMD_GET_BASE = iowr(IOCTL_MAGIC, 1, 104)      // mem_packet_t
    val CMD_READ_MEM = iowr(IOCTL_MAGIC, 2, 104)
    val CMD_WRITE_MEM = iowr(IOCTL_MAGIC, 3, 104)
    val CMD_READ_PTR = iowr(IOCTL_MAGIC, 4, 104)
    val CMD_READ_BATCH = iowr(IOCTL_MAGIC, 5, 56)     // mem_batch_packet_t
    val CMD_GET_MODULE_INFO = ior(IOCTL_MAGIC, 6, 24)  // module_info_t
    val CMD_SET_HW_BP = iow(IOCTL_MAGIC, 7, 104)
    val CMD_SET_BLR_ADDRS = iow(IOCTL_MAGIC, 8, 104)
    val CMD_QUERY_SNAPSHOT = iowr(IOCTL_MAGIC, 9, 104)
    val CMD_GET_HWBP_STATS = ior(IOCTL_MAGIC, 10, 32)  // hwbp_stats_t

    // 新增命令
    val CMD_LIST_PROCS = iowr(IOCTL_MAGIC, 11, 24)     // kai_proc_list_packet_t
    val CMD_LIST_MODULES = iowr(IOCTL_MAGIC, 12, 48)   // kai_module_list_packet_t
    val CMD_LIST_VMAS = iowr(IOCTL_MAGIC, 13, 48)      // kai_vma_list_packet_t
    val CMD_READ_REGS = iowr(IOCTL_MAGIC, 14, 360)     // kai_reg_read_packet_t
    val CMD_HWBP_SET = iow(IOCTL_MAGIC, 15, 88)        // kai_hwbp_setup_t
    val CMD_HWBP_REMOVE = iow(IOCTL_MAGIC, 16, 72)     // kai_hwbp_remove_t
    val CMD_HWBP_LIST = iowr(IOCTL_MAGIC, 17, 48)      // kai_hwbp_list_packet_t
    val CMD_SWBP_SET = iowr(IOCTL_MAGIC, 18, 80)       // kai_swbp_setup_t
    val CMD_SWBP_REMOVE = iow(IOCTL_MAGIC, 19, 72)     // kai_swbp_remove_t
    val CMD_MEM_SEARCH = iowr(IOCTL_MAGIC, 20, 600)    // kai_mem_search_packet_t
    val CMD_CALLSTACK = iowr(IOCTL_MAGIC, 21, 56)      // kai_callstack_packet_t
    val CMD_LIST_THREADS = iowr(IOCTL_MAGIC, 22, 48)   // kai_thread_list_packet_t
    val CMD_RAW_READ = iowr(IOCTL_MAGIC, 23, 40)       // kai_raw_read_packet_t
}
