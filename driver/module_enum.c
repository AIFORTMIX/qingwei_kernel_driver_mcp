// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_mod: " fmt

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "module_enum.h"

/*
 * 枚举目标进程加载的所有模块/共享库
 * 遍历 VMA 列表，提取有文件映射的条目
 */
int qw_list_modules(struct task_struct *task, kai_module_list_packet_t *pkt)
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
                /* 跳过已收集的相同路径（去重） */
                bool dup = false;
                __u32 j;
                for (j = 0; j < collected; j++) {
                    if (strcmp(infos[j].path, path) == 0) {
                        dup = true;
                        break;
                    }
                }

                if (!dup) {
                    infos[collected].base_addr = vma->vm_start;
                    infos[collected].end_addr = vma->vm_end;
                    infos[collected].size = vma->vm_end - vma->vm_start;
                    infos[collected].flags = vma->vm_flags;
                    infos[collected].offset = vma->vm_pgoff << PAGE_SHIFT;
                    strncpy(infos[collected].path, path,
                            sizeof(infos[collected].path) - 1);
                    infos[collected].path[sizeof(infos[collected].path) - 1] = '\0';
                    infos[collected].is_executable =
                        (vma->vm_flags & VM_EXEC) ? 1 : 0;
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
