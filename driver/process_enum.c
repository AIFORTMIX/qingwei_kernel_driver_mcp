// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_enum: " fmt

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "process_enum.h"
#include "proc_lookup.h"

/*
 * 枚举系统中所有进程
 * 支持分页（offset + max_count）
 */
int qw_list_processes(kai_proc_list_packet_t *pkt)
{
    struct task_struct *task;
    kai_proc_info_t *infos = NULL;
    __u32 total = 0;
    __u32 collected = 0;
    __u32 idx = 0;
    int ret = 0;

    if (!pkt || pkt->max_count == 0 || pkt->max_count > 1024)
        return -EINVAL;

    /* 先统计总数 */
    rcu_read_lock();
    for_each_process(task)
        total++;
    rcu_read_unlock();

    pkt->total_count = total;

    if (pkt->offset >= total) {
        pkt->actual_count = 0;
        return 0;
    }

    /* 分配输出缓冲区 */
    infos = kcalloc(pkt->max_count, sizeof(kai_proc_info_t), GFP_KERNEL);
    if (!infos)
        return -ENOMEM;

    rcu_read_lock();
    for_each_process(task) {
        if (idx < pkt->offset) {
            idx++;
            continue;
        }
        if (collected >= pkt->max_count)
            break;

        infos[collected].pid = task->pid;
        infos[collected].ppid = task->real_parent ? task->real_parent->pid : 0;
        infos[collected].tgid = task->tgid;
        strncpy(infos[collected].comm, task->comm, sizeof(infos[collected].comm) - 1);
        infos[collected].comm[sizeof(infos[collected].comm) - 1] = '\0';
        infos[collected].state = task->__state;

        /* 统计线程数 */
        {
            struct task_struct *t;
            int tc = 0;
            if (task->signal) {
                tc = atomic_read(&task->signal->live);
            }
            infos[collected].thread_count = tc;
        }

        /* 读取 cmdline */
        if (task->mm) {
            qw_extract_cmdline(task, infos[collected].cmdline,
                                     sizeof(infos[collected].cmdline));
        } else {
            infos[collected].cmdline[0] = '\0';
        }

        collected++;
        idx++;
    }
    rcu_read_unlock();

    pkt->actual_count = collected;

    /* 拷贝到用户态 */
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
