// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_search: " fmt

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "mem_search.h"

/* 内存读写函数声明（在 main 中定义） */
extern int manual_read_memory(struct task_struct *task, unsigned long vaddr,
                              void *kbuf, size_t len);

/*
 * 在目标进程地址空间中搜索字节模式
 * 支持通配符掩码：mask[i] = 0xFF 精确匹配，0x00 忽略
 * 逐页扫描，支持跨页匹配
 */
int qw_mem_search(struct task_struct *task, kai_mem_search_packet_t *pkt)
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
    read_buf = vmalloc(PAGE_SIZE * 2); /* 双页缓冲，处理跨页 */
    if (!results || !read_buf) {
        kfree(results);
        vfree(read_buf);
        return -ENOMEM;
    }

    mmap_read_lock(mm);

    for (addr = pkt->start_addr;
         addr < pkt->end_addr && found < pkt->max_results;
         addr += PAGE_SIZE) {

        size_t read_size = min_t(size_t, PAGE_SIZE * 2,
                                  pkt->end_addr - addr);
        size_t scan_end;
        size_t i;
        int r;

        r = manual_read_memory(task, addr, read_buf, read_size);
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

            if (match) {
                results[found++] = addr + i;
            }
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
