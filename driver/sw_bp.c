// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_swbp: " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include "sw_bp.h"

struct sw_bp_entry {
    struct kprobe kp;
    u32 id;
    int target_pid;
    unsigned long addr;
    atomic64_t hit_count;
    /* 触发时的寄存器快照 */
    struct pt_regs last_regs;
    bool regs_valid;
    spinlock_t lock;
    struct list_head list;
};

static LIST_HEAD(g_sw_bp_list);
static DEFINE_MUTEX(g_sw_bp_lock);
static atomic_t g_sw_bp_id_counter = ATOMIC_INIT(1);

/* kprobe 预处理器：捕获寄存器状态 */
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

static struct sw_bp_entry *find_entry_by_id(u32 id)
{
    struct sw_bp_entry *entry;

    list_for_each_entry(entry, &g_sw_bp_list, list) {
        if (entry->id == id)
            return entry;
    }
    return NULL;
}

int sw_bp_set(kai_swbp_setup_t *setup, struct task_struct *task)
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

    pr_info("SWBP: set id=%u addr=0x%lx pid=%d\n",
            entry->id, setup->addr, task->pid);
    return 0;
}

int sw_bp_remove(kai_swbp_remove_t *remove)
{
    struct sw_bp_entry *entry;

    if (!remove)
        return -EINVAL;

    mutex_lock(&g_sw_bp_lock);
    entry = find_entry_by_id(remove->sw_bp_id);
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

int sw_bp_read_regs(u32 sw_bp_id, kai_reg_read_packet_t *pkt)
{
    struct sw_bp_entry *entry;
    int ret = -ENOENT;

    mutex_lock(&g_sw_bp_lock);
    entry = find_entry_by_id(sw_bp_id);
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

int sw_bp_get_count(void)
{
    struct sw_bp_entry *entry;
    int count = 0;

    mutex_lock(&g_sw_bp_lock);
    list_for_each_entry(entry, &g_sw_bp_list, list)
        count++;
    mutex_unlock(&g_sw_bp_lock);

    return count;
}

void sw_bp_cleanup_all(void)
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
