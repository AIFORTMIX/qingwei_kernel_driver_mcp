// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_hwbp: " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/version.h>

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <asm/sysreg.h>
#endif

#include "hw_bp_multi.h"

#ifdef CONFIG_HAVE_HW_BREAKPOINT

/* 动态解析未导出的 HWBP 符号 */
typedef struct perf_event *(*register_user_hw_bp_fn)(struct perf_event_attr *attr,
    perf_overflow_handler_t triggered, void *context, struct task_struct *tsk);
typedef void (*unregister_hw_bp_fn)(struct perf_event *bp);

static register_user_hw_bp_fn g_register_user_hw_bp = NULL;
static unregister_hw_bp_fn g_unregister_hw_bp = NULL;

/* 通过 kprobe 解析未导出符号 */
static unsigned long resolve_symbol(const char *name)
{
    struct kprobe kp;
    unsigned long addr;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = name;

    if (register_kprobe(&kp) < 0)
        return 0;

    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    return addr;
}

/* 硬件断点槽位 */
struct hw_bp_slot {
    struct perf_event *event;
    struct task_struct *target_task;
    unsigned long bp_addr;
    u32 bp_type;
    u32 bp_len;
    u32 priority;
    atomic64_t hit_count;
    bool active;
    /* 每个断点独立的快照缓存 */
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
static int g_hwbp_count = 0; /* 硬件支持的断点数量 */

/* 全局变量：检测到的硬件断点和观察点数量 */
static int g_hw_bp_max = 0;
static int g_hw_wp_max = 0;

/* 检测硬件支持的断点数量（同时检测 watchpoint 数量） */
int hw_bp_multi_detect_count(void)
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

    pr_info("HWBP: detected %d hardware breakpoint slots, %d watchpoint slots\n",
            num_bp, num_wp);
    return num_bp;
}

/* 返回当前可用的（未使用的）硬件断点槽位数 */
int hw_bp_get_available_count(void)
{
    int i, used = 0;

    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < g_hw_bp_max; i++) {
        if (g_bp_slots[i].active)
            used++;
    }
    mutex_unlock(&g_hwbp_mgr_lock);

    return g_hw_bp_max - used;
}

/* 返回硬件支持的总断点槽位数 */
int hw_bp_get_total_slots(void)
{
    return g_hw_bp_max;
}

/* 断点使用状态查询 */
int hw_bp_get_usage(qingwei_mcp_bp_usage_t *usage)
{
    int i, used_hw = 0;

    memset(usage, 0, sizeof(*usage));
    usage->total_hw_slots = g_hw_bp_max;
    usage->total_sw_slots = 0; /* 软件断点无限制 */

    mutex_lock(&g_hwbp_mgr_lock);
    for (i = 0; i < QW_MAX_HWBP; i++) {
        if (g_bp_slots[i].active)
            used_hw++;
    }
    mutex_unlock(&g_hwbp_mgr_lock);

    usage->used_hw_slots = used_hw;
    usage->hw_bp_available = g_hw_bp_max - used_hw;

    return 0;
}

/* 断点溢出处理器 */
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

/* 初始化 */
int hw_bp_multi_init(void)
{
    int i;

    g_register_user_hw_bp = (register_user_hw_bp_fn)
        resolve_symbol("register_user_hw_breakpoint");
    g_unregister_hw_bp = (unregister_hw_bp_fn)
        resolve_symbol("unregister_hw_breakpoint");

    if (g_register_user_hw_bp && g_unregister_hw_bp) {
        pr_info("HWBP symbols resolved via kprobe\n");
    } else {
        pr_warn("HWBP symbols not found, HWBP disabled\n");
        return -ENOSYS;
    }

    g_hwbp_count = hw_bp_multi_detect_count();

    for (i = 0; i < QW_MAX_HWBP; i++) {
        memset(&g_bp_slots[i], 0, sizeof(g_bp_slots[i]));
        spin_lock_init(&g_bp_slots[i].snap_lock);
        g_bp_slots[i].snapshots = kcalloc(QW_SNAPSHOT_CACHE_SIZE,
                                           sizeof(struct snapshot_entry),
                                           GFP_KERNEL);
        if (!g_bp_slots[i].snapshots) {
            pr_warn("HWBP: failed to alloc snapshot buffer for slot %d\n", i);
        }
    }

    return 0;
}

/* 清理所有断点 */
void hw_bp_multi_cleanup_all(void)
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

/* 设置硬件断点（内部实现，需持有 g_hwbp_mgr_lock） */
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

    /* 清理旧断点 */
    if (slot->active && slot->event) {
        if (g_unregister_hw_bp)
            g_unregister_hw_bp(slot->event);
        slot->event = NULL;
    }
    if (slot->target_task) {
        put_task_struct(slot->target_task);
        slot->target_task = NULL;
    }

    /* 初始化断点属性 */
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

    slot->event = g_register_user_hw_bp(&attr, hw_bp_overflow_handler,
                                         slot, task);
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

    pr_info("HWBP[%u]: set addr=0x%lx type=%u len=%u pid=%d\n",
            setup->bp_index, setup->bp_addr, setup->bp_type,
            attr.bp_len, task->pid);
    return 0;
}

/* 设置硬件断点（公开接口） */
int hw_bp_multi_set(kai_hwbp_setup_t *setup, struct task_struct *task)
{
    int ret;

    mutex_lock(&g_hwbp_mgr_lock);
    ret = __hw_bp_multi_set_locked(setup, task);
    mutex_unlock(&g_hwbp_mgr_lock);

    return ret;
}

/* 自动槽位分配 */
int hw_bp_multi_set_auto(kai_hwbp_setup_t *setup, struct task_struct *task)
{
    int i, ret;
    int best_slot = -1;
    int best_priority = QW_BP_PRI_HIGH + 1; /* 比最高优先级还高，确保能找到 */

    if (setup->bp_index != 0xFFFFFFFF) {
        /* 指定了槽位号，直接使用原有逻辑 */
        return hw_bp_multi_set(setup, task);
    }

    mutex_lock(&g_hwbp_mgr_lock);

    /* 自动寻找第一个空闲槽位，优先使用低优先级槽位 */
    for (i = 0; i < g_hw_bp_max; i++) {
        if (!g_bp_slots[i].active) {
            /* 空闲槽位，检查其优先级（空闲槽位优先级为0） */
            if (best_slot == -1) {
                best_slot = i;
                break; /* 第一个空闲槽位即可 */
            }
        }
    }

    /* 如果没有空闲槽位，尝试替换低优先级槽位 */
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

    /* 如果要替换的槽位有旧断点，先清理 */
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

/* 移除硬件断点 */
int hw_bp_multi_remove(kai_hwbp_remove_t *remove)
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

/* 列出所有断点 */
int hw_bp_multi_list(kai_hwbp_list_packet_t *pkt)
{
    kai_hwbp_info_t *infos;
    u32 count = 0;
    int i;
    int ret = 0;

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

/* 旧版兼容：设置单断点（使用槽位 0） */
int hw_bp_legacy_setup(struct task_struct *task, unsigned long bp_addr,
                        unsigned long blr_x8, unsigned long blr_x9)
{
    kai_hwbp_setup_t setup;
    memset(&setup, 0, sizeof(setup));
    setup.bp_index = 0;
    setup.bp_addr = bp_addr;
    setup.bp_type = 0; /* execute */
    setup.bp_len = HW_BREAKPOINT_LEN_4;
    setup.pid = task->pid;
    return hw_bp_multi_set(&setup, task);
}

void hw_bp_legacy_cleanup(void)
{
    kai_hwbp_remove_t remove;
    remove.bp_index = 0;
    hw_bp_multi_remove(&remove);
}

int hw_bp_query_snapshot(unsigned long obj_addr,
                          u32 *x_raw, u32 *y_raw, u32 *z_raw)
{
    struct hw_bp_slot *slot = &g_bp_slots[0]; /* 旧版使用槽位 0 */
    int i, found = -2;

    spin_lock(&slot->snap_lock);
    for (i = 0; i < slot->snap_count; i++) {
        int idx = (slot->snap_head - 1 - i + QW_SNAPSHOT_CACHE_SIZE)
                  % QW_SNAPSHOT_CACHE_SIZE;
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

int hw_bp_get_stats(hwbp_stats_t *stats)
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

#endif /* CONFIG_HAVE_HW_BREAKPOINT */
