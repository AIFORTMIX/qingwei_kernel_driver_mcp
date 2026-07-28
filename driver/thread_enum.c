// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_thr: " fmt

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "thread_enum.h"

/*
 * 枚举目标进程的所有线程
 */
int qw_list_threads(struct task_struct *task, kai_thread_list_packet_t *pkt)
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

    /* 遍历线程组 */
    rcu_read_lock();
    for_each_thread(task, t) {
        if (collected >= pkt->max_count)
            break;

        infos[collected].tid = t->pid;
        strncpy(infos[collected].comm, t->comm, sizeof(infos[collected].comm) - 1);
        infos[collected].comm[sizeof(infos[collected].comm) - 1] = '\0';
        infos[collected].state = t->__state;

        /* 获取线程的栈指针和 PC */
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
