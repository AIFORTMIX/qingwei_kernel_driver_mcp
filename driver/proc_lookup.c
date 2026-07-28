// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_proc: " fmt

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "proc_lookup.h"

/*
 * 从 task_struct 读取 cmdline 第一个 token
 * 等价于读取 /proc/<pid>/cmdline 并以 \0 截断
 */
int qw_extract_cmdline(struct task_struct *task, char *buf, size_t buf_size)
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

    /* cmdline 以 \0 分隔参数，截断到第一个 \0（即第一个 token） */
    for (i = 0; i < (size_t)ret; i++) {
        if (buf[i] == '\0')
            break;
    }
    buf[i] = '\0';
    return 0;
}

/*
 * 通过包名查找 PID
 * 遍历所有进程，读取 cmdline 第一个 token 进行匹配
 * 支持精确匹配、子串匹配、前缀匹配三种模式
 */
int qw_find_pid_by_pkgname(const char *pkg_name, enum kai_match_mode mode)
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
