// SPDX-License-Identifier: GPL-2.0
/*
 * qingwei_mcp - AI-callable kernel driver with IDA-like capabilities
 * Single-file build: all modules merged into one translation unit.
 */
#define pr_fmt(fmt) "qingwei_mcp: " fmt

/* ==================== Kernel Headers ==================== */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
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
#include <asm/pgtable.h>

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <asm/sysreg.h>
#endif

/* ==================== Data Structures & ioctl Defs ==================== */

#define QINGWEI_DEVICE_NAME    "qingwei_mcp"
#define QW_MODULE_HIDE        "vfat"
#define QINGWEI_IOCTL_MAGIC    'M'

/* --- Legacy structs (backward compat) --- */
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

/* --- ioctl 1-10 (legacy) --- */
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

/* --- Process info --- */
typedef struct {
    __s32 pid;
    __s32 ppid;
    __s32 tgid;
    char comm[16];
    char cmdline[256];
    unsigned long state;
    __s32 thread_count;
} kai_proc_info_t;

typedef struct {
    __u32 max_count;
    __u32 actual_count;
    __u32 total_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_proc_list_packet_t;

/* --- Module/library info --- */
typedef struct {
    unsigned long base_addr;
    unsigned long end_addr;
    unsigned long size;
    __u32 flags;
    unsigned long offset;
    char path[256];
    __u8 is_executable;
} kai_module_info_t;

typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 max_count;
    __u32 actual_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_module_list_packet_t;

/* --- VMA entries --- */
typedef struct {
    unsigned long start;
    unsigned long end;
    __u32 flags;
    unsigned long pgoff;
    char name[128];
} kai_vma_entry_t;

typedef struct {
    __s32 pid;
    char pkg_name[64];
    __u32 max_count;
    __u32 actual_count;
    __u32 offset;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_vma_list_packet_t;

/* --- Register read --- */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long bp_id;
    __u64 regs[31];
    unsigned long sp;
    unsigned long pc;
    unsigned long pstate;
    __u32 valid_mask;
} kai_reg_read_packet_t;

/* --- Hardware breakpoints --- */
#define QW_MAX_HWBP 16

typedef struct {
    __u32 bp_index;
    unsigned long bp_addr;
    __u32 bp_type;
    __u32 bp_len;
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

/* --- Software breakpoints (kprobes) --- */
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

/* --- Memory search --- */
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

/* --- Callstack --- */
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

/* --- Thread enum --- */
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

/* --- Raw read --- */
typedef struct {
    __s32 pid;
    char pkg_name[64];
    unsigned long addr;
    __u32 size;
    unsigned long out_buf;
    size_t out_buf_size;
} kai_raw_read_packet_t;

/* --- Breakpoint usage --- */
typedef struct {
    __u32 total_hw_slots;
    __u32 used_hw_slots;
    __u32 total_sw_slots;
    __u32 used_sw_slots;
    __u32 hw_bp_available;
} qingwei_mcp_bp_usage_t;

/* --- ioctl 11-24 (new) --- */
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

/* --- Constants --- */
#define QW_BATCH_MAX_ITEMS    512
#define QW_BATCH_MAX_SIZE     (2 * 1024 * 1024)
#define QW_MAX_SEARCH_RANGE   (64 * 1024 * 1024)
#define QW_SNAPSHOT_CACHE_SIZE 256

enum kai_match_mode {
    KAI_MATCH_EXACT = 0,
    KAI_MATCH_SUBSTRING = 1,
    KAI_MATCH_PREFIX = 2,
};

/* --- ARM64 huge page detection --- */
#ifndef pud_leaf
#define pud_leaf(pud)   pud_sect(pud)
#endif
#ifndef pmd_leaf
#define pmd_leaf(pmd)   pmd_sect(pmd)
#endif

/* --- HWBP priority --- */
enum qw_bp_priority {
    QW_BP_PRI_LOW = 0,
    QW_BP_PRI_NORMAL = 1,
    QW_BP_PRI_HIGH = 2,
};

/* ==================== Forward Declarations ==================== */
static int __manual_read_memory_locked(struct mm_struct *mm, unsigned long vaddr,
                                       void *kbuf, size_t len);
static int manual_read_memory_fn(struct task_struct *task, unsigned long vaddr,
                                 void *kbuf, size_t len);
static int manual_write_memory(struct task_struct *task, unsigned long vaddr,
                               void *kbuf, size_t len);

/* ==================== proc_lookup ==================== */

static int qw_extract_cmdline(struct task_struct *task, char *buf, size_t buf_size)
{
    struct mm_struct *mm;
    size_t len;
    long ret;
    size_t i;

    if (!buf || buf_size == 0)
        return -EINVAL;

    mm = get_task_mm(task);
    if (!mm)
        return -EINVAL;

    if (!mm->arg_start || !mm->arg_end || mm->arg_end <= mm->arg_start) {
        mmput(mm);
        return -ENOENT;
    }

    len = min_t(size_t, buf_size - 1, mm->arg_end - mm->arg_start);
    ret = strncpy_from_user(buf, (char __user *)mm->arg_start, len);
    mmput(mm);

    if (ret <= 0)
        return -EFAULT;

    buf[ret] = '\0';
    for (i = 0; i < (size_t)ret; i++) {
        if (buf[i] == '\0')
            break;
    }
    buf[i] = '\0';
    return 0;
}

static int qw_find_pid_by_pkgname(const char *pkg_name, enum kai_match_mode mode)
{
    struct task_struct *task;
    int pid = -ESRCH;
    char cmdline_buf[256];

    if (!pkg_name || !*pkg_name)
        return -EINVAL;

    rcu_read_lock();
    for_each_process(task) {
        if (!task->mm)
            continue;
        if (qw_extract_cmdline(task, cmdline_buf, sizeof(cmdline_buf)) < 0)
            continue;

        switch (mode) {
        case KAI_MATCH_EXACT:
            if (strcmp(cmdline_buf, pkg_name) == 0) {
                pid = task->pid;
                goto found;
            }
            break;
        case KAI_MATCH_SUBSTRING:
            if (strstr(cmdline_buf, pkg_name)) {
                pid = task->pid;
                goto found;
            }
            break;
        case KAI_MATCH_PREFIX:
            if (strncmp(cmdline_buf, pkg_name, strlen(pkg_name)) == 0) {
                pid = task->pid;
                goto found;
            }
            break;
        }
    }
found:
    rcu_read_unlock();
    return pid;
}

/* ==================== process_enum ==================== */

static int qw_list_processes(kai_proc_list_packet_t *pkt)
{
    struct task_struct *task;
    kai_proc_info_t *infos = NULL;
    __u32 total = 0, collected = 0, idx = 0;
    int ret = 0;

    if (!pkt || pkt->max_count == 0 || pkt->max_count > 1024)
        return -EINVAL;

    rcu_read_lock();
    for_each_process(task)
        total++;
    rcu_read_unlock();

    pkt->total_count = total;
    if (pkt->offset >= total) {
        pkt->actual_count = 0;
        return 0;
    }

    infos = kcalloc(pkt->max_count, sizeof(kai_proc_info_t), GFP_KERNEL);
    if (!infos)
        return -ENOMEM;

    rcu_read_lock();
    for_each_process(task) {
        if (idx < pkt->offset) { idx++; continue; }
        if (collected >= pkt->max_count) break;

        infos[collected].pid = task->pid;
        infos[collected].ppid = task->real_parent ? task->real_parent->pid : 0;
        infos[collected].tgid = task->tgid;
        strncpy(infos[collected].comm, task->comm, sizeof(infos[collected].comm) - 1);
        infos[collected].comm[sizeof(infos[collected].comm) - 1] = '\0';
        infos[collected].state = task->__state;
        {
            int tc = 0;
            if (task->signal)
                tc = atomic_read(&task->signal->live);
            infos[collected].thread_count = tc;
        }
        if (task->mm)
            qw_extract_cmdline(task, infos[collected].cmdline, sizeof(infos[collected].cmdline));
        else
            infos[collected].cmdline[0] = '\0';

        collected++;
        idx++;
    }
    rcu_read_unlock();

    pkt->actual_count = collected;
    if (pkt->out_buf && pkt->out_buf_size >= collected * sizeof(kai_proc_info_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, infos,
                         collected * sizeof(kai_proc_info_t)))
            ret = -EFAULT;
    } else if (collected > 0) {
        ret = -ENOSPC;
    }

    kfree(infos);
    return ret;
}

/* ==================== module_enum ==================== */

static int qw_list_modules(struct task_struct *task, kai_module_list_packet_t *pkt)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    unsigned long addr = 0;
    kai_module_info_t *infos = NULL;
    __u32 collected = 0;
    char *pathbuf;
    int ret = 0;

    if (!mm || !pkt || pkt->max_count == 0 || pkt->max_count > 2048)
        return -EINVAL;

    infos = kcalloc(pkt->max_count, sizeof(kai_module_info_t), GFP_KERNEL);
    pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!infos || !pathbuf) {
        kfree(infos);
        kfree(pathbuf);
        return -ENOMEM;
    }

    mmap_read_lock(mm);
    while ((vma = find_vma(mm, addr)) != NULL && collected < pkt->max_count) {
        if (vma->vm_file) {
            char *path = d_path(&vma->vm_file->f_path, pathbuf, PAGE_SIZE);
            if (!IS_ERR(path)) {
                bool dup = false;
                __u32 j;
                for (j = 0; j < collected; j++) {
                    if (strcmp(infos[j].path, path) == 0) { dup = true; break; }
                }
                if (!dup) {
                    infos[collected].base_addr = vma->vm_start;
                    infos[collected].end_addr = vma->vm_end;
                    infos[collected].size = vma->vm_end - vma->vm_start;
                    infos[collected].flags = vma->vm_flags;
                    infos[collected].offset = vma->vm_pgoff << PAGE_SHIFT;
                    strncpy(infos[collected].path, path, sizeof(infos[collected].path) - 1);
                    infos[collected].path[sizeof(infos[collected].path) - 1] = '\0';
                    infos[collected].is_executable = (vma->vm_flags & VM_EXEC) ? 1 : 0;
                    collected++;
                }
            }
        }
        addr = vma->vm_end;
    }
    mmap_read_unlock(mm);

    pkt->actual_count = collected;
    if (pkt->out_buf && pkt->out_buf_size >= collected * sizeof(kai_module_info_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, infos,
                         collected * sizeof(kai_module_info_t)))
            ret = -EFAULT;
    } else if (collected > 0) {
        ret = -ENOSPC;
    }

    kfree(infos);
    kfree(pathbuf);
    return ret;
}

/* ==================== vma_enum ==================== */

static int qw_list_vmas(struct task_struct *task, kai_vma_list_packet_t *pkt)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    unsigned long addr = 0;
    kai_vma_entry_t *entries = NULL;
    __u32 collected = 0, skip = 0;
    char *pathbuf;
    int ret = 0;

    if (!mm || !pkt || pkt->max_count == 0 || pkt->max_count > 4096)
        return -EINVAL;

    entries = kcalloc(pkt->max_count, sizeof(kai_vma_entry_t), GFP_KERNEL);
    pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!entries || !pathbuf) {
        kfree(entries);
        kfree(pathbuf);
        return -ENOMEM;
    }

    mmap_read_lock(mm);
    while ((vma = find_vma(mm, addr)) != NULL) {
        if (skip < pkt->offset) { skip++; addr = vma->vm_end; continue; }
        if (collected >= pkt->max_count) break;

        entries[collected].start = vma->vm_start;
        entries[collected].end = vma->vm_end;
        entries[collected].flags = vma->vm_flags;
        entries[collected].pgoff = vma->vm_pgoff;

        if (vma->vm_file) {
            char *path = d_path(&vma->vm_file->f_path, pathbuf, PAGE_SIZE);
            if (!IS_ERR(path))
                strncpy(entries[collected].name, path, sizeof(entries[collected].name) - 1);
            else
                strncpy(entries[collected].name, "[file]", sizeof(entries[collected].name));
        } else {
            if (vma->vm_start == mm->start_brk && vma->vm_end == mm->brk)
                strncpy(entries[collected].name, "[heap]", sizeof(entries[collected].name));
            else if (vma->vm_start == mm->start_stack && vma->vm_end == mm->brk + PAGE_SIZE)
                strncpy(entries[collected].name, "[stack]", sizeof(entries[collected].name));
            else
                strncpy(entries[collected].name, "[anon]", sizeof(entries[collected].name));
        }
        entries[collected].name[sizeof(entries[collected].name) - 1] = '\0';

        collected++;
        addr = vma->vm_end;
    }
    mmap_read_unlock(mm);

    pkt->actual_count = collected;
    if (pkt->out_buf && pkt->out_buf_size >= collected * sizeof(kai_vma_entry_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, entries,
                         collected * sizeof(kai_vma_entry_t)))
            ret = -EFAULT;
    } else if (collected > 0) {
        ret = -ENOSPC;
    }

    kfree(entries);
    kfree(pathbuf);
    return ret;
}

/* ==================== sw_bp (Software Breakpoints) ==================== */

struct sw_bp_entry {
    struct kprobe kp;
    u32 id;
    int target_pid;
    unsigned long addr;
    atomic64_t hit_count;
    struct pt_regs last_regs;
    bool regs_valid;
    spinlock_t lock;
    struct list_head list;
};

static LIST_HEAD(g_sw_bp_list);
static DEFINE_MUTEX(g_sw_bp_lock);
static atomic_t g_sw_bp_id_counter = ATOMIC_INIT(1);

static int sw_bp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    struct sw_bp_entry *entry = container_of(p, struct sw_bp_entry, kp);

    spin_lock(&entry->lock);
    memcpy(&entry->last_regs, regs, sizeof(struct pt_regs));
    entry->regs_valid = true;
    spin_unlock(&entry->lock);

    atomic64_inc(&entry->hit_count);
    return 0;
}

static struct sw_bp_entry *find_sw_entry_by_id(u32 id)
{
    struct sw_bp_entry *entry;
    list_for_each_entry(entry, &g_sw_bp_list, list) {
        if (entry->id == id)
            return entry;
    }
    return NULL;
}

static int sw_bp_set(kai_swbp_setup_t *setup, struct task_struct *task)
{
    struct sw_bp_entry *entry;
    int ret;

    if (!setup || !task)
        return -EINVAL;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;

    entry->id = atomic_inc_return(&g_sw_bp_id_counter);
    entry->target_pid = task->pid;
    entry->addr = setup->addr;
    entry->regs_valid = false;
    atomic64_set(&entry->hit_count, 0);
    spin_lock_init(&entry->lock);
    INIT_LIST_HEAD(&entry->list);

    entry->kp.addr = (void *)setup->addr;
    entry->kp.pre_handler = sw_bp_pre_handler;

    ret = register_kprobe(&entry->kp);
    if (ret < 0) {
        pr_err("SWBP: register_kprobe failed at 0x%lx: %d\n", setup->addr, ret);
        kfree(entry);
        return ret;
    }

    setup->sw_bp_id = entry->id;
    mutex_lock(&g_sw_bp_lock);
    list_add_tail(&entry->list, &g_sw_bp_list);
    mutex_unlock(&g_sw_bp_lock);

    pr_info("SWBP: set id=%u addr=0x%lx pid=%d\n", entry->id, setup->addr, task->pid);
    return 0;
}

static int sw_bp_remove_fn(kai_swbp_remove_t *remove)
{
    struct sw_bp_entry *entry;

    if (!remove)
        return -EINVAL;

    mutex_lock(&g_sw_bp_lock);
    entry = find_sw_entry_by_id(remove->sw_bp_id);
    if (!entry) {
        mutex_unlock(&g_sw_bp_lock);
        return -ENOENT;
    }
    list_del(&entry->list);
    mutex_unlock(&g_sw_bp_lock);

    unregister_kprobe(&entry->kp);
    kfree(entry);
    pr_info("SWBP: removed id=%u\n", remove->sw_bp_id);
    return 0;
}

static int sw_bp_read_regs(u32 sw_bp_id, kai_reg_read_packet_t *pkt)
{
    struct sw_bp_entry *entry;
    int ret = -ENOENT;

    mutex_lock(&g_sw_bp_lock);
    entry = find_sw_entry_by_id(sw_bp_id);
    if (entry) {
        spin_lock(&entry->lock);
        if (entry->regs_valid) {
#ifdef CONFIG_ARM64
            memcpy(pkt->regs, entry->last_regs.regs, sizeof(pkt->regs));
            pkt->sp = entry->last_regs.sp;
            pkt->pc = entry->last_regs.pc;
            pkt->pstate = entry->last_regs.pstate;
#endif
            pkt->valid_mask = 0xFFFFFFFF;
            ret = 0;
        }
        spin_unlock(&entry->lock);
    }
    mutex_unlock(&g_sw_bp_lock);
    return ret;
}

static int sw_bp_get_count(void)
{
    struct sw_bp_entry *entry;
    int count = 0;
    mutex_lock(&g_sw_bp_lock);
    list_for_each_entry(entry, &g_sw_bp_list, list)
        count++;
    mutex_unlock(&g_sw_bp_lock);
    return count;
}

static void sw_bp_cleanup_all(void)
{
    struct sw_bp_entry *entry, *tmp;
    mutex_lock(&g_sw_bp_lock);
    list_for_each_entry_safe(entry, tmp, &g_sw_bp_list, list) {
        list_del(&entry->list);
        unregister_kprobe(&entry->kp);
        kfree(entry);
    }
    mutex_unlock(&g_sw_bp_lock);
}

/* ==================== hw_bp_multi (Hardware Breakpoints) ==================== */

#ifdef CONFIG_HAVE_HW_BREAKPOINT

typedef struct perf_event *(*register_user_hw_bp_fn)(struct perf_event_attr *attr,
    perf_overflow_handler_t triggered, void *context, struct task_struct *tsk);
typedef void (*unregister_hw_bp_fn)(struct perf_event *bp);

static register_user_hw_bp_fn g_register_user_hw_bp = NULL;
static unregister_hw_bp_fn g_unregister_hw_bp = NULL;

static unsigned long resolve_symbol(const char *name)
{
    struct kprobe kp;
    unsigned long addr;
    int ret;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = name;

    pr_info("HWBP: resolving symbol '%s' via kprobe...\n", name);
    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_warn("HWBP: failed to resolve symbol '%s' (ret=%d)\n", name, ret);
        return 0;
    }

    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);

    if (addr < PAGE_SIZE) {
        pr_warn("HWBP: resolved address 0x%lx for '%s' is invalid (too low)\n", addr, name);
        return 0;
    }

    pr_info("HWBP: resolved %s -> 0x%lx\n", name, addr);
    return addr;
}

struct hw_bp_slot {
    struct perf_event *event;
    struct task_struct *target_task;
    unsigned long bp_addr;
    u32 bp_type;
    u32 bp_len;
    u32 priority;
    atomic64_t hit_count;
    bool active;
    struct snapshot_entry {
        unsigned long obj_addr;
        u32 x_raw, y_raw, z_raw;
        unsigned long jiffies_val;
    } *snapshots;
    int snap_head;
    int snap_count;
    spinlock_t snap_lock;
};

static struct hw_bp_slot g_bp_slots[QW_MAX_HWBP];
static DEFINE_MUTEX(g_hwbp_mgr_lock);
static int g_hwbp_count = 0;
static int g_hw_bp_max = 0;
static int g_hw_wp_max = 0;

static int hw_bp_multi_detect_count(void)
{
    u64 dfr0;
    int num_bp, num_wp;

    dfr0 = read_sysreg(id_aa64dfr0_el1);
    num_bp = ((dfr0 >> 12) & 0xF) + 1;
    num_wp = ((dfr0 >> 20) & 0xF) + 1;
    if (num_bp > QW_MAX_HWBP)
        num_bp = QW_MAX_HWBP;

    g_hw_bp_max = num_bp;
    g_hw_wp_max = num_wp;
    pr_info("HWBP: detected %d HW BP slots, %d WP slots\n", num_bp, num_wp);
    return num_bp;
}

static int hw_bp_get_usage(qingwei_mcp_bp_usage_t *usage)
{
    int i, used_hw = 0;
    memset(usage, 0, sizeof(*usage));
    usage->total_hw_slots = g_hw_bp_max;
    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < QW_MAX_HWBP; i++)
        if (g_bp_slots[i].active)
            used_hw++;
    mutex_unlock(&g_hwbp_mgr_lock);
    usage->used_hw_slots = used_hw;
    usage->hw_bp_available = g_hw_bp_max - used_hw;
    return 0;
}

static void hw_bp_overflow_handler(struct perf_event *event,
                                    struct perf_sample_data *data,
                                    struct pt_regs *regs)
{
    struct hw_bp_slot *slot = event->overflow_handler_context;
    unsigned long obj_addr;
    u32 raw_x, raw_y, raw_z;

    if (!regs || !slot)
        return;

    obj_addr = regs->regs[19];
    raw_x = (u32)regs->regs[8];
    raw_y = (u32)regs->regs[9];
    raw_z = (u32)regs->regs[10];

    if (obj_addr < 0x100000)
        return;
    if (raw_x == 0 && raw_y == 0 && raw_z == 0)
        return;

    spin_lock(&slot->snap_lock);
    if (slot->snapshots) {
        slot->snapshots[slot->snap_head].obj_addr = obj_addr;
        slot->snapshots[slot->snap_head].x_raw = raw_x;
        slot->snapshots[slot->snap_head].y_raw = raw_y;
        slot->snapshots[slot->snap_head].z_raw = raw_z;
        slot->snapshots[slot->snap_head].jiffies_val = jiffies;
        slot->snap_head = (slot->snap_head + 1) % QW_SNAPSHOT_CACHE_SIZE;
        if (slot->snap_count < QW_SNAPSHOT_CACHE_SIZE)
            slot->snap_count++;
    }
    spin_unlock(&slot->snap_lock);

    atomic64_inc(&slot->hit_count);
}

static int hw_bp_multi_init(void)
{
    int i;

    pr_info("HWBP: init step 1 - resolving register_user_hw_breakpoint\n");
    g_register_user_hw_bp = (register_user_hw_bp_fn)
        resolve_symbol("register_user_hw_breakpoint");
    pr_info("HWBP: init step 2 - resolving unregister_hw_breakpoint\n");
    g_unregister_hw_bp = (unregister_hw_bp_fn)
        resolve_symbol("unregister_hw_breakpoint");

    if (g_register_user_hw_bp && g_unregister_hw_bp) {
        pr_info("HWBP: init step 3 - symbols resolved OK\n");
    } else {
        pr_warn("HWBP: init step 3 - symbols not found, HWBP disabled\n");
        return -ENOSYS;
    }

    pr_info("HWBP: init step 4 - detecting hardware breakpoint count\n");
    g_hwbp_count = hw_bp_multi_detect_count();

    pr_info("HWBP: init step 5 - allocating snapshot buffers\n");
    for (i = 0; i < QW_MAX_HWBP; i++) {
        memset(&g_bp_slots[i], 0, sizeof(g_bp_slots[i]));
        spin_lock_init(&g_bp_slots[i].snap_lock);
        g_bp_slots[i].snapshots = kcalloc(QW_SNAPSHOT_CACHE_SIZE,
                                           sizeof(struct snapshot_entry), GFP_KERNEL);
        if (!g_bp_slots[i].snapshots)
            pr_warn("HWBP: failed to alloc snapshot buffer for slot %d\n", i);
    }

    pr_info("HWBP: init complete - %d slots ready\n", QW_MAX_HWBP);
    return 0;
}

static void hw_bp_multi_cleanup_all(void)
{
    int i;
    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < QW_MAX_HWBP; i++) {
        if (g_bp_slots[i].active && g_bp_slots[i].event) {
            if (g_unregister_hw_bp)
                g_unregister_hw_bp(g_bp_slots[i].event);
            g_bp_slots[i].event = NULL;
        }
        if (g_bp_slots[i].target_task) {
            put_task_struct(g_bp_slots[i].target_task);
            g_bp_slots[i].target_task = NULL;
        }
        g_bp_slots[i].active = false;
        kfree(g_bp_slots[i].snapshots);
        g_bp_slots[i].snapshots = NULL;
    }
    mutex_unlock(&g_hwbp_mgr_lock);
}

static int __hw_bp_multi_set_locked(kai_hwbp_setup_t *setup, struct task_struct *task)
{
    struct perf_event_attr attr;
    struct hw_bp_slot *slot;
    int err;

    if (!g_register_user_hw_bp)
        return -ENOSYS;
    if (setup->bp_index >= QW_MAX_HWBP || setup->bp_index >= (u32)g_hwbp_count)
        return -EINVAL;

    slot = &g_bp_slots[setup->bp_index];

    if (slot->active && slot->event) {
        if (g_unregister_hw_bp)
            g_unregister_hw_bp(slot->event);
        slot->event = NULL;
    }
    if (slot->target_task) {
        put_task_struct(slot->target_task);
        slot->target_task = NULL;
    }

    hw_breakpoint_init(&attr);
    attr.bp_addr = setup->bp_addr;
    attr.bp_len = setup->bp_len ? setup->bp_len : HW_BREAKPOINT_LEN_4;

    switch (setup->bp_type) {
    case 0: attr.bp_type = HW_BREAKPOINT_X; break;
    case 1: attr.bp_type = HW_BREAKPOINT_R; break;
    case 2: attr.bp_type = HW_BREAKPOINT_W; break;
    case 3: attr.bp_type = HW_BREAKPOINT_RW; break;
    default: attr.bp_type = HW_BREAKPOINT_X; break;
    }

    slot->event = g_register_user_hw_bp(&attr, hw_bp_overflow_handler, slot, task);
    if (IS_ERR(slot->event)) {
        err = PTR_ERR(slot->event);
        slot->event = NULL;
        pr_err("HWBP[%u]: register failed: %d\n", setup->bp_index, err);
        return err;
    }

    slot->target_task = task;
    get_task_struct(task);
    slot->bp_addr = setup->bp_addr;
    slot->bp_type = setup->bp_type;
    slot->bp_len = attr.bp_len;
    slot->priority = QW_BP_PRI_NORMAL;
    slot->active = true;
    atomic64_set(&slot->hit_count, 0);

    spin_lock(&slot->snap_lock);
    slot->snap_head = 0;
    slot->snap_count = 0;
    spin_unlock(&slot->snap_lock);

    pr_info("HWBP[%u]: set addr=0x%lx type=%u len=%llu pid=%d\n",
            setup->bp_index, setup->bp_addr, setup->bp_type, attr.bp_len, task->pid);
    return 0;
}

static int hw_bp_multi_set(kai_hwbp_setup_t *setup, struct task_struct *task)
{
    int ret;
    mutex_lock(&g_hwbp_mgr_lock);
    ret = __hw_bp_multi_set_locked(setup, task);
    mutex_unlock(&g_hwbp_mgr_lock);
    return ret;
}

static int hw_bp_multi_set_auto(kai_hwbp_setup_t *setup, struct task_struct *task)
{
    int i, ret;
    int best_slot = -1;
    int best_priority = QW_BP_PRI_HIGH + 1;

    if (setup->bp_index != 0xFFFFFFFF)
        return hw_bp_multi_set(setup, task);

    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < g_hw_bp_max; i++) {
        if (!g_bp_slots[i].active) {
            best_slot = i;
            break;
        }
    }
    if (best_slot == -1) {
        for (i = 0; i < g_hw_bp_max; i++) {
            if (g_bp_slots[i].active &&
                g_bp_slots[i].priority < best_priority &&
                g_bp_slots[i].priority == QW_BP_PRI_LOW) {
                best_slot = i;
                best_priority = g_bp_slots[i].priority;
            }
        }
    }
    if (best_slot == -1) {
        mutex_unlock(&g_hwbp_mgr_lock);
        return -ENOSPC;
    }

    setup->bp_index = best_slot;
    if (g_bp_slots[best_slot].active && g_bp_slots[best_slot].event) {
        pr_info("HWBP[%d]: auto-replacing low priority breakpoint\n", best_slot);
        if (g_unregister_hw_bp)
            g_unregister_hw_bp(g_bp_slots[best_slot].event);
        g_bp_slots[best_slot].event = NULL;
        if (g_bp_slots[best_slot].target_task) {
            put_task_struct(g_bp_slots[best_slot].target_task);
            g_bp_slots[best_slot].target_task = NULL;
        }
        g_bp_slots[best_slot].active = false;
    }

    ret = __hw_bp_multi_set_locked(setup, task);
    mutex_unlock(&g_hwbp_mgr_lock);
    return ret;
}

static int hw_bp_multi_remove(kai_hwbp_remove_t *remove)
{
    struct hw_bp_slot *slot;
    if (remove->bp_index >= QW_MAX_HWBP)
        return -EINVAL;

    mutex_lock(&g_hwbp_mgr_lock);
    slot = &g_bp_slots[remove->bp_index];
    if (!slot->active) {
        mutex_unlock(&g_hwbp_mgr_lock);
        return -ENOENT;
    }
    if (slot->event && g_unregister_hw_bp)
        g_unregister_hw_bp(slot->event);
    slot->event = NULL;
    if (slot->target_task)
        put_task_struct(slot->target_task);
    slot->target_task = NULL;
    slot->active = false;
    mutex_unlock(&g_hwbp_mgr_lock);

    pr_info("HWBP[%u]: removed\n", remove->bp_index);
    return 0;
}

static int hw_bp_multi_list(kai_hwbp_list_packet_t *pkt)
{
    kai_hwbp_info_t *infos;
    u32 count = 0;
    int i, ret = 0;

    if (!pkt)
        return -EINVAL;

    infos = kcalloc(QW_MAX_HWBP, sizeof(kai_hwbp_info_t), GFP_KERNEL);
    if (!infos)
        return -ENOMEM;

    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < QW_MAX_HWBP; i++) {
        if (g_bp_slots[i].active) {
            infos[count].bp_index = i;
            infos[count].bp_addr = g_bp_slots[i].bp_addr;
            infos[count].bp_type = g_bp_slots[i].bp_type;
            infos[count].bp_len = g_bp_slots[i].bp_len;
            infos[count].hit_count = atomic64_read(&g_bp_slots[i].hit_count);
            infos[count].active = 1;
            count++;
        }
    }
    mutex_unlock(&g_hwbp_mgr_lock);

    pkt->count = count;
    if (pkt->out_buf && pkt->out_buf_size >= count * sizeof(kai_hwbp_info_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, infos,
                         count * sizeof(kai_hwbp_info_t)))
            ret = -EFAULT;
    }

    kfree(infos);
    return ret;
}

static int hw_bp_legacy_setup(struct task_struct *task, unsigned long bp_addr,
                               unsigned long blr_x8, unsigned long blr_x9)
{
    kai_hwbp_setup_t setup;
    memset(&setup, 0, sizeof(setup));
    setup.bp_index = 0;
    setup.bp_addr = bp_addr;
    setup.bp_type = 0;
    setup.bp_len = HW_BREAKPOINT_LEN_4;
    setup.pid = task->pid;
    return hw_bp_multi_set(&setup, task);
}

static int hw_bp_query_snapshot(unsigned long obj_addr,
                                 u32 *x_raw, u32 *y_raw, u32 *z_raw)
{
    struct hw_bp_slot *slot = &g_bp_slots[0];
    int i, found = -2;

    spin_lock(&slot->snap_lock);
    for (i = 0; i < slot->snap_count; i++) {
        int idx = (slot->snap_head - 1 - i + QW_SNAPSHOT_CACHE_SIZE) % QW_SNAPSHOT_CACHE_SIZE;
        if (slot->snapshots && slot->snapshots[idx].obj_addr == obj_addr) {
            *x_raw = slot->snapshots[idx].x_raw;
            *y_raw = slot->snapshots[idx].y_raw;
            *z_raw = slot->snapshots[idx].z_raw;
            found = 0;
            break;
        }
    }
    spin_unlock(&slot->snap_lock);
    return found;
}

static int hw_bp_get_stats(hwbp_stats_t *stats)
{
    struct hw_bp_slot *slot = &g_bp_slots[0];
    memset(stats, 0, sizeof(*stats));
    stats->hit_count = atomic64_read(&slot->hit_count);
    if (slot->event)
        stats->bp_addr = slot->event->attr.bp_addr;
    stats->snapshot_count = slot->snap_count;
    stats->active = slot->active ? 1 : 0;
    return 0;
}

#else /* !CONFIG_HAVE_HW_BREAKPOINT */

static int hw_bp_multi_init(void) { return 0; }
static void hw_bp_multi_cleanup_all(void) {}
static int hw_bp_multi_set_auto(kai_hwbp_setup_t *s, struct task_struct *t) { return -ENOSYS; }
static int hw_bp_multi_remove(kai_hwbp_remove_t *r) { return -ENOSYS; }
static int hw_bp_multi_list(kai_hwbp_list_packet_t *p) { return -ENOSYS; }
static int hw_bp_get_usage(qingwei_mcp_bp_usage_t *u) { memset(u, 0, sizeof(*u)); return 0; }
static int hw_bp_legacy_setup(struct task_struct *t, unsigned long a,
                               unsigned long x, unsigned long y) { return -ENOSYS; }
static int hw_bp_query_snapshot(unsigned long a, u32 *x, u32 *y, u32 *z) { return -ENOSYS; }
static int hw_bp_get_stats(hwbp_stats_t *s) { memset(s, 0, sizeof(*s)); return 0; }

#endif /* CONFIG_HAVE_HW_BREAKPOINT */

/* ==================== mem_search ==================== */

static int qw_mem_search(struct task_struct *task, kai_mem_search_packet_t *pkt)
{
    struct mm_struct *mm = task->mm;
    unsigned long *results = NULL;
    u8 *read_buf = NULL;
    unsigned long addr;
    u32 found = 0;
    int ret = 0;

    if (!mm || !pkt)
        return -EINVAL;
    if (pkt->pattern_len == 0 || pkt->pattern_len > 256)
        return -EINVAL;
    if (pkt->max_results == 0 || pkt->max_results > 1024)
        return -EINVAL;
    if (pkt->start_addr >= pkt->end_addr)
        return -EINVAL;
    if (pkt->end_addr - pkt->start_addr > QW_MAX_SEARCH_RANGE)
        return -EINVAL;

    results = kcalloc(pkt->max_results, sizeof(unsigned long), GFP_KERNEL);
    read_buf = vmalloc(PAGE_SIZE * 2);
    if (!results || !read_buf) {
        kfree(results);
        vfree(read_buf);
        return -ENOMEM;
    }

    mmap_read_lock(mm);
    for (addr = pkt->start_addr;
         addr < pkt->end_addr && found < pkt->max_results;
         addr += PAGE_SIZE) {

        size_t read_size = min_t(size_t, PAGE_SIZE * 2, pkt->end_addr - addr);
        size_t scan_end;
        size_t i;
        int r;

        r = manual_read_memory_fn(task, addr, read_buf, read_size);
        if (r <= 0)
            continue;

        scan_end = r - pkt->pattern_len;
        for (i = 0; i <= scan_end && found < pkt->max_results; i++) {
            bool match = true;
            u32 j;
            for (j = 0; j < pkt->pattern_len; j++) {
                if (pkt->mask[j] &&
                    (read_buf[i + j] & pkt->mask[j]) !=
                    (pkt->pattern[j] & pkt->mask[j])) {
                    match = false;
                    break;
                }
            }
            if (match)
                results[found++] = addr + i;
        }
    }
    mmap_read_unlock(mm);

    pkt->found_count = found;
    if (pkt->results_buf && pkt->results_buf_size >= found * sizeof(unsigned long)) {
        if (copy_to_user((void __user *)pkt->results_buf, results,
                         found * sizeof(unsigned long)))
            ret = -EFAULT;
    } else if (found > 0) {
        ret = -ENOSPC;
    }

    kfree(results);
    vfree(read_buf);
    return ret;
}

/* ==================== callstack ==================== */

static int qw_capture_callstack(struct task_struct *task, kai_callstack_packet_t *pkt)
{
    kai_stack_frame_t *frames = NULL;
    kai_reg_read_packet_t reg_pkt;
    unsigned long fp;
    u32 count = 0;
    int ret = 0;

    if (!pkt || pkt->max_frames == 0 || pkt->max_frames > 128)
        return -EINVAL;

    frames = kcalloc(pkt->max_frames, sizeof(kai_stack_frame_t), GFP_KERNEL);
    if (!frames)
        return -ENOMEM;

    if (pkt->bp_id > 0) {
        memset(&reg_pkt, 0, sizeof(reg_pkt));
        ret = sw_bp_read_regs(pkt->bp_id, &reg_pkt);
        if (ret == 0 && reg_pkt.valid_mask) {
            frames[count].pc = reg_pkt.pc;
            frames[count].sp = reg_pkt.sp;
            frames[count].fp = reg_pkt.regs[29];
            frames[count].lr = reg_pkt.regs[30];
            snprintf(frames[count].symbol, sizeof(frames[count].symbol), "pc:0x%lx", reg_pkt.pc);
            count++;
            fp = reg_pkt.regs[29];
        } else {
            pkt->frame_count = 0;
            kfree(frames);
            return -ENOENT;
        }
    } else {
        pkt->frame_count = 0;
        kfree(frames);
        return -EINVAL;
    }

    while (count < pkt->max_frames && fp && fp > 0x1000) {
        unsigned long frame[2];
        ret = manual_read_memory_fn(task, fp, frame, sizeof(frame));
        if (ret < (int)sizeof(frame))
            break;

        frames[count].fp = frame[0];
        frames[count].lr = frame[1];
        frames[count].pc = frame[1];
        frames[count].sp = fp + 16;
        snprintf(frames[count].symbol, sizeof(frames[count].symbol), "lr:0x%lx", frame[1]);
        count++;

        if (frame[0] <= fp)
            break;
        fp = frame[0];
    }

    pkt->frame_count = count;
    if (pkt->out_buf && pkt->out_buf_size >= count * sizeof(kai_stack_frame_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, frames,
                         count * sizeof(kai_stack_frame_t)))
            ret = -EFAULT;
        else
            ret = 0;
    } else if (count > 0) {
        ret = -ENOSPC;
    }

    kfree(frames);
    return ret;
}

/* ==================== thread_enum ==================== */

static int qw_list_threads(struct task_struct *task, kai_thread_list_packet_t *pkt)
{
    struct task_struct *t;
    kai_thread_info_t *infos = NULL;
    __u32 collected = 0;
    int ret = 0;

    if (!pkt || pkt->max_count == 0 || pkt->max_count > 4096)
        return -EINVAL;

    infos = kcalloc(pkt->max_count, sizeof(kai_thread_info_t), GFP_KERNEL);
    if (!infos)
        return -ENOMEM;

    rcu_read_lock();
    for_each_thread(task, t) {
        if (collected >= pkt->max_count)
            break;

        infos[collected].tid = t->pid;
        strncpy(infos[collected].comm, t->comm, sizeof(infos[collected].comm) - 1);
        infos[collected].comm[sizeof(infos[collected].comm) - 1] = '\0';
        infos[collected].state = t->__state;
        {
            struct pt_regs *thread_regs = task_pt_regs(t);
            if (thread_regs) {
                infos[collected].stack_ptr = thread_regs->sp;
                infos[collected].pc = thread_regs->pc;
            }
        }
        collected++;
    }
    rcu_read_unlock();

    pkt->actual_count = collected;
    if (pkt->out_buf && pkt->out_buf_size >= collected * sizeof(kai_thread_info_t)) {
        if (copy_to_user((void __user *)pkt->out_buf, infos,
                         collected * sizeof(kai_thread_info_t)))
            ret = -EFAULT;
    } else if (collected > 0) {
        ret = -ENOSPC;
    }

    kfree(infos);
    return ret;
}

/* ==================== Memory Read/Write Core ==================== */

static int __manual_read_memory_locked(struct mm_struct *mm, unsigned long vaddr,
                                       void *kbuf, size_t len)
{
    size_t done = 0;
    int ret = 0;
    while (done < len) {
        unsigned long addr = vaddr + done;
        unsigned long remaining = len - done;
        unsigned long page_offset = addr & ~PAGE_MASK;
        size_t copy_size = min_t(size_t, remaining, PAGE_SIZE - page_offset);
        pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
        struct page *page; void *kmap_addr; unsigned long pfn;

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
            memcpy(kbuf + done, kmap_addr + page_offset, copy_size);
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
            memcpy(kbuf + done, kmap_addr + page_offset, copy_size);
            kunmap_local(kmap_addr);
            done += copy_size;
            continue;
        }

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
    return ret ? ret : done;
}

static int manual_read_memory_fn(struct task_struct *task, unsigned long vaddr,
                                 void *kbuf, size_t len)
{
    struct mm_struct *mm = task->mm;
    int ret;
    if (!mm) return -EINVAL;
    mmap_read_lock(mm);
    ret = __manual_read_memory_locked(mm, vaddr, kbuf, len);
    mmap_read_unlock(mm);
    return ret;
}

static int manual_write_memory(struct task_struct *task, unsigned long vaddr,
                               void *kbuf, size_t len)
{
    struct mm_struct *mm = task->mm;
    size_t done = 0;
    int ret = 0;
    if (!mm) return -EINVAL;

    mmap_read_lock(mm);
    while (done < len) {
        unsigned long addr = vaddr + done;
        unsigned long remaining = len - done;
        unsigned long page_offset = addr & ~PAGE_MASK;
        size_t copy_size = min_t(size_t, remaining, PAGE_SIZE - page_offset);
        pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
        struct page *page; void *kmap_addr; unsigned long pfn;

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
    return ret ? ret : done;
}

/* ==================== Task Cache & Helpers ==================== */

static DEFINE_MUTEX(g_task_cache_lock);
static struct task_struct *g_cached_task;
static int g_cached_pid = -1;
static char g_cached_name[64];

static atomic64_t g_total_cpu_ns = ATOMIC64_INIT(0);
static atomic64_t g_call_count = ATOMIC64_INIT(0);

static void clear_task_cache_locked(void)
{
    if (g_cached_task) {
        put_task_struct(g_cached_task);
        g_cached_task = NULL;
    }
    g_cached_pid = -1;
    g_cached_name[0] = '\0';
}

static struct task_struct *get_cached_task_for_target(int pid, const char *pkg_name)
{
    struct task_struct *task = NULL;
    int target_pid = pid;

    mutex_lock(&g_task_cache_lock);
    if (g_cached_task && g_cached_pid > 0) {
        if ((target_pid > 0 && target_pid == g_cached_pid) ||
            (target_pid <= 0 && pkg_name && pkg_name[0] &&
             strncmp(g_cached_name, pkg_name, sizeof(g_cached_name)) == 0)) {
            if (pid_alive(g_cached_task) && g_cached_task->mm) {
                get_task_struct(g_cached_task);
                mutex_unlock(&g_task_cache_lock);
                return g_cached_task;
            }
            clear_task_cache_locked();
        }
    }
    mutex_unlock(&g_task_cache_lock);

    if (target_pid <= 0 && pkg_name && pkg_name[0]) {
        target_pid = qw_find_pid_by_pkgname(pkg_name, KAI_MATCH_SUBSTRING);
        if (target_pid <= 0)
            return NULL;
    }
    if (target_pid <= 0)
        return NULL;

    rcu_read_lock();
    task = find_task_by_vpid(target_pid);
    if (task)
        get_task_struct(task);
    rcu_read_unlock();
    if (!task)
        return NULL;

    mutex_lock(&g_task_cache_lock);
    clear_task_cache_locked();
    g_cached_task = task;
    get_task_struct(g_cached_task);
    g_cached_pid = target_pid;
    if (pkg_name && pkg_name[0]) {
        strncpy(g_cached_name, pkg_name, sizeof(g_cached_name) - 1);
        g_cached_name[sizeof(g_cached_name) - 1] = '\0';
    } else {
        g_cached_name[0] = '\0';
    }
    mutex_unlock(&g_task_cache_lock);

    return task;
}

static unsigned long get_module_base(struct task_struct *task, const char *mod_name)
{
    struct mm_struct *mm = task->mm;
    unsigned long base = 0, addr = 0;
    struct vm_area_struct *vma;
    char *pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
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

/* ==================== ioctl Handler ==================== */

static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    u64 start = ktime_get_ns();
    long ret = 0;

    /* === Commands without process context === */

    if (cmd == CMD_GET_MODULE_INFO) {
        module_info_t info;
        info.cpu_time_ns = atomic64_read(&g_total_cpu_ns);
        info.call_count = atomic64_read(&g_call_count);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
        info.mem_bytes = THIS_MODULE->core_layout.size;
#else
        info.mem_bytes = 0;
#endif
        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            ret = -EFAULT;
        goto out;
    }

    if (cmd == CMD_GET_HWBP_STATS) {
        hwbp_stats_t stats;
        hw_bp_get_stats(&stats);
        if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
            ret = -EFAULT;
        goto out;
    }

    if (cmd == CMD_GET_BP_USAGE) {
        qingwei_mcp_bp_usage_t usage;
        hw_bp_get_usage(&usage);
        usage.used_sw_slots = sw_bp_get_count();
        if (copy_to_user((void __user *)arg, &usage, sizeof(usage)))
            ret = -EFAULT;
        goto out;
    }

    if (cmd == CMD_LIST_PROCS) {
        kai_proc_list_packet_t pkt;
        if (copy_from_user(&pkt, (void __user *)arg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }
        ret = qw_list_processes(&pkt);
        if (ret == 0 && copy_to_user((void __user *)arg, &pkt, sizeof(pkt)))
            ret = -EFAULT;
        goto out;
    }

    /* === Commands requiring process context === */
    {
        mem_packet_t pkt;
        void *kbuf = NULL;
        struct task_struct *task;
        unsigned long final_addr;
        uint64_t ptr_val;

        if (cmd == CMD_READ_BATCH) {
            mem_batch_packet_t bpkt;
            mem_batch_item_t *items = NULL;
            void *out_buf = NULL;
            uint32_t i;

            if (copy_from_user(&bpkt, (void __user *)arg, sizeof(bpkt))) { ret = -EFAULT; goto out; }
            if (bpkt.count == 0 || bpkt.count > QW_BATCH_MAX_ITEMS ||
                bpkt.item_size != sizeof(mem_batch_item_t) ||
                bpkt.out_size == 0 || bpkt.out_size > QW_BATCH_MAX_SIZE) {
                ret = -EINVAL; goto out;
            }

            task = get_cached_task_for_target(bpkt.pid, bpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }

            items = kcalloc(bpkt.count, sizeof(mem_batch_item_t), GFP_KERNEL);
            out_buf = vmalloc(bpkt.out_size);
            if (!items || !out_buf) { ret = -ENOMEM; goto batch_out; }
            if (copy_from_user(items, (void __user *)bpkt.items_buf,
                               bpkt.count * sizeof(mem_batch_item_t))) {
                ret = -EFAULT; goto batch_out;
            }

            if (task->mm) {
                mmap_read_lock(task->mm);
                for (i = 0; i < bpkt.count; i++) {
                    size_t end = (size_t)items[i].out_offset + (size_t)items[i].size;
                    items[i].status = -EINVAL;
                    if (items[i].size == 0 || items[i].size > PAGE_SIZE || end > bpkt.out_size)
                        continue;
                    int r = __manual_read_memory_locked(task->mm, items[i].addr,
                                                        (char *)out_buf + items[i].out_offset,
                                                        items[i].size);
                    items[i].status = (r > 0) ? 0 : r;
                }
                mmap_read_unlock(task->mm);
            } else {
                ret = -EINVAL;
            }

            if (copy_to_user((void __user *)bpkt.items_buf, items,
                             bpkt.count * sizeof(mem_batch_item_t)) ||
                copy_to_user((void __user *)bpkt.out_buf, out_buf, bpkt.out_size))
                ret = -EFAULT;
            else
                ret = 0;

batch_out:
            if (task)
                put_task_struct(task);
            kfree(items);
            vfree(out_buf);
            goto out;
        }

        if (cmd == CMD_HWBP_SET) {
            kai_hwbp_setup_t setup;
            if (copy_from_user(&setup, (void __user *)arg, sizeof(setup))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(setup.pid, setup.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = hw_bp_multi_set_auto(&setup, task);
            if (ret == 0 && copy_to_user((void __user *)arg, &setup, sizeof(setup)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_HWBP_REMOVE) {
            kai_hwbp_remove_t remove;
            if (copy_from_user(&remove, (void __user *)arg, sizeof(remove))) { ret = -EFAULT; goto out; }
            ret = hw_bp_multi_remove(&remove);
            goto out;
        }

        if (cmd == CMD_HWBP_LIST) {
            kai_hwbp_list_packet_t lpkt;
            if (copy_from_user(&lpkt, (void __user *)arg, sizeof(lpkt))) { ret = -EFAULT; goto out; }
            ret = hw_bp_multi_list(&lpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &lpkt, sizeof(lpkt)))
                ret = -EFAULT;
            goto out;
        }

        if (cmd == CMD_SWBP_SET) {
            kai_swbp_setup_t setup;
            if (copy_from_user(&setup, (void __user *)arg, sizeof(setup))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(setup.pid, setup.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = sw_bp_set(&setup, task);
            if (ret == 0 && copy_to_user((void __user *)arg, &setup, sizeof(setup)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_SWBP_REMOVE) {
            kai_swbp_remove_t remove;
            if (copy_from_user(&remove, (void __user *)arg, sizeof(remove))) { ret = -EFAULT; goto out; }
            ret = sw_bp_remove_fn(&remove);
            goto out;
        }

        if (cmd == CMD_READ_REGS) {
            kai_reg_read_packet_t reg_pkt;
            if (copy_from_user(&reg_pkt, (void __user *)arg, sizeof(reg_pkt))) { ret = -EFAULT; goto out; }
            ret = sw_bp_read_regs(reg_pkt.bp_id, &reg_pkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &reg_pkt, sizeof(reg_pkt)))
                ret = -EFAULT;
            goto out;
        }

        if (cmd == CMD_MEM_SEARCH) {
            kai_mem_search_packet_t spkt;
            if (copy_from_user(&spkt, (void __user *)arg, sizeof(spkt))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(spkt.pid, spkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_mem_search(task, &spkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &spkt, sizeof(spkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_CALLSTACK) {
            kai_callstack_packet_t cpkt;
            if (copy_from_user(&cpkt, (void __user *)arg, sizeof(cpkt))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(cpkt.pid, cpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_capture_callstack(task, &cpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &cpkt, sizeof(cpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_LIST_THREADS) {
            kai_thread_list_packet_t tpkt;
            if (copy_from_user(&tpkt, (void __user *)arg, sizeof(tpkt))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(tpkt.pid, tpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_threads(task, &tpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &tpkt, sizeof(tpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_LIST_MODULES) {
            kai_module_list_packet_t mpkt;
            if (copy_from_user(&mpkt, (void __user *)arg, sizeof(mpkt))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(mpkt.pid, mpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_modules(task, &mpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &mpkt, sizeof(mpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_LIST_VMAS) {
            kai_vma_list_packet_t vpkt;
            if (copy_from_user(&vpkt, (void __user *)arg, sizeof(vpkt))) { ret = -EFAULT; goto out; }
            task = get_cached_task_for_target(vpkt.pid, vpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_vmas(task, &vpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &vpkt, sizeof(vpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        if (cmd == CMD_RAW_READ) {
            kai_raw_read_packet_t rpkt;
            void *rbuf;
            if (copy_from_user(&rpkt, (void __user *)arg, sizeof(rpkt))) { ret = -EFAULT; goto out; }
            if (rpkt.size == 0 || rpkt.size > 0x100000) { ret = -EINVAL; goto out; }
            task = get_cached_task_for_target(rpkt.pid, rpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            rbuf = kmalloc(rpkt.size, GFP_KERNEL);
            if (!rbuf) { put_task_struct(task); ret = -ENOMEM; goto out; }
            ret = manual_read_memory_fn(task, rpkt.addr, rbuf, rpkt.size);
            if (ret > 0) {
                if (copy_to_user((void __user *)rpkt.out_buf, rbuf, ret))
                    ret = -EFAULT;
                else
                    ret = 0;
            }
            kfree(rbuf);
            put_task_struct(task);
            goto out;
        }

        /* === Legacy commands (backward compat) === */
        if (copy_from_user(&pkt, (void __user *)arg, sizeof(pkt))) { ret = -EFAULT; goto out; }

        task = get_cached_task_for_target(pkt.pid, pkt.pkg_name);
        if (!task) { ret = -ESRCH; goto out; }

        switch (cmd) {
        case CMD_GET_BASE: {
            char *mod_name = (char *)pkt.user_buf;
            char namebuf[256] = {0};
            if (!mod_name) { ret = -EINVAL; break; }
            if (copy_from_user(namebuf, (void __user *)mod_name, sizeof(namebuf)-1)) {
                ret = -EFAULT; break;
            }
            unsigned long base = get_module_base(task, namebuf);
            if (copy_to_user((void __user *)pkt.user_buf, &base, sizeof(unsigned long)))
                ret = -EFAULT;
            break;
        }

        case CMD_READ_MEM:
        case CMD_READ_PTR: {
            size_t total = pkt.size;
            if (total == 0 || total > 0x1000000) { ret = -EINVAL; break; }
            kbuf = kmalloc(total, GFP_KERNEL);
            if (!kbuf) { ret = -ENOMEM; break; }

            final_addr = pkt.addr;
            if (cmd == CMD_READ_PTR && pkt.offset_count > 0) {
                int i;
                for (i = 0; i < pkt.offset_count; i++) {
                    unsigned long cur_addr = final_addr + (i == 0 ? 0 : pkt.offsets[i-1]);
                    ret = manual_read_memory_fn(task, cur_addr, &ptr_val, sizeof(uint64_t));
                    if (ret < 0) break;
                    if (i == pkt.offset_count - 1)
                        final_addr = ptr_val + pkt.offsets[i];
                    else
                        final_addr = ptr_val;
                }
                if (ret < 0) break;
            } else {
                if (pkt.offset_count > 0 && pkt.offsets[0] != 0)
                    final_addr += pkt.offsets[0];
            }

            ret = manual_read_memory_fn(task, final_addr, kbuf, total);
            if (ret > 0 && copy_to_user((void __user *)pkt.user_buf, kbuf, total))
                ret = -EFAULT;
            else if (ret > 0)
                ret = 0;
            break;
        }

        case CMD_WRITE_MEM: {
            size_t total = pkt.size;
            if (total == 0 || total > 0x1000000) { ret = -EINVAL; break; }
            kbuf = kmalloc(total, GFP_KERNEL);
            if (!kbuf) { ret = -ENOMEM; break; }
            if (copy_from_user(kbuf, (void __user *)pkt.user_buf, total)) {
                ret = -EFAULT; break;
            }
            final_addr = pkt.addr;
            if (pkt.offset_count > 0 && pkt.offsets[0] != 0)
                final_addr += pkt.offsets[0];
            ret = manual_write_memory(task, final_addr, kbuf, total);
            if (ret > 0) ret = 0;
            break;
        }

        case CMD_SET_HW_BP: {
            unsigned long bp_addr = pkt.addr;
            unsigned long blr_addrs[2] = {0, 0};
            if (pkt.user_buf && copy_from_user(blr_addrs,
                    (void __user *)pkt.user_buf, sizeof(blr_addrs))) {
                ret = -EFAULT; break;
            }
            ret = hw_bp_legacy_setup(task, bp_addr, blr_addrs[0], blr_addrs[1]);
            break;
        }

        case CMD_SET_BLR_ADDRS:
            ret = 0;
            break;

        case CMD_QUERY_SNAPSHOT: {
            unsigned long obj_addr = pkt.addr;
            u32 result[3] = {0, 0, 0};
            int status;
            status = hw_bp_query_snapshot(obj_addr, &result[0], &result[1], &result[2]);
            if (pkt.user_buf && copy_to_user((void __user *)pkt.user_buf,
                    result, sizeof(result)))
                ret = -EFAULT;
            else
                ret = status;
            break;
        }

        default:
            ret = -ENOTTY;
        }

        if (task)
            put_task_struct(task);
        kfree(kbuf);
    }

out:
    {
        u64 delta = ktime_get_ns() - start;
        atomic64_add(delta, &g_total_cpu_ns);
        atomic64_inc(&g_call_count);
    }
    return ret;
}

/* ==================== Device & Module ==================== */

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = device_ioctl,
};

static struct miscdevice misc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = QINGWEI_DEVICE_NAME,
    .fops = &fops,
};

static int __init qingwei_mcp_init(void)
{
    int ret;

    pr_info("init step 1 - registering misc device\n");
    ret = misc_register(&misc_dev);
    if (ret) {
        pr_err("misc_register failed (ret=%d)\n", ret);
        return ret;
    }
    pr_info("init step 2 - misc device registered OK\n");

    pr_info("init step 3 - initializing HWBP subsystem\n");
    ret = hw_bp_multi_init();
    if (ret < 0)
        pr_warn("HWBP init failed (ret=%d), hardware breakpoints disabled\n", ret);
    else
        pr_info("init step 4 - HWBP initialized OK\n");

    /* 动态解析 copy_to_kernel_nofault（GKI 内核可能未导出此符号） */
    pr_info("init step 5 - attempting module name hide\n");
    {
        typedef long (*copy_to_kernel_nofault_fn)(void *, const void *, size_t);
        struct kprobe kp_hide;
        copy_to_kernel_nofault_fn fn;

        memset(&kp_hide, 0, sizeof(kp_hide));
        kp_hide.symbol_name = "copy_to_kernel_nofault";
        if (register_kprobe(&kp_hide) == 0) {
            fn = (copy_to_kernel_nofault_fn)kp_hide.addr;
            unregister_kprobe(&kp_hide);
            if (fn((char *)THIS_MODULE->name, QW_MODULE_HIDE,
                   strlen(QW_MODULE_HIDE) + 1) == 0) {
                pr_info("init step 6 - module hidden as '%s'\n", QW_MODULE_HIDE);
            } else {
                pr_info("init step 6 - name hide failed (read-only memory)\n");
            }
        } else {
            pr_info("init step 6 - name hide skipped (symbol not exported)\n");
        }
    }

    pr_info("qingwei_mcp driver loaded with %d ioctl commands (1-24)\n", 24);
    return 0;
}

static void __exit qingwei_mcp_exit(void)
{
    hw_bp_multi_cleanup_all();
    sw_bp_cleanup_all();
    mutex_lock(&g_task_cache_lock);
    clear_task_cache_locked();
    mutex_unlock(&g_task_cache_lock);
    misc_deregister(&misc_dev);
    pr_info("qingwei_mcp driver unloaded\n");
}

module_init(qingwei_mcp_init);
module_exit(qingwei_mcp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelAI Team");
MODULE_DESCRIPTION("qingwei_mcp: AI-callable kernel driver with IDA-like capabilities");
