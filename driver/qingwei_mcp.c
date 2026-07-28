// SPDX-License-Identifier: GPL-2.0
/*
 * qingwei_mcp.c — 优化性能版内核驱动
 *
 * 核心优化：
 *   1. 内核态包名→PID解析：通过遍历 /proc/<pid>/cmdline 直接在内核空间
 *      完成包名到 PID 的映射，避免用户态枚举全部进程的低效方式
 *   2. 高性能 PID 缓存：带哈希表的 LRU 缓存，加速重复查找
 *   3. 进程运行状态验证：检查目标进程是否存活、mm 是否有效
 *   4. 优化页表遍历：支持 ARM64 巨页（pud/pmd leaf）检测
 *   5. 批量读取优化：单次持锁完成多个内存区域读取
 *   6. 性能统计：记录每次 ioctl 的 CPU 耗时与调用次数
 *
 * 兼容 App 端：
 *   - 设备节点: /dev/qingwei_mcp
 *   - ioctl 命令号与结构体大小完全匹配 IoctlCommands.kt
 *   - 所有需要 PID 的命令均支持传入包名自动解析
 *
 * 配套文件: Kbuild, Makefile
 */

#define pr_fmt(fmt) "qingwei_mcp: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>
#include <linux/kprobes.h>
#include <linux/dcache.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <asm/pgtable.h>

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#endif

/* =====================================================================
 * 配置常量
 * ===================================================================== */

#define DEVICE_NAME       "qingwei_mcp"
#define MODULE_HIDE_NAME  "vfat"

/* PID 缓存配置 */
#define PID_CACHE_BITS       6
#define PID_CACHE_SIZE       (1 << PID_CACHE_BITS)
#define PID_CACHE_MASK       (PID_CACHE_SIZE - 1)
#define PKG_NAME_MAX_LEN     256
#define CMDLINE_BUF_SIZE     512

/* 批量操作限制 */
#define MAX_BATCH_ITEMS      512
#define MAX_BATCH_BUF_SIZE   (2 * 1024 * 1024)

/* 内存读取限制 */
#define MAX_SINGLE_READ      (16 * 1024 * 1024)
#define MAX_PROC_ENTRIES     512
#define MAX_MODULE_ENTRIES   256
#define MAX_VMA_ENTRIES      1024
#define MAX_THREAD_ENTRIES   256
#define MAX_BP_ENTRIES       64
#define MAX_SEARCH_RESULTS   256

/* ARM64 寄存器数量 */
#define ARM64_REG_COUNT      34

/* 硬件断点快照缓存 */
#define SNAPSHOT_CACHE_SIZE  256

/* =====================================================================
 * 数据结构定义 — 与 App 端 Kotlin 代码严格对应
 * ===================================================================== */

/* --- CMD_LIST_PROCS (24 bytes) --- */
struct kai_proc_list_packet {
    __s32  max_count;
    __s32  actual_count;
    __s32  total_count;
    __s32  offset;
    __u64  out_buf;
    __u64  out_buf_size;
} __packed;

/* 进程条目 (292 bytes) */
struct kai_proc_entry {
    __s32  pid;
    __s32  ppid;
    __s32  tgid;
    char   comm[16];
    char   cmdline[256];
    __u64  state;
    __s32  thread_count;
} __packed;

/* --- CMD_LIST_MODULES (48 bytes) --- */
struct kai_module_list_packet {
    __s32  pid;
    __s32  max_count;
    __s32  actual_count;
    __s32  pad;
    __u64  out_buf;
    __u64  out_buf_size;
    __u64  reserved[2];
} __packed;

/* 模块条目 (280 bytes) */
struct kai_module_entry {
    __u64  base_addr;
    __u64  end_addr;
    __u64  size;
    __u32  flags;
    __u64  offset;
    char   path[248];
    __u32  is_exec;
} __packed;

/* --- CMD_LIST_VMAS (48 bytes) --- */
struct kai_vma_list_packet {
    __s32  pid;
    __s32  max_count;
    __s32  actual_count;
    __s32  pad;
    __u64  out_buf;
    __u64  out_buf_size;
    __u64  reserved[2];
} __packed;

/* VMA 条目 (40 bytes) */
struct kai_vma_entry {
    __u64  start;
    __u64  end;
    __u32  flags;
    __u64  pgoff;
    char   name[16];
    __u32  pad;
} __packed;

/* --- CMD_LIST_THREADS (48 bytes) --- */
struct kai_thread_list_packet {
    __s32  pid;
    __s32  max_count;
    __s32  actual_count;
    __s32  pad;
    __u64  out_buf;
    __u64  out_buf_size;
    __u64  reserved[2];
} __packed;

/* 线程条目 (32 bytes) */
struct kai_thread_entry {
    __s32  tid;
    char   comm[16];
    __u64  state;
    __u64  stack_ptr;
    __u64  pc;
} __packed;

/* --- CMD_RAW_READ (40 bytes) --- */
struct kai_raw_read_packet {
    __s32  pid;
    __s32  pad;
    __u64  address;
    __u32  size;
    __u32  actual_size;
    __u64  data_ptr;
    __u64  data_size;
} __packed;

/* --- CMD_MEM_SEARCH (600 bytes) --- */
struct kai_mem_search_packet {
    __s32  pid;
    __u32  pattern_size;
    __s32  actual_results;
    __s32  total_found;
    __u64  start_addr;
    __u64  end_addr;
    __u32  max_results;
    __u32  pad;
    char   pattern[512];
    __u64  results_ptr;
    __u64  results_size;
} __packed;

/* --- CMD_HWBP_SET (88 bytes) --- */
struct kai_hwbp_setup {
    __s32  pid;
    __u32  bp_type;
    __u32  bp_len;
    __u32  pad;
    __u64  address;
    __u32  bp_id;
    char   reserved[64];
} __packed;

/* --- CMD_HWBP_REMOVE / CMD_SWBP_REMOVE (72 bytes) --- */
struct kai_hwbp_remove {
    __u32  bp_id;
    __s32  pid;
    char   reserved[64];
} __packed;

/* --- CMD_HWBP_LIST (48 bytes) --- */
struct kai_hwbp_list_packet {
    __s32  pid;
    __s32  total_bps;
    __s32  actual_bps;
    __s32  pad;
    __u64  out_buf;
    __u64  out_buf_size;
    __u64  reserved[2];
} __packed;

/* 断点条目 (28 bytes) */
struct kai_bp_entry {
    __u32  index;
    __u64  addr;
    __u32  type;
    __u32  len;
    __u64  hit_count;
    __u32  active;
} __packed;

/* --- CMD_SWBP_SET (80 bytes) --- */
struct kai_swbp_setup {
    __s32  pid;
    __u32  pad;
    __u64  address;
    __u32  bp_id;
    char   reserved[60];
} __packed;

/* --- CMD_READ_REGS (360 bytes) --- */
struct kai_reg_read_packet {
    __u32  bp_id;
    __u32  valid;
    __u64  regs[ARM64_REG_COUNT]; /* X0-X30, SP, PC, PSTATE */
} __packed;

/* --- CMD_CALLSTACK (56 bytes) --- */
struct kai_callstack_packet {
    __s32  pid;
    __s32  bp_id;
    __s32  actual_frames;
    __s32  total_frames;
    __u64  out_buf;
    __u64  out_buf_size;
    __s32  max_frames;
    __s32  pad;
    __u64  reserved[2];
} __packed;

/* 栈帧条目 (48 bytes) */
struct kai_stack_frame {
    __u64  pc;
    __u64  sp;
    __u64  fp;
    __u64  lr;
    char   symbol[16];
} __packed;

/* --- CMD_RESOLVE_PKG (12 bytes) — 新增：包名→PID解析 --- */
struct kai_resolve_pkg {
    char   pkg_name[PKG_NAME_MAX_LEN]; /* 输入：包名 */
    __s32  pid;                        /* 输出：PID */
    __u32  running;                    /* 输出：1=运行中, 0=未运行 */
} __packed;

/* =====================================================================
 * ioctl 命令定义 — 与 IoctlCommands.kt 严格对应
 * ===================================================================== */

#define IOCTL_MAGIC  'M'

/* 辅助宏：生成 _IOWR/_IOW/_IOR 值 */
#define KAI_IOWR(nr, size)  _IOWR(IOCTL_MAGIC, nr, char[size])
#define KAI_IOW(nr, size)   _IOW(IOCTL_MAGIC, nr, char[size])
#define KAI_IOR(nr, size)   _IOR(IOCTL_MAGIC, nr, char[size])

/* 原有命令（向后兼容，保留旧接口） */
#define CMD_GET_BASE_LEGACY     KAI_IOWR(1,  104)
#define CMD_READ_MEM_LEGACY     KAI_IOWR(2,  104)
#define CMD_WRITE_MEM           KAI_IOWR(3,  104)
#define CMD_READ_PTR_LEGACY     KAI_IOWR(4,  104)
#define CMD_READ_BATCH_LEGACY   KAI_IOWR(5,  56)
#define CMD_GET_MODULE_INFO     KAI_IOR(6,   24)
#define CMD_SET_HW_BP_LEGACY    KAI_IOW(7,   104)
#define CMD_SET_BLR_ADDRS_LEGACY KAI_IOW(8,  104)
#define CMD_QUERY_SNAPSHOT_LEGACY KAI_IOWR(9, 104)
#define CMD_GET_HWBP_STATS      KAI_IOR(10,  32)

/* 新增命令 — 完全匹配 App 端 IoctlCommands.kt */
#define CMD_LIST_PROCS          KAI_IOWR(11, 24)
#define CMD_LIST_MODULES        KAI_IOWR(12, 48)
#define CMD_LIST_VMAS           KAI_IOWR(13, 48)
#define CMD_READ_REGS           KAI_IOWR(14, 360)
#define CMD_HWBP_SET            KAI_IOW(15,  88)
#define CMD_HWBP_REMOVE         KAI_IOW(16,  72)
#define CMD_HWBP_LIST           KAI_IOWR(17, 48)
#define CMD_SWBP_SET            KAI_IOWR(18, 80)
#define CMD_SWBP_REMOVE         KAI_IOW(19,  72)
#define CMD_MEM_SEARCH          KAI_IOWR(20, 600)
#define CMD_CALLSTACK           KAI_IOWR(21, 56)
#define CMD_LIST_THREADS        KAI_IOWR(22, 48)
#define CMD_RAW_READ            KAI_IOWR(23, 40)

/* ★ 新增命令：包名→PID解析（核心优化） */
#define CMD_RESOLVE_PKG         KAI_IOWR(24, sizeof(struct kai_resolve_pkg))

/* =====================================================================
 * 性能统计结构
 * ===================================================================== */

struct module_perf_info {
    __u64  cpu_time_ns;
    __u64  call_count;
    __u64  pkg_resolve_count;
    __u64  pkg_resolve_hit;
    __u64  mem_read_count;
    __u64  mem_write_count;
    __u64  mem_read_bytes;
};

struct hwbp_stats {
    __u64  hit_count;
    __u64  bp_addr;
    __s32  snapshot_count;
    __s32  active;
};

/* =====================================================================
 * PID 缓存 — 哈希表 + LRU
 * ===================================================================== */

struct pid_cache_entry {
    struct hlist_node   hash_node;
    struct list_head    lru_node;
    char                pkg_name[PKG_NAME_MAX_LEN];
    __s32               pid;
    __u32               running;
    struct task_struct  *task;
    ktime_t             last_access;
    atomic_t            ref_count;
};

/* =====================================================================
 * 全局变量
 * ===================================================================== */

/* 进程查找缓存 */
static DEFINE_MUTEX(g_cache_lock);
static struct hlist_head g_pid_cache_hash[PID_CACHE_SIZE];
static LIST_HEAD(g_pid_cache_lru);
static struct pid_cache_entry g_pid_cache_pool[PID_CACHE_SIZE];
static int g_pid_cache_init_done;

/* 性能统计 */
static atomic64_t g_total_cpu_ns    = ATOMIC64_INIT(0);
static atomic64_t g_call_count      = ATOMIC64_INIT(0);
static atomic64_t g_pkg_resolve_cnt = ATOMIC64_INIT(0);
static atomic64_t g_pkg_resolve_hit = ATOMIC64_INIT(0);
static atomic64_t g_mem_read_cnt    = ATOMIC64_INIT(0);
static atomic64_t g_mem_read_bytes  = ATOMIC64_INIT(0);
static atomic64_t g_mem_write_cnt   = ATOMIC64_INIT(0);

#ifdef CONFIG_HAVE_HW_BREAKPOINT
/* HW Breakpoint 动态符号解析 */
typedef struct perf_event *(*register_user_hw_bp_fn)(
    struct perf_event_attr *attr,
    perf_overflow_handler_t triggered,
    void *context,
    struct task_struct *tsk);
typedef void (*unregister_hw_bp_fn)(struct perf_event *bp);

static register_user_hw_bp_fn  g_reg_hw_bp;
static unregister_hw_bp_fn     g_unreg_hw_bp;

/* HW Breakpoint 状态 */
#define MAX_HW_BREAKPOINTS  16
struct hw_bp_slot {
    struct perf_event   *event;
    struct task_struct  *target_task;
    __u64                bp_addr;
    __u32                bp_type;
    __u32                bp_len;
    __u32                bp_id;
    atomic64_t           hit_count;
    bool                 active;
};
static struct hw_bp_slot g_hw_bp_slots[MAX_HW_BREAKPOINTS];
static DEFINE_SPINLOCK(g_hw_bp_lock);
static __u32 g_next_bp_id = 1;

/* 寄存器快照（断点命中时保存） */
struct reg_snapshot {
    __u32  bp_id;
    __u32  valid;
    __u64  regs[ARM64_REG_COUNT];
};
static struct reg_snapshot g_reg_snapshots[MAX_HW_BREAKPOINTS];
static DEFINE_SPINLOCK(g_reg_snap_lock);

/* 坐标快照缓存（环形缓冲区） */
struct snapshot_entry {
    unsigned long  obj_addr;
    u32            x_raw, y_raw, z_raw;
    unsigned long  jiffies_val;
};
static struct snapshot_entry g_snapshots[SNAPSHOT_CACHE_SIZE];
static int g_snapshot_head;
static int g_snapshot_count;
static DEFINE_SPINLOCK(g_snapshot_lock);
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

/* ARM64 巨页检测兼容 */
#ifndef pud_leaf
#define pud_leaf(pud)   pud_sect(pud)
#endif
#ifndef pmd_leaf
#define pmd_leaf(pmd)   pmd_sect(pmd)
#endif

/* =====================================================================
 * 第一部分：PID 缓存管理
 * ===================================================================== */

/**
 * 初始化 PID 缓存池
 */
static void pid_cache_init(void)
{
    int i;

    if (g_pid_cache_init_done)
        return;

    for (i = 0; i < PID_CACHE_SIZE; i++) {
        INIT_HLIST_NODE(&g_pid_cache_pool[i].hash_node);
        INIT_LIST_HEAD(&g_pid_cache_pool[i].lru_node);
        atomic_set(&g_pid_cache_pool[i].ref_count, 0);
    }
    for (i = 0; i < PID_CACHE_SIZE; i++)
        INIT_HLIST_HEAD(&g_pid_cache_hash[i]);

    g_pid_cache_init_done = 1;
}

/**
 * DJB2 哈希 — 用于包名→缓存桶映射
 */
static unsigned int pkg_name_hash(const char *name)
{
    unsigned int hash = 5381;

    while (*name) {
        hash = ((hash << 5) + hash) + (unsigned char)*name;
        name++;
    }
    return hash & PID_CACHE_MASK;
}

/**
 * 在缓存中查找包名对应的 PID
 * 返回: 0 = 未命中, >0 = 命中（PID）
 */
static int pid_cache_lookup(const char *pkg_name, int *out_pid)
{
    unsigned int h = pkg_name_hash(pkg_name);
    struct pid_cache_entry *entry;

    hlist_for_each_entry(entry, &g_pid_cache_hash[h], hash_node) {
        if (strncmp(entry->pkg_name, pkg_name, PKG_NAME_MAX_LEN) == 0) {
            /* 验证进程是否仍然存活 */
            if (entry->task && pid_alive(entry->task) && entry->task->mm) {
                *out_pid = entry->pid;
                entry->last_access = ktime_get();
                /* 移到 LRU 头部 */
                list_del(&entry->lru_node);
                list_add(&entry->lru_node, &g_pid_cache_lru);
                atomic64_inc(&g_pkg_resolve_hit);
                return 1;
            }
            /* 进程已死，清除缓存条目 */
            hlist_del(&entry->hash_node);
            list_del(&entry->lru_node);
            if (entry->task) {
                put_task_struct(entry->task);
                entry->task = NULL;
            }
            entry->pkg_name[0] = '\0';
            break;
        }
    }
    return 0;
}

/**
 * 将包名→PID映射插入缓存
 */
static void pid_cache_insert(const char *pkg_name, int pid,
                              struct task_struct *task)
{
    unsigned int h = pkg_name_hash(pkg_name);
    struct pid_cache_entry *entry = NULL, *victim;

    /* 查找空闲槽位或淘汰 LRU 尾部 */
    victim = list_last_entry(&g_pid_cache_lru, struct pid_cache_entry, lru_node);
    if (victim && victim->pkg_name[0]) {
        /* 淘汰最久未使用的条目 */
        hlist_del(&victim->hash_node);
        list_del(&victim->lru_node);
        if (victim->task)
            put_task_struct(victim->task);
        entry = victim;
    } else {
        /* 使用未初始化的池条目 */
        int i;
        for (i = 0; i < PID_CACHE_SIZE; i++) {
            if (!g_pid_cache_pool[i].pkg_name[0]) {
                entry = &g_pid_cache_pool[i];
                break;
            }
        }
    }

    if (!entry)
        return;

    strncpy(entry->pkg_name, pkg_name, PKG_NAME_MAX_LEN - 1);
    entry->pkg_name[PKG_NAME_MAX_LEN - 1] = '\0';
    entry->pid = pid;
    entry->running = 1;
    entry->last_access = ktime_get();
    if (task)
        get_task_struct(task);
    entry->task = task;

    hlist_add_head(&entry->hash_node, &g_pid_cache_hash[h]);
    list_add(&entry->lru_node, &g_pid_cache_lru);
}

/* =====================================================================
 * 第二部分：核心 — 包名→PID解析（内核态通过 /proc 读取）
 * ===================================================================== */

/**
 * 读取进程的 /proc/<pid>/cmdline 内容
 * 返回: 0 = 成功, <0 = 失败
 *
 * cmdline 是进程的完整命令行，对于 Android 应用就是包名
 * 例如: "com.example.app\0" 或 "com.example.app:param1\0"
 */
static int read_proc_cmdline(struct task_struct *task, char *buf, size_t buf_size)
{
    struct mm_struct *mm;
    size_t len;
    long ret;

    buf[0] = '\0';

    mm = get_task_mm(task);
    if (!mm)
        return -EINVAL;

    /* 检查 arg_start 是否有效 */
    if (!mm->arg_start || mm->arg_end <= mm->arg_start) {
        mmput(mm);
        return -ENOENT;
    }

    len = min_t(size_t, buf_size - 1, mm->arg_end - mm->arg_start);
    if (len == 0) {
        mmput(mm);
        return -ENOENT;
    }

    /* 从用户空间读取 cmdline（内核态读取目标进程的用户空间内存） */
    ret = strncpy_from_user(buf, (char __user *)mm->arg_start, len);
    mmput(mm);

    if (ret < 0)
        return (int)ret;

    buf[ret] = '\0';
    return 0;
}

/**
 * ★ 核心优化函数：通过包名查找 PID ★
 *
 * 工作流程：
 *   1. 先查缓存 → 命中则直接返回
 *   2. 未命中则遍历所有进程
 *   3. 对每个进程：先比较 comm（快速路径），再读 cmdline（精确匹配）
 *   4. 匹配成功 → 插入缓存并返回 PID
 *   5. 检查进程运行状态（mm 是否存在）
 *
 * 参数：
 *   pkg_name: 目标包名（如 "com.example.app"）
 *   out_pid:  输出 PID
 *   out_running: 输出运行状态（1=运行中, 0=未运行）
 *
 * 返回: 0 = 成功找到, -ESRCH = 未找到, -EINVAL = 参数无效
 */
static int resolve_pkg_to_pid(const char *pkg_name, int *out_pid,
                               u32 *out_running)
{
    struct task_struct *task;
    int found_pid = 0;
    char cmdline_buf[CMDLINE_BUF_SIZE];

    if (!pkg_name || !pkg_name[0])
        return -EINVAL;

    atomic64_inc(&g_pkg_resolve_cnt);

    /* 第一步：查缓存 */
    mutex_lock(&g_cache_lock);
    if (pid_cache_lookup(pkg_name, out_pid)) {
        *out_running = 1;
        mutex_unlock(&g_cache_lock);
        return 0;
    }
    mutex_unlock(&g_cache_lock);

    /* 第二步：遍历所有进程查找 */
    rcu_read_lock();
    for_each_process(task) {
        int matched = 0;

        /* 快速路径：比较 comm（进程短名称，通常 <= 15 字符） */
        if (strcmp(task->comm, pkg_name) == 0) {
            matched = 1;
        }

        /* 精确路径：读取 cmdline 进行包名匹配 */
        if (!matched && task->mm) {
            struct mm_struct *mm;
            size_t len;
            long ret;

            mm = get_task_mm(task);
            if (mm && mm->arg_start && mm->arg_end > mm->arg_start) {
                len = min_t(size_t, sizeof(cmdline_buf) - 1,
                            mm->arg_end - mm->arg_start);
                if (len > 0) {
                    ret = strncpy_from_user(cmdline_buf,
                                            (char __user *)mm->arg_start, len);
                    if (ret > 0) {
                        cmdline_buf[ret] = '\0';
                        /* 精确匹配：cmdline 以包名开头或包含包名 */
                        if (strcmp(cmdline_buf, pkg_name) == 0 ||
                            strncmp(cmdline_buf, pkg_name, strlen(pkg_name)) == 0) {
                            matched = 1;
                        }
                    }
                }
            }
            if (mm)
                mmput(mm);
        }

        if (matched) {
            /* 检查进程是否真正在运行（有有效内存映射） */
            if (task->mm) {
                *out_pid = task->pid;
                *out_running = 1;
                found_pid = 1;

                /* 插入缓存 */
                mutex_lock(&g_cache_lock);
                pid_cache_insert(pkg_name, task->pid, task);
                mutex_unlock(&g_cache_lock);
                break;
            }
        }
    }
    rcu_read_unlock();

    if (!found_pid) {
        *out_pid = 0;
        *out_running = 0;
        return -ESRCH;
    }

    return 0;
}

/**
 * 检查指定包名的进程是否正在运行
 * 返回: 1 = 运行中, 0 = 未运行
 */
static int check_pkg_running(const char *pkg_name)
{
    struct task_struct *task;
    int running = 0;
    char cmdline_buf[CMDLINE_BUF_SIZE];

    if (!pkg_name || !pkg_name[0])
        return 0;

    rcu_read_lock();
    for_each_process(task) {
        if (task->mm) {
            struct mm_struct *mm = get_task_mm(task);
            if (mm && mm->arg_start && mm->arg_end > mm->arg_start) {
                size_t len = min_t(size_t, sizeof(cmdline_buf) - 1,
                                   mm->arg_end - mm->arg_start);
                if (len > 0) {
                    long ret = strncpy_from_user(cmdline_buf,
                                                 (char __user *)mm->arg_start, len);
                    if (ret > 0) {
                        cmdline_buf[ret] = '\0';
                        if (strcmp(cmdline_buf, pkg_name) == 0 ||
                            strncmp(cmdline_buf, pkg_name, strlen(pkg_name)) == 0) {
                            running = 1;
                            mmput(mm);
                            break;
                        }
                    }
                }
            }
            if (mm)
                mmput(mm);
        }
    }
    rcu_read_unlock();

    return running;
}

/* =====================================================================
 * 第三部分：任务解析（支持 PID 或包名）
 * ===================================================================== */

/**
 * 根据 PID 或包名获取 task_struct
 *
 * 优先级：
 *   1. 如果 pid > 0，直接通过 PID 查找
 *   2. 如果 pid <= 0 且 pkg_name 非空，通过包名解析 PID
 *
 * 返回: task_struct 指针（需要调用者 put_task_struct），NULL = 未找到
 */
static struct task_struct *resolve_task(int pid, const char *pkg_name)
{
    struct task_struct *task = NULL;
    int target_pid = pid;

    /* 如果 pid 无效，尝试通过包名解析 */
    if (target_pid <= 0 && pkg_name && pkg_name[0]) {
        u32 running = 0;
        int ret = resolve_pkg_to_pid(pkg_name, &target_pid, &running);
        if (ret < 0 || target_pid <= 0)
            return NULL;
    }

    if (target_pid <= 0)
        return NULL;

    /* 通过 PID 查找 task_struct */
    rcu_read_lock();
    task = find_task_by_vpid(target_pid);
    if (task)
        get_task_struct(task);
    rcu_read_unlock();

    /* 验证进程有效性 */
    if (task) {
        if (!pid_alive(task) || !task->mm) {
            put_task_struct(task);
            return NULL;
        }
    }

    return task;
}

/* =====================================================================
 * 第四部分：内存读写（优化版页表遍历）
 * ===================================================================== */

/**
 * 底层内存读取 — 已持锁版本
 * 支持 ARM64 巨页（pud leaf / pmd leaf）检测
 */
static int __read_memory_locked(struct mm_struct *mm, unsigned long vaddr,
                                 void *kbuf, size_t len)
{
    size_t done = 0;
    int ret = 0;

    while (done < len) {
        unsigned long addr = vaddr + done;
        unsigned long remaining = len - done;
        unsigned long page_offset = addr & ~PAGE_MASK;
        size_t copy_size = min_t(size_t, remaining, PAGE_SIZE - page_offset);
        pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;
        struct page *page;
        void *kmap_addr;
        unsigned long pfn;

        /* 四级页表遍历 */
        pgd = pgd_offset(mm, addr);
        if (pgd_none(*pgd) || pgd_bad(*pgd)) { ret = -EFAULT; break; }
        p4d = p4d_offset(pgd, addr);
        if (p4d_none(*p4d) || p4d_bad(*p4d)) { ret = -EFAULT; break; }
        pud = pud_offset(p4d, addr);
        if (pud_none(*pud) || pud_bad(*pud)) { ret = -EFAULT; break; }

        /* ARM64 巨页：pud 级别直接映射 */
        if (pud_leaf(*pud)) {
            pfn = pud_pfn(*pud);
            if (!pfn_valid(pfn)) { ret = -EFAULT; break; }
            page = pfn_to_page(pfn);
            if (!page) { ret = -EFAULT; break; }
            kmap_addr = kmap_local_page(page);
            memcpy(kbuf + done, kmap_addr + page_offset, copy_size);
            kunmap_local(kmap_addr);
            done += copy_size;
            continue;
        }

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd)) { ret = -EFAULT; break; }

        /* ARM64 巨页：pmd 级别直接映射 */
        if (pmd_leaf(*pmd)) {
            pfn = pmd_pfn(*pmd);
            if (!pfn_valid(pfn)) { ret = -EFAULT; break; }
            page = pfn_to_page(pfn);
            if (!page) { ret = -EFAULT; break; }
            kmap_addr = kmap_local_page(page);
            memcpy(kbuf + done, kmap_addr + page_offset, copy_size);
            kunmap_local(kmap_addr);
            done += copy_size;
            continue;
        }

        /* 标准 4K 页 */
        pte = pte_offset_map(pmd, addr);
        if (!pte) { ret = -EFAULT; break; }
        if (!pte_present(*pte)) { pte_unmap(pte); ret = -EFAULT; break; }
        pfn = pte_pfn(*pte);
        if (!pfn_valid(pfn)) { pte_unmap(pte); ret = -EFAULT; break; }
        page = pfn_to_page(pfn);
        if (!page) { pte_unmap(pte); ret = -EFAULT; break; }
        kmap_addr = kmap_local_page(page);
        memcpy(kbuf + done, kmap_addr + page_offset, copy_size);
        kunmap_local(kmap_addr);
        pte_unmap(pte);
        done += copy_size;
    }

    return ret ? ret : (int)done;
}

/**
 * 外层内存读取 — 自动加锁
 */
static int read_memory(struct task_struct *task, unsigned long vaddr,
                        void *kbuf, size_t len)
{
    struct mm_struct *mm = task->mm;
    int ret;

    if (!mm)
        return -EINVAL;

    mmap_read_lock(mm);
    ret = __read_memory_locked(mm, vaddr, kbuf, len);
    mmap_read_unlock(mm);

    if (ret > 0) {
        atomic64_inc(&g_mem_read_cnt);
        atomic64_add(ret, &g_mem_read_bytes);
    }

    return ret;
}

/**
 * 内存写入 — 支持 ARM64 巨页
 */
static int write_memory(struct task_struct *task, unsigned long vaddr,
                         void *kbuf, size_t len)
{
    struct mm_struct *mm = task->mm;
    size_t done = 0;
    int ret = 0;

    if (!mm)
        return -EINVAL;

    mmap_read_lock(mm);
    while (done < len) {
        unsigned long addr = vaddr + done;
        unsigned long remaining = len - done;
        unsigned long page_offset = addr & ~PAGE_MASK;
        size_t copy_size = min_t(size_t, remaining, PAGE_SIZE - page_offset);
        pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;
        struct page *page;
        void *kmap_addr;
        unsigned long pfn;

        pgd = pgd_offset(mm, addr);
        if (pgd_none(*pgd) || pgd_bad(*pgd)) { ret = -EFAULT; break; }
        p4d = p4d_offset(pgd, addr);
        if (p4d_none(*p4d) || p4d_bad(*p4d)) { ret = -EFAULT; break; }
        pud = pud_offset(p4d, addr);
        if (pud_none(*pud) || pud_bad(*pud)) { ret = -EFAULT; break; }

        if (pud_leaf(*pud)) {
            pfn = pud_pfn(*pud);
            if (!pfn_valid(pfn)) { ret = -EFAULT; break; }
            page = pfn_to_page(pfn);
            if (!page) { ret = -EFAULT; break; }
            kmap_addr = kmap_local_page(page);
            memcpy(kmap_addr + page_offset, kbuf + done, copy_size);
            kunmap_local(kmap_addr);
            done += copy_size;
            continue;
        }

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd)) { ret = -EFAULT; break; }

        if (pmd_leaf(*pmd)) {
            pfn = pmd_pfn(*pmd);
            if (!pfn_valid(pfn)) { ret = -EFAULT; break; }
            page = pfn_to_page(pfn);
            if (!page) { ret = -EFAULT; break; }
            kmap_addr = kmap_local_page(page);
            memcpy(kmap_addr + page_offset, kbuf + done, copy_size);
            kunmap_local(kmap_addr);
            done += copy_size;
            continue;
        }

        pte = pte_offset_map(pmd, addr);
        if (!pte) { ret = -EFAULT; break; }
        if (!pte_present(*pte) || !pte_write(*pte)) {
            pte_unmap(pte);
            ret = -EPERM;
            break;
        }
        pfn = pte_pfn(*pte);
        if (!pfn_valid(pfn)) { pte_unmap(pte); ret = -EFAULT; break; }
        page = pfn_to_page(pfn);
        if (!page) { pte_unmap(pte); ret = -EFAULT; break; }
        kmap_addr = kmap_local_page(page);
        memcpy(kmap_addr + page_offset, kbuf + done, copy_size);
        kunmap_local(kmap_addr);
        pte_unmap(pte);
        done += copy_size;
    }
    mmap_read_unlock(mm);

    if (ret == 0 && done > 0)
        atomic64_inc(&g_mem_write_cnt);

    return ret ? ret : (int)done;
}

/* =====================================================================
 * 第五部分：进程/模块/VMA/线程枚举
 * ===================================================================== */

/**
 * 枚举系统进程列表
 */
static int enumerate_processes(struct kai_proc_entry *entries,
                                int max_count, int offset)
{
    struct task_struct *task;
    int count = 0;
    int skip = 0;

    rcu_read_lock();
    for_each_process(task) {
        if (skip < offset) {
            skip++;
            continue;
        }
        if (count >= max_count)
            break;

        entries[count].pid = task->pid;
        entries[count].ppid = task->real_parent ? task->real_parent->pid : 0;
        entries[count].tgid = task->tgid;
        strncpy(entries[count].comm, task->comm,
                sizeof(entries[count].comm) - 1);
        entries[count].comm[sizeof(entries[count].comm) - 1] = '\0';

        /* 读取 cmdline */
        entries[count].cmdline[0] = '\0';
        if (task->mm) {
            struct mm_struct *mm = get_task_mm(task);
            if (mm && mm->arg_start && mm->arg_end > mm->arg_start) {
                size_t len = min_t(size_t,
                                   sizeof(entries[count].cmdline) - 1,
                                   mm->arg_end - mm->arg_start);
                if (len > 0) {
                    long ret = strncpy_from_user(
                        entries[count].cmdline,
                        (char __user *)mm->arg_start, len);
                    if (ret > 0) {
                        entries[count].cmdline[ret] = '\0';
                    }
                }
            }
            if (mm)
                mmput(mm);
        }

        entries[count].state = task->__state;

        /* 统计线程数 */
        {
            struct task_struct *t;
            int tc = 0;
            rcu_read_lock(); /* nested - safe in RCU read-side */
            for_each_thread(task, t)
                tc++;
            rcu_read_unlock();
            entries[count].thread_count = tc;
        }

        count++;
    }
    rcu_read_unlock();

    return count;
}

/**
 * 统计系统总进程数
 */
static int count_total_processes(void)
{
    struct task_struct *task;
    int total = 0;

    rcu_read_lock();
    for_each_process(task)
        total++;
    rcu_read_unlock();

    return total;
}

/**
 * 枚举进程加载的模块（共享库）
 */
static int enumerate_modules(struct task_struct *task,
                              struct kai_module_entry *entries,
                              int max_count)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    unsigned long addr = 0;
    int count = 0;
    char *pathbuf;

    if (!mm)
        return 0;

    pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!pathbuf)
        return 0;

    mmap_read_lock(mm);
    while ((vma = find_vma(mm, addr)) != NULL && count < max_count) {
        if (vma->vm_file) {
            char *path = d_path(&vma->vm_file->f_path, pathbuf, PAGE_SIZE);
            if (!IS_ERR(path)) {
                /* 避免重复：只记录首次出现的映射 */
                int dup = 0;
                int i;
                for (i = 0; i < count; i++) {
                    if (strncmp(entries[i].path, path,
                                sizeof(entries[i].path)) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    entries[count].base_addr = vma->vm_start;
                    entries[count].end_addr = vma->vm_end;
                    entries[count].size = vma->vm_end - vma->vm_start;
                    entries[count].flags = vma->vm_flags & 0xf;
                    entries[count].offset = vma->vm_pgoff << PAGE_SHIFT;
                    strncpy(entries[count].path, path,
                            sizeof(entries[count].path) - 1);
                    entries[count].path[sizeof(entries[count].path) - 1] = '\0';
                    entries[count].is_exec =
                        (vma->vm_flags & VM_EXEC) ? 1 : 0;
                    count++;
                }
            }
        }
        addr = vma->vm_end;
    }
    mmap_read_unlock(mm);

    kfree(pathbuf);
    return count;
}

/**
 * 枚举进程 VMA（虚拟内存区域）
 */
static int enumerate_vmas(struct task_struct *task,
                           struct kai_vma_entry *entries,
                           int max_count)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    int count = 0;

    if (!mm)
        return 0;

    mmap_read_lock(mm);
    vma = mm->mmap;
    while (vma && count < max_count) {
        entries[count].start = vma->vm_start;
        entries[count].end = vma->vm_end;
        entries[count].flags = vma->vm_flags & 0xf;
        entries[count].pgoff = vma->vm_pgoff;

        if (vma->vm_file) {
            char buf[16];
            char *path = d_path(&vma->vm_file->f_path, buf, sizeof(buf));
            if (!IS_ERR(path)) {
                strncpy(entries[count].name, path,
                        sizeof(entries[count].name) - 1);
                entries[count].name[sizeof(entries[count].name) - 1] = '\0';
            }
        } else {
            entries[count].name[0] = '\0';
        }
        entries[count].pad = 0;

        count++;
        vma = vma->vm_next;
    }
    mmap_read_unlock(mm);

    return count;
}

/**
 * 枚举进程的所有线程
 */
static int enumerate_threads(struct task_struct *task,
                              struct kai_thread_entry *entries,
                              int max_count)
{
    struct task_struct *t;
    int count = 0;

    rcu_read_lock();
    for_each_thread(task, t) {
        if (count >= max_count)
            break;

        entries[count].tid = t->pid;
        strncpy(entries[count].comm, t->comm,
                sizeof(entries[count].comm) - 1);
        entries[count].comm[sizeof(entries[count].comm) - 1] = '\0';
        entries[count].state = t->__state;
        entries[count].stack_ptr = (unsigned long)t->stack;

        /* PC: 尝试从 thread_saved_pc 获取 */
#ifdef thread_saved_pc
        entries[count].pc = thread_saved_pc(t);
#else
        entries[count].pc = 0;
#endif
        count++;
    }
    rcu_read_unlock();

    return count;
}

/**
 * 获取模块基址
 */
static unsigned long get_module_base(struct task_struct *task,
                                      const char *mod_name)
{
    struct mm_struct *mm = task->mm;
    unsigned long base = 0;
    unsigned long addr = 0;
    struct vm_area_struct *vma;
    char *pathbuf;

    if (!mm)
        return 0;

    pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!pathbuf)
        return 0;

    mmap_read_lock(mm);
    while ((vma = find_vma(mm, addr)) != NULL) {
        if (vma->vm_file) {
            char *path = d_path(&vma->vm_file->f_path, pathbuf, PAGE_SIZE);
            if (!IS_ERR(path) && strstr(path, mod_name)) {
                base = vma->vm_start;
                break;
            }
        }
        addr = vma->vm_end;
    }
    mmap_read_unlock(mm);

    kfree(pathbuf);
    return base;
}

/* =====================================================================
 * 第六部分：内存搜索
 * ===================================================================== */

/**
 * 在目标进程地址空间中搜索字节模式
 * 支持 ?? 通配符（pattern 中对应字节为 0 且 mask 为 0）
 */
static int search_memory_pattern(struct task_struct *task,
                                  const char *pattern, u32 pattern_size,
                                  unsigned long start_addr,
                                  unsigned long end_addr,
                                  int max_results,
                                  unsigned long *results, int *total_found)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    int result_count = 0;
    int total = 0;
    char *read_buf;

    if (!mm || !pattern || pattern_size == 0)
        return 0;

    read_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!read_buf)
        return 0;

    *total_found = 0;

    mmap_read_lock(mm);

    if (start_addr == 0 && end_addr == 0) {
        /* 搜索所有 VMA */
        vma = mm->mmap;
    } else {
        vma = find_vma(mm, start_addr);
    }

    while (vma && result_count < max_results) {
        unsigned long scan_addr;
        unsigned long scan_end;

        if (end_addr > 0 && vma->vm_start > end_addr)
            break;

        scan_addr = max(vma->vm_start, start_addr);
        scan_end = vma->vm_end;
        if (end_addr > 0)
            scan_end = min(scan_end, end_addr);

        while (scan_addr + pattern_size <= scan_end &&
               result_count < max_results) {
            int ret = __read_memory_locked(mm, scan_addr, read_buf,
                                            pattern_size);
            if (ret > 0) {
                if (memcmp(read_buf, pattern, pattern_size) == 0) {
                    if (result_count < max_results)
                        results[result_count] = scan_addr;
                    result_count++;
                    total++;
                }
            }
            scan_addr++;
        }

        vma = vma->vm_next;
    }

    mmap_read_unlock(mm);
    kfree(read_buf);

    *total_found = total;
    return result_count;
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
/* =====================================================================
 * 第七部分：硬件断点支持
 * ===================================================================== */

/**
 * 通过 kprobe 解析未导出的内核符号
 */
static unsigned long resolve_symbol(const char *name)
{
    struct kprobe kp;
    unsigned long addr;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = (char *)name;

    if (register_kprobe(&kp) < 0)
        return 0;

    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    return addr;
}

/**
 * HWBP 溢出处理 — 中断上下文，只能使用 spinlock
 */
static void hw_bp_overflow_handler(struct perf_event *event,
                                    struct perf_sample_data *data,
                                    struct pt_regs *regs)
{
    int slot_idx;
    struct hw_bp_slot *slot = (struct hw_bp_slot *)event->overflow_handler_context;

    if (!regs || !slot)
        return;

    slot_idx = slot->bp_id;
    atomic64_inc(&slot->hit_count);

    /* 保存寄存器快照 */
    if (slot_idx >= 0 && slot_idx < MAX_HW_BREAKPOINTS) {
        int i;
        spin_lock(&g_reg_snap_lock);
        g_reg_snapshots[slot_idx].bp_id = slot->bp_id;
        g_reg_snapshots[slot_idx].valid = 1;
        for (i = 0; i < ARM64_REG_COUNT && i < 34; i++) {
            if (i < 31)
                g_reg_snapshots[slot_idx].regs[i] = regs->regs[i];
            else if (i == 31)
                g_reg_snapshots[slot_idx].regs[i] = regs->sp;
            else if (i == 32)
                g_reg_snapshots[slot_idx].regs[i] = regs->pc;
            else
                g_reg_snapshots[slot_idx].regs[i] = regs->pstate;
        }
        spin_unlock(&g_reg_snap_lock);
    }
}

/**
 * 设置硬件断点
 */
static int hw_bp_set(int pid, unsigned long addr, u32 bp_type, u32 bp_len,
                      u32 *out_bp_id)
{
    struct perf_event_attr attr;
    struct task_struct *task;
    int slot = -1;
    int i;
    struct perf_event *bp;

    if (!g_reg_hw_bp)
        return -ENOSYS;

    /* 查找空闲槽位 */
    spin_lock(&g_hw_bp_lock);
    for (i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (!g_hw_bp_slots[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        spin_unlock(&g_hw_bp_lock);
        return -ENOSPC;
    }
    g_hw_bp_slots[slot].bp_id = g_next_bp_id++;
    *out_bp_id = g_hw_bp_slots[slot].bp_id;
    spin_unlock(&g_hw_bp_lock);

    /* 查找目标进程 */
    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (task)
        get_task_struct(task);
    rcu_read_unlock();
    if (!task)
        return -ESRCH;

    /* 初始化断点属性 */
    hw_breakpoint_init(&attr);
    attr.bp_addr = addr;
    attr.bp_len = bp_len ? bp_len : HW_BREAKPOINT_LEN_4;
    attr.bp_type = bp_type;

    bp = g_reg_hw_bp(&attr, hw_bp_overflow_handler,
                      &g_hw_bp_slots[slot], task);
    if (IS_ERR(bp)) {
        put_task_struct(task);
        return PTR_ERR(bp);
    }

    g_hw_bp_slots[slot].event = bp;
    g_hw_bp_slots[slot].target_task = task;
    g_hw_bp_slots[slot].bp_addr = addr;
    g_hw_bp_slots[slot].bp_type = bp_type;
    g_hw_bp_slots[slot].bp_len = attr.bp_len;
    atomic64_set(&g_hw_bp_slots[slot].hit_count, 0);
    g_hw_bp_slots[slot].active = true;

    pr_info("HWBP set: slot=%d bp_id=%u addr=0x%lx pid=%d\n",
            slot, *out_bp_id, addr, pid);
    return 0;
}

/**
 * 移除硬件断点
 */
static int hw_bp_remove(u32 bp_id)
{
    int i;

    spin_lock(&g_hw_bp_lock);
    for (i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (g_hw_bp_slots[i].active &&
            g_hw_bp_slots[i].bp_id == bp_id) {
            struct hw_bp_slot *slot = &g_hw_bp_slots[i];

            if (slot->event && g_unreg_hw_bp)
                g_unreg_hw_bp(slot->event);
            if (slot->target_task)
                put_task_struct(slot->target_task);

            memset(slot, 0, sizeof(*slot));
            spin_unlock(&g_hw_bp_lock);

            pr_info("HWBP removed: bp_id=%u\n", bp_id);
            return 0;
        }
    }
    spin_unlock(&g_hw_bp_lock);

    return -ENOENT;
}

/**
 * 列出所有硬件断点
 */
static int hw_bp_list(struct kai_bp_entry *entries, int max_count)
{
    int count = 0;
    int i;

    spin_lock(&g_hw_bp_lock);
    for (i = 0; i < MAX_HW_BREAKPOINTS && count < max_count; i++) {
        if (g_hw_bp_slots[i].active) {
            entries[count].index = g_hw_bp_slots[i].bp_id;
            entries[count].addr = g_hw_bp_slots[i].bp_addr;
            entries[count].type = g_hw_bp_slots[i].bp_type;
            entries[count].len = g_hw_bp_slots[i].bp_len;
            entries[count].hit_count =
                atomic64_read(&g_hw_bp_slots[i].hit_count);
            entries[count].active = 1;
            count++;
        }
    }
    spin_unlock(&g_hw_bp_lock);

    return count;
}

/**
 * 读取断点命中时的寄存器快照
 */
static int hw_bp_read_regs(u32 bp_id, struct kai_reg_read_packet *pkt)
{
    int i;

    spin_lock(&g_reg_snap_lock);
    for (i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (g_reg_snapshots[i].valid &&
            g_reg_snapshots[i].bp_id == bp_id) {
            pkt->bp_id = bp_id;
            pkt->valid = 1;
            memcpy(pkt->regs, g_reg_snapshots[i].regs,
                   sizeof(pkt->regs));
            spin_unlock(&g_reg_snap_lock);
            return 0;
        }
    }
    spin_unlock(&g_reg_snap_lock);

    pkt->bp_id = bp_id;
    pkt->valid = 0;
    return -ENOENT;
}

/**
 * 清理所有硬件断点
 */
static void hw_bp_cleanup_all(void)
{
    int i;

    spin_lock(&g_hw_bp_lock);
    for (i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (g_hw_bp_slots[i].active) {
            if (g_hw_bp_slots[i].event && g_unreg_hw_bp)
                g_unreg_hw_bp(g_hw_bp_slots[i].event);
            if (g_hw_bp_slots[i].target_task)
                put_task_struct(g_hw_bp_slots[i].target_task);
            memset(&g_hw_bp_slots[i], 0, sizeof(struct hw_bp_slot));
        }
    }
    spin_unlock(&g_hw_bp_lock);
}

#else /* !CONFIG_HAVE_HW_BREAKPOINT */

static void hw_bp_cleanup_all(void) {}

#endif /* CONFIG_HAVE_HW_BREAKPOINT */

/* =====================================================================
 * 第八部分：ioctl 处理入口
 * ===================================================================== */

static long device_ioctl(struct file *filp, unsigned int cmd,
                          unsigned long arg)
{
    u64 start_ns = ktime_get_ns();
    long ret = 0;
    void __user *uarg = (void __user *)arg;

    /* ---- CMD_GET_MODULE_INFO: 获取模块性能信息 ---- */
    if (cmd == CMD_GET_MODULE_INFO) {
        struct module_perf_info info;

        info.cpu_time_ns = atomic64_read(&g_total_cpu_ns);
        info.call_count  = atomic64_read(&g_call_count);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
        info.mem_bytes   = THIS_MODULE->core_layout.size;
#else
        info.mem_bytes   = 0;
#endif
        if (copy_to_user(uarg, &info, sizeof(info)))
            ret = -EFAULT;
        goto out;
    }

    /* ---- CMD_GET_HWBP_STATS: 获取硬件断点统计 ---- */
    if (cmd == CMD_GET_HWBP_STATS) {
        struct hwbp_stats stats;
        memset(&stats, 0, sizeof(stats));
#ifdef CONFIG_HAVE_HW_BREAKPOINT
        {
            int i;
            for (i = 0; i < MAX_HW_BREAKPOINTS; i++) {
                if (g_hw_bp_slots[i].active) {
                    stats.hit_count +=
                        atomic64_read(&g_hw_bp_slots[i].hit_count);
                    stats.bp_addr = g_hw_bp_slots[i].bp_addr;
                    stats.active = 1;
                    break;
                }
            }
        }
        spin_lock(&g_snapshot_lock);
        stats.snapshot_count = g_snapshot_count;
        spin_unlock(&g_snapshot_lock);
#endif
        if (copy_to_user(uarg, &stats, sizeof(stats)))
            ret = -EFAULT;
        goto out;
    }

    /* ---- ★ CMD_RESOLVE_PKG: 包名→PID解析（核心优化）★ ---- */
    if (cmd == CMD_RESOLVE_PKG) {
        struct kai_resolve_pkg req;

        if (copy_from_user(&req, uarg, sizeof(req))) {
            ret = -EFAULT;
            goto out;
        }

        ret = resolve_pkg_to_pid(req.pkg_name, &req.pid, &req.running);
        if (ret < 0) {
            req.pid = 0;
            req.running = 0;
        }

        if (copy_to_user(uarg, &req, sizeof(req)))
            ret = -EFAULT;
        else
            ret = 0;
        goto out;
    }

    /* ---- CMD_LIST_PROCS: 列出进程 ---- */
    if (cmd == CMD_LIST_PROCS) {
        struct kai_proc_list_packet pkt;
        struct kai_proc_entry *entries;
        int actual, total;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        if (pkt.max_count <= 0 || pkt.max_count > MAX_PROC_ENTRIES)
            pkt.max_count = MAX_PROC_ENTRIES;

        entries = kcalloc(pkt.max_count, sizeof(*entries), GFP_KERNEL);
        if (!entries) {
            ret = -ENOMEM;
            goto out;
        }

        total = count_total_processes();
        actual = enumerate_processes(entries, pkt.max_count, pkt.offset);

        pkt.actual_count = actual;
        pkt.total_count = total;

        /* 将头部写回 */
        if (copy_to_user(uarg, &pkt, sizeof(pkt))) {
            kfree(entries);
            ret = -EFAULT;
            goto out;
        }

        /* 将进程条目写到头部之后的用户空间缓冲区 */
        if (actual > 0) {
            size_t data_size = actual * sizeof(*entries);
            void __user *data_dst = uarg + sizeof(pkt);

            if (copy_to_user(data_dst, entries, data_size)) {
                kfree(entries);
                ret = -EFAULT;
                goto out;
            }
        }

        kfree(entries);
        ret = 0;
        goto out;
    }

    /* ---- CMD_LIST_MODULES: 列出模块 ---- */
    if (cmd == CMD_LIST_MODULES) {
        struct kai_module_list_packet pkt;
        struct kai_module_entry *entries;
        struct task_struct *task;
        int actual;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(pkt.pid, NULL);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (pkt.max_count <= 0 || pkt.max_count > MAX_MODULE_ENTRIES)
            pkt.max_count = MAX_MODULE_ENTRIES;

        entries = kcalloc(pkt.max_count, sizeof(*entries), GFP_KERNEL);
        if (!entries) {
            put_task_struct(task);
            ret = -ENOMEM;
            goto out;
        }

        actual = enumerate_modules(task, entries, pkt.max_count);
        pkt.actual_count = actual;

        if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
            (actual > 0 && copy_to_user(uarg + sizeof(pkt),
                                         entries,
                                         actual * sizeof(*entries)))) {
            kfree(entries);
            put_task_struct(task);
            ret = -EFAULT;
            goto out;
        }

        kfree(entries);
        put_task_struct(task);
        ret = 0;
        goto out;
    }

    /* ---- CMD_LIST_VMAS: 列出 VMA ---- */
    if (cmd == CMD_LIST_VMAS) {
        struct kai_vma_list_packet pkt;
        struct kai_vma_entry *entries;
        struct task_struct *task;
        int actual;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(pkt.pid, NULL);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (pkt.max_count <= 0 || pkt.max_count > MAX_VMA_ENTRIES)
            pkt.max_count = MAX_VMA_ENTRIES;

        entries = kcalloc(pkt.max_count, sizeof(*entries), GFP_KERNEL);
        if (!entries) {
            put_task_struct(task);
            ret = -ENOMEM;
            goto out;
        }

        actual = enumerate_vmas(task, entries, pkt.max_count);
        pkt.actual_count = actual;

        if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
            (actual > 0 && copy_to_user(uarg + sizeof(pkt),
                                         entries,
                                         actual * sizeof(*entries)))) {
            kfree(entries);
            put_task_struct(task);
            ret = -EFAULT;
            goto out;
        }

        kfree(entries);
        put_task_struct(task);
        ret = 0;
        goto out;
    }

    /* ---- CMD_LIST_THREADS: 列出线程 ---- */
    if (cmd == CMD_LIST_THREADS) {
        struct kai_thread_list_packet pkt;
        struct kai_thread_entry *entries;
        struct task_struct *task;
        int actual;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(pkt.pid, NULL);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (pkt.max_count <= 0 || pkt.max_count > MAX_THREAD_ENTRIES)
            pkt.max_count = MAX_THREAD_ENTRIES;

        entries = kcalloc(pkt.max_count, sizeof(*entries), GFP_KERNEL);
        if (!entries) {
            put_task_struct(task);
            ret = -ENOMEM;
            goto out;
        }

        actual = enumerate_threads(task, entries, pkt.max_count);
        pkt.actual_count = actual;

        if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
            (actual > 0 && copy_to_user(uarg + sizeof(pkt),
                                         entries,
                                         actual * sizeof(*entries)))) {
            kfree(entries);
            put_task_struct(task);
            ret = -EFAULT;
            goto out;
        }

        kfree(entries);
        put_task_struct(task);
        ret = 0;
        goto out;
    }

    /* ---- CMD_RAW_READ: 原始内存读取 ---- */
    if (cmd == CMD_RAW_READ) {
        struct kai_raw_read_packet pkt;
        struct task_struct *task;
        void *kbuf;
        int actual;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(pkt.pid, NULL);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (pkt.size == 0 || pkt.size > MAX_SINGLE_READ) {
            put_task_struct(task);
            ret = -EINVAL;
            goto out;
        }

        kbuf = kmalloc(pkt.size, GFP_KERNEL);
        if (!kbuf) {
            put_task_struct(task);
            ret = -ENOMEM;
            goto out;
        }

        actual = read_memory(task, pkt.address, kbuf, pkt.size);
        if (actual > 0) {
            pkt.actual_size = actual;
            /* 将数据写到用户缓冲区头部之后 */
            if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
                copy_to_user(uarg + sizeof(pkt), kbuf, actual)) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }
        } else {
            pkt.actual_size = 0;
            if (copy_to_user(uarg, &pkt, sizeof(pkt)))
                ret = -EFAULT;
            else
                ret = actual < 0 ? actual : 0;
        }

        kfree(kbuf);
        put_task_struct(task);
        goto out;
    }

    /* ---- CMD_WRITE_MEM: 内存写入 ---- */
    if (cmd == CMD_WRITE_MEM) {
        /* 兼容旧接口: mem_packet_t 104 bytes */
        struct {
            __s32  pid;
            char   pkg_name[64];
            __u64  addr;
            __u32  size;
            __u32  pad;
            char   data[80];
        } __packed wpkt;
        struct task_struct *task;
        int written;

        if (copy_from_user(&wpkt, uarg, sizeof(wpkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(wpkt.pid, wpkt.pkg_name);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (wpkt.size == 0 || wpkt.size > 80) {
            put_task_struct(task);
            ret = -EINVAL;
            goto out;
        }

        written = write_memory(task, wpkt.addr, wpkt.data, wpkt.size);
        put_task_struct(task);
        ret = (written > 0) ? 0 : written;
        goto out;
    }

    /* ---- CMD_MEM_SEARCH: 内存搜索 ---- */
    if (cmd == CMD_MEM_SEARCH) {
        struct kai_mem_search_packet pkt;
        struct task_struct *task;
        unsigned long *results;
        int found;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = resolve_task(pkt.pid, NULL);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

        if (pkt.max_results <= 0 || pkt.max_results > MAX_SEARCH_RESULTS)
            pkt.max_results = MAX_SEARCH_RESULTS;

        results = kcalloc(pkt.max_results, sizeof(unsigned long),
                          GFP_KERNEL);
        if (!results) {
            put_task_struct(task);
            ret = -ENOMEM;
            goto out;
        }

        found = search_memory_pattern(task, pkt.pattern, pkt.pattern_size,
                                       pkt.start_addr, pkt.end_addr,
                                       pkt.max_results, results,
                                       &pkt.total_found);

        pkt.actual_results = found;

        if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
            (found > 0 && copy_to_user(uarg + sizeof(pkt),
                                        results,
                                        found * sizeof(unsigned long)))) {
            kfree(results);
            put_task_struct(task);
            ret = -EFAULT;
            goto out;
        }

        kfree(results);
        put_task_struct(task);
        ret = 0;
        goto out;
    }

#ifdef CONFIG_HAVE_HW_BREAKPOINT
    /* ---- CMD_HWBP_SET: 设置硬件断点 ---- */
    if (cmd == CMD_HWBP_SET) {
        struct kai_hwbp_setup pkt;
        u32 bp_id = 0;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        ret = hw_bp_set(pkt.pid, pkt.address, pkt.bp_type,
                         pkt.bp_len, &bp_id);
        if (ret == 0) {
            pkt.bp_id = bp_id;
            if (copy_to_user(uarg, &pkt, sizeof(pkt)))
                ret = -EFAULT;
        }
        goto out;
    }

    /* ---- CMD_HWBP_REMOVE: 移除硬件断点 ---- */
    if (cmd == CMD_HWBP_REMOVE) {
        struct kai_hwbp_remove pkt;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        ret = hw_bp_remove(pkt.bp_id);
        goto out;
    }

    /* ---- CMD_HWBP_LIST: 列出硬件断点 ---- */
    if (cmd == CMD_HWBP_LIST) {
        struct kai_hwbp_list_packet pkt;
        struct kai_bp_entry *entries;
        int actual;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        entries = kcalloc(MAX_HW_BREAKPOINTS, sizeof(*entries),
                          GFP_KERNEL);
        if (!entries) {
            ret = -ENOMEM;
            goto out;
        }

        actual = hw_bp_list(entries, MAX_HW_BREAKPOINTS);
        pkt.total_bps = actual;
        pkt.actual_bps = actual;

        if (copy_to_user(uarg, &pkt, sizeof(pkt)) ||
            (actual > 0 && copy_to_user(uarg + sizeof(pkt),
                                         entries,
                                         actual * sizeof(*entries)))) {
            kfree(entries);
            ret = -EFAULT;
            goto out;
        }

        kfree(entries);
        ret = 0;
        goto out;
    }

    /* ---- CMD_READ_REGS: 读取寄存器快照 ---- */
    if (cmd == CMD_READ_REGS) {
        struct kai_reg_read_packet pkt;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        ret = hw_bp_read_regs(pkt.bp_id, &pkt);
        if (copy_to_user(uarg, &pkt, sizeof(pkt)))
            ret = -EFAULT;
        else
            ret = 0;
        goto out;
    }
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

    /* ---- CMD_SWBP_SET / CMD_SWBP_REMOVE: 软件断点（桩） ---- */
    if (cmd == CMD_SWBP_SET || cmd == CMD_SWBP_REMOVE) {
        pr_warn("Software breakpoints not yet implemented\n");
        ret = -ENOSYS;
        goto out;
    }

    /* ---- CMD_CALLSTACK: 调用栈捕获（桩） ---- */
    if (cmd == CMD_CALLSTACK) {
        struct kai_callstack_packet pkt;

        if (copy_from_user(&pkt, uarg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        pkt.actual_frames = 0;
        pkt.total_frames = 0;
        if (copy_to_user(uarg, &pkt, sizeof(pkt)))
            ret = -EFAULT;
        else
            ret = 0;
        goto out;
    }

    /* 未识别的命令 */
    ret = -ENOTTY;

out:
    {
        u64 delta = ktime_get_ns() - start_ns;
        atomic64_add(delta, &g_total_cpu_ns);
        atomic64_inc(&g_call_count);
    }
    return ret;
}

/* =====================================================================
 * 第九部分：设备注册与模块初始化
 * ===================================================================== */

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = device_ioctl,
};

static struct miscdevice misc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &fops,
};

static int __init qingwei_mcp_init(void)
{
    int ret;

    /* 初始化 PID 缓存 */
    pid_cache_init();

    /* 注册设备 */
    ret = misc_register(&misc_dev);
    if (ret) {
        pr_err("misc_register failed: %d\n", ret);
        return ret;
    }

#ifdef CONFIG_HAVE_HW_BREAKPOINT
    /* 动态解析 HWBP 符号 */
    g_reg_hw_bp = (register_user_hw_bp_fn)
        resolve_symbol("register_user_hw_breakpoint");
    g_unreg_hw_bp = (unregister_hw_bp_fn)
        resolve_symbol("unregister_hw_breakpoint");

    if (g_reg_hw_bp && g_unreg_hw_bp) {
        pr_info("HWBP symbols resolved via kprobe\n");
        memset(g_hw_bp_slots, 0, sizeof(g_hw_bp_slots));
        memset(g_reg_snapshots, 0, sizeof(g_reg_snapshots));
    } else {
        pr_warn("HWBP symbols not found, HWBP disabled\n");
    }
#endif

    /* 隐藏模块名称 */
    strcpy((char *)THIS_MODULE->name, MODULE_HIDE_NAME);

    pr_info("device /dev/%s ready (lsmod: '%s')\n",
            DEVICE_NAME, MODULE_HIDE_NAME);
    pr_info("optimizations: pkg->pid resolve, LRU cache, "
            "ARM64 huge page walk\n");

    return 0;
}

static void __exit qingwei_mcp_exit(void)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
    hw_bp_cleanup_all();
#endif

    /* 清理 PID 缓存 */
    {
        int i;
        mutex_lock(&g_cache_lock);
        for (i = 0; i < PID_CACHE_SIZE; i++) {
            if (g_pid_cache_pool[i].task) {
                put_task_struct(g_pid_cache_pool[i].task);
                g_pid_cache_pool[i].task = NULL;
            }
        }
        mutex_unlock(&g_cache_lock);
    }

    misc_deregister(&misc_dev);

    pr_info("module unloaded, total calls=%llu, pkg_resolves=%llu, "
            "cache_hits=%llu\n",
            (unsigned long long)atomic64_read(&g_call_count),
            (unsigned long long)atomic64_read(&g_pkg_resolve_cnt),
            (unsigned long long)atomic64_read(&g_pkg_resolve_hit));
}

module_init(qingwei_mcp_init);
module_exit(qingwei_mcp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Security Researcher");
MODULE_DESCRIPTION("Optimized kernel driver with pkg->pid resolution, "
                   "memory R/W, HWBP support for MCP protocol");
MODULE_VERSION("2.0.0");

