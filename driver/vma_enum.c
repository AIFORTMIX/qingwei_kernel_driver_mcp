// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_vma: " fmt

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "vma_enum.h"

/*
 * 枚举目标进程的完整 VMA（虚拟内存映射）列表
 */
int qw_list_vmas(struct task_struct *task, kai_vma_list_packet_t *pkt)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    unsigned long addr = 0;
    kai_vma_entry_t *entries = NULL;
    __u32 collected = 0;
    __u32 skip = 0;
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
        if (skip < pkt->offset) {
            skip++;
            addr = vma->vm_end;
            continue;
        }
        if (collected >= pkt->max_count)
            break;

        entries[collected].start = vma->vm_start;
        entries[collected].end = vma->vm_end;
        entries[collected].flags = vma->vm_flags;
        entries[collected].pgoff = vma->vm_pgoff;

        if (vma->vm_file) {
            char *path = d_path(&vma->vm_file->f_path, pathbuf, PAGE_SIZE);
            if (!IS_ERR(path)) {
                strncpy(entries[collected].name, path,
                        sizeof(entries[collected].name) - 1);
                entries[collected].name[sizeof(entries[collected].name) - 1] = '\0';
            } else {
                strncpy(entries[collected].name, "[file]", sizeof(entries[collected].name));
            }
        } else {
            /* 匿名映射命名 */
            if (vma->vm_start == mm->start_brk && vma->vm_end == mm->brk)
                strncpy(entries[collected].name, "[heap]", sizeof(entries[collected].name));
            else if (vma->vm_start == mm->start_stack && vma->vm_end == mm->brk + PAGE_SIZE)
                strncpy(entries[collected].name, "[stack]", sizeof(entries[collected].name));
            else
                strncpy(entries[collected].name, "[anon]", sizeof(entries[collected].name));
        }

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
