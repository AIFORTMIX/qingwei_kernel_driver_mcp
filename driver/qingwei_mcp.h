/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_MCP_H
#define QINGWEI_MCP_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define QINGWEI_DEVICE_NAME    "qingwei_mcp"
#define QW_MODULE_HIDE        "vfat"
#define QINGWEI_IOCTL_MAGIC    'M'

/* ===== 原有数据结构（向后兼容） ===== */

typedef struct {
    int pid;
    char pkg_name[64];
    unsigned long addr;
    size_t size;
    unsigned long offsets[8];
    int offset_count;
    unsigned long user_buf;
} mem_packet_t;

typedef struct {
    unsigned long addr;
    __u32 size;
    __u32 out_offset;
    __s32 status;
} mem_batch_item_t;

typedef struct {
    __u32 pid;
    char pkg_name[64];
    __u32 count;
    __u32 item_size;
    unsigned long items_buf;
    unsigned long out_buf;
    size_t out_size;
} mem_batch_packet_t;

typedef struct {
    __u64 cpu_time_ns;
    __u64 call_count;
    unsigned long mem_bytes;
} module_info_t;

typedef struct {
    __u64 hit_count;
    __u64 bp_addr;
    int snapshot_count;
    int active;
} hwbp_stats_t;

/* ===== 原有 ioctl 命令（1-10，向后兼容） ===== */

#define CMD_GET_BASE            _IOWR(QINGWEI_IOCTL_MAGIC, 1, mem_packet_t)
#define CMD_READ_MEM            _IOWR(QINGWEI_IOCTL_MAGIC, 2, mem_packet_t)
#define CMD_WRITE_MEM           _IOWR(QINGWEI_IOCTL_MAGIC, 3, mem_packet_t)
#define CMD_READ_PTR            _IOWR(QINGWEI_IOCTL_MAGIC, 4, mem_packet_t)
#define CMD_READ_BATCH          _IOWR(QINGWEI_IOCTL_MAGIC, 5, mem_batch_packet_t)
#define CMD_GET_MODULE_INFO     _IOR(QINGWEI_IOCTL_MAGIC, 6, module_info_t)
#define CMD_SET_HW_BP           _IOW(QINGWEI_IOCTL_MAGIC, 7, mem_packet_t)
#define CMD_SET_BLR_ADDRS       _IOW(QINGWEI_IOCTL_MAGIC, 8, mem_packet_t)
#define CMD_QUERY_SNAPSHOT      _IOWR(QINGWEI_IOCTL_MAGIC, 9, mem_packet_t)
#define CMD_GET_HWBP_STATS      _IOR(QINGWEI_IOCTL_MAGIC, 10, hwbp_stats_t)

/* ===== 新增数据结构 ===== */

/* 进程信息（用于进程枚举） */
typedef struct {
    __s32 pid;
    __s32 ppid;
    __s32 tgid;
    char comm[16];
    char cmdline[256];
    unsigned long state;
    __s32 thread_count;
} kai_proc_info_t;

/* 进程列表请求/响应 */
typedef struct {
    __u32 max_count;
    __u32 actual_count;
    __u32 total_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_proc_list_packet_t;

/* 模块/库信息 */
typedef struct {
    unsigned long base_addr;
    unsigned long end_addr;
    unsigned long size;
    __u32 flags;
    unsigned long offset;
    char path[256];
    __u8 is_executable;
} kai_module_info_t;

/* 模块列表请求/响应 */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 max_count;
    __u32 actual_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_module_list_packet_t;

/* VMA/内存映射条目 */
typedef struct {
    unsigned long start;
    unsigned long end;
    __u32 flags;
    unsigned long pgoff;
    char name[128];
} kai_vma_entry_t;

/* VMA 列表请求/响应 */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 max_count;
    __u32 actual_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_vma_list_packet_t;

/* 寄存器读取 */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long bp_id;
    __u64 regs[31];         /* X0-X30 */
    unsigned long sp;
    unsigned long pc;
    unsigned long pstate;
    __u32 valid_mask;
} kai_reg_read_packet_t;

/* 多硬件断点管理 */
#define QW_MAX_HWBP 16

typedef struct {
    __u32 bp_index;
    unsigned long bp_addr;
    __u32 bp_type;        /* 0=exec, 1=read, 2=write, 3=access */
    __u32 bp_len;         /* 1/2/4/8 bytes */
    __s32 pid;
    char pkg_name[64];
} kai_hwbp_setup_t;

typedef struct {
    __u32 bp_index;
    __s32 pid;
    char pkg_name[64];
} kai_hwbp_remove_t;

typedef struct {
    __u32 bp_index;
    unsigned long bp_addr;
    __u32 bp_type;
    __u32 bp_len;
    __u64 hit_count;
    __u32 active;
} kai_hwbp_info_t;

typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 count;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_hwbp_list_packet_t;

/* 软件断点（kprobes） */
typedef struct {
    unsigned long addr;
    __s32 pid;
    char pkg_name[64];
    __u32 sw_bp_id;
} kai_swbp_setup_t;

typedef struct {
    __u32 sw_bp_id;
    __s32 pid;
    char pkg_name[64];
} kai_swbp_remove_t;

/* 内存搜索/模式扫描 */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long start_addr;
    unsigned long end_addr;
    __u8 pattern[256];
    __u8 mask[256];
    __u32 pattern_len;
    __u32 max_results;
    __u32 found_count;
    unsigned long results_buf;
    size_t results_buf_size;
} kai_mem_search_packet_t;

/* 调用栈捕获 */
typedef struct {
    unsigned long pc;
    unsigned long sp;
    unsigned long fp;
    unsigned long lr;
    char symbol[64];
} kai_stack_frame_t;

typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long bp_id;
    __u32 max_frames;
    __u32 frame_count;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_callstack_packet_t;

/* 线程枚举 */
typedef struct {
    __s32 tid;
    char comm[16];
    unsigned long state;
    unsigned long stack_ptr;
    unsigned long pc;
} kai_thread_info_t;

typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 max_count;
    __u32 actual_count;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_thread_list_packet_t;

/* 原始字节读取 */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long addr;
    __u32 size;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_raw_read_packet_t;

/* 断点使用状态查询 */
typedef struct {
    __u32 total_hw_slots;      /* 硬件支持的总断点数 */
    __u32 used_hw_slots;       /* 已使用的硬件断点数 */
    __u32 total_sw_slots;      /* 软件断点无限制，返回 0 */
    __u32 used_sw_slots;       /* 当前使用的软件断点数 */
    __u32 hw_bp_available;     /* 可用硬件断点槽位数 */
} qingwei_mcp_bp_usage_t;

/* ===== 新增 ioctl 命令（11-24） ===== */

#define CMD_LIST_PROCS          _IOWR(QINGWEI_IOCTL_MAGIC, 11, kai_proc_list_packet_t)
#define CMD_LIST_MODULES        _IOWR(QINGWEI_IOCTL_MAGIC, 12, kai_module_list_packet_t)
#define CMD_LIST_VMAS           _IOWR(QINGWEI_IOCTL_MAGIC, 13, kai_vma_list_packet_t)
#define CMD_READ_REGS           _IOWR(QINGWEI_IOCTL_MAGIC, 14, kai_reg_read_packet_t)
#define CMD_HWBP_SET            _IOW(QINGWEI_IOCTL_MAGIC, 15, kai_hwbp_setup_t)
#define CMD_HWBP_REMOVE         _IOW(QINGWEI_IOCTL_MAGIC, 16, kai_hwbp_remove_t)
#define CMD_HWBP_LIST           _IOWR(QINGWEI_IOCTL_MAGIC, 17, kai_hwbp_list_packet_t)
#define CMD_SWBP_SET            _IOWR(QINGWEI_IOCTL_MAGIC, 18, kai_swbp_setup_t)
#define CMD_SWBP_REMOVE         _IOW(QINGWEI_IOCTL_MAGIC, 19, kai_swbp_remove_t)
#define CMD_MEM_SEARCH          _IOWR(QINGWEI_IOCTL_MAGIC, 20, kai_mem_search_packet_t)
#define CMD_CALLSTACK           _IOWR(QINGWEI_IOCTL_MAGIC, 21, kai_callstack_packet_t)
#define CMD_LIST_THREADS        _IOWR(QINGWEI_IOCTL_MAGIC, 22, kai_thread_list_packet_t)
#define CMD_RAW_READ            _IOWR(QINGWEI_IOCTL_MAGIC, 23, kai_raw_read_packet_t)
#define CMD_GET_BP_USAGE        _IOR(QINGWEI_IOCTL_MAGIC, 24, qingwei_mcp_bp_usage_t)

/* ===== 常量 ===== */

#define QW_BATCH_MAX_ITEMS    512
#define QW_BATCH_MAX_SIZE     (2 * 1024 * 1024)
#define QW_MAX_SEARCH_RANGE   (64 * 1024 * 1024)
#define QW_SNAPSHOT_CACHE_SIZE 256

/* 进程查找匹配模式 */
enum kai_match_mode {
    KAI_MATCH_EXACT = 0,
    KAI_MATCH_SUBSTRING = 1,
    KAI_MATCH_PREFIX = 2,
};

#endif /* QINGWEI_MCP_H */
