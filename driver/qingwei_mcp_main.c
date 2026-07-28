// SPDX-License-Identifier: GPL-2.0
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
#include <linux/vmalloc.h>
#include <linux/ktime.h>
#include <linux/kprobes.h>
#include <asm/pgtable.h>

#include "qingwei_mcp.h"
#include "proc_lookup.h"
#include "process_enum.h"
#include "module_enum.h"
#include "vma_enum.h"
#include "hw_bp_multi.h"
#include "sw_bp.h"
#include "mem_search.h"
#include "callstack.h"
#include "thread_enum.h"

/* ---------- ARM64 巨页检测宏 ---------- */
#ifndef pud_leaf
#define pud_leaf(pud)   pud_sect(pud)
#endif
#ifndef pmd_leaf
#define pmd_leaf(pmd)   pmd_sect(pmd)
#endif

/* ---------- 全局变量 ---------- */
static DEFINE_MUTEX(g_task_cache_lock);
static struct task_struct *g_cached_task;
static int g_cached_pid = -1;
static char g_cached_name[64];

static atomic64_t g_total_cpu_ns = ATOMIC64_INIT(0);
static atomic64_t g_call_count = ATOMIC64_INIT(0);

/* ---------- 底层内存读取（已持锁版本） ---------- */
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

/* ---------- 外层读（自动加锁） ---------- */
int manual_read_memory(struct task_struct *task, unsigned long vaddr,
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

/* ---------- 手动页表遍历写 ---------- */
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

/* ---------- 获取模块基址 ---------- */
static unsigned long get_module_base(struct task_struct *task, const char *mod_name)
{
    struct mm_struct *mm = task->mm;
    unsigned long base = 0;
    unsigned long addr = 0;
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

/* ---------- 任务缓存 ---------- */
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

    /* 使用新的 /proc 包名查找 */
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

/* ---------- ioctl 处理 ---------- */
static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    u64 start = ktime_get_ns();
    long ret = 0;

    /* === 无需进程上下文的命令 === */

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
        /* 将更新后的 pkt 写回（包含 actual_count, total_count） */
        if (ret == 0 && copy_to_user((void __user *)arg, &pkt, sizeof(pkt)))
            ret = -EFAULT;
        goto out;
    }

    /* === 需要进程上下文的命令 === */
    {
        mem_packet_t pkt;
        void *kbuf = NULL;
        struct task_struct *task;
        unsigned long final_addr;
        uint64_t ptr_val;

        /* 批量读取 */
        if (cmd == CMD_READ_BATCH) {
            mem_batch_packet_t bpkt;
            mem_batch_item_t *items = NULL;
            void *out_buf = NULL;
            uint32_t i;

            if (copy_from_user(&bpkt, (void __user *)arg, sizeof(bpkt))) {
                ret = -EFAULT;
                goto out;
            }
            if (bpkt.count == 0 || bpkt.count > QW_BATCH_MAX_ITEMS ||
                bpkt.item_size != sizeof(mem_batch_item_t) ||
                bpkt.out_size == 0 || bpkt.out_size > QW_BATCH_MAX_SIZE) {
                ret = -EINVAL;
                goto out;
            }

            task = get_cached_task_for_target(bpkt.pid, bpkt.pkg_name);
            if (!task) {
                ret = -ESRCH;
                goto out;
            }

            items = kcalloc(bpkt.count, sizeof(mem_batch_item_t), GFP_KERNEL);
            out_buf = vmalloc(bpkt.out_size);
            if (!items || !out_buf) {
                ret = -ENOMEM;
                goto batch_out;
            }
            if (copy_from_user(items, (void __user *)bpkt.items_buf,
                               bpkt.count * sizeof(mem_batch_item_t))) {
                ret = -EFAULT;
                goto batch_out;
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
                copy_to_user((void __user *)bpkt.out_buf, out_buf, bpkt.out_size)) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }

batch_out:
            if (task)
                put_task_struct(task);
            kfree(items);
            vfree(out_buf);
            goto out;
        }

        /* 多硬件断点命令 */
        if (cmd == CMD_HWBP_SET) {
            kai_hwbp_setup_t setup;
            if (copy_from_user(&setup, (void __user *)arg, sizeof(setup))) {
                ret = -EFAULT;
                goto out;
            }
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
            if (copy_from_user(&remove, (void __user *)arg, sizeof(remove))) {
                ret = -EFAULT;
                goto out;
            }
            ret = hw_bp_multi_remove(&remove);
            goto out;
        }

        if (cmd == CMD_HWBP_LIST) {
            kai_hwbp_list_packet_t lpkt;
            if (copy_from_user(&lpkt, (void __user *)arg, sizeof(lpkt))) {
                ret = -EFAULT;
                goto out;
            }
            ret = hw_bp_multi_list(&lpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &lpkt, sizeof(lpkt)))
                ret = -EFAULT;
            goto out;
        }

        /* 软件断点命令 */
        if (cmd == CMD_SWBP_SET) {
            kai_swbp_setup_t setup;
            if (copy_from_user(&setup, (void __user *)arg, sizeof(setup))) {
                ret = -EFAULT;
                goto out;
            }
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
            if (copy_from_user(&remove, (void __user *)arg, sizeof(remove))) {
                ret = -EFAULT;
                goto out;
            }
            ret = sw_bp_remove(&remove);
            goto out;
        }

        /* 寄存器读取 */
        if (cmd == CMD_READ_REGS) {
            kai_reg_read_packet_t reg_pkt;
            if (copy_from_user(&reg_pkt, (void __user *)arg, sizeof(reg_pkt))) {
                ret = -EFAULT;
                goto out;
            }
            ret = sw_bp_read_regs(reg_pkt.bp_id, &reg_pkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &reg_pkt, sizeof(reg_pkt)))
                ret = -EFAULT;
            goto out;
        }

        /* 内存搜索 */
        if (cmd == CMD_MEM_SEARCH) {
            kai_mem_search_packet_t spkt;
            if (copy_from_user(&spkt, (void __user *)arg, sizeof(spkt))) {
                ret = -EFAULT;
                goto out;
            }
            task = get_cached_task_for_target(spkt.pid, spkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_mem_search(task, &spkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &spkt, sizeof(spkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        /* 调用栈捕获 */
        if (cmd == CMD_CALLSTACK) {
            kai_callstack_packet_t cpkt;
            if (copy_from_user(&cpkt, (void __user *)arg, sizeof(cpkt))) {
                ret = -EFAULT;
                goto out;
            }
            task = get_cached_task_for_target(cpkt.pid, cpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_capture_callstack(task, &cpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &cpkt, sizeof(cpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        /* 线程枚举 */
        if (cmd == CMD_LIST_THREADS) {
            kai_thread_list_packet_t tpkt;
            if (copy_from_user(&tpkt, (void __user *)arg, sizeof(tpkt))) {
                ret = -EFAULT;
                goto out;
            }
            task = get_cached_task_for_target(tpkt.pid, tpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_threads(task, &tpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &tpkt, sizeof(tpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        /* 模块枚举 */
        if (cmd == CMD_LIST_MODULES) {
            kai_module_list_packet_t mpkt;
            if (copy_from_user(&mpkt, (void __user *)arg, sizeof(mpkt))) {
                ret = -EFAULT;
                goto out;
            }
            task = get_cached_task_for_target(mpkt.pid, mpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_modules(task, &mpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &mpkt, sizeof(mpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        /* VMA 枚举 */
        if (cmd == CMD_LIST_VMAS) {
            kai_vma_list_packet_t vpkt;
            if (copy_from_user(&vpkt, (void __user *)arg, sizeof(vpkt))) {
                ret = -EFAULT;
                goto out;
            }
            task = get_cached_task_for_target(vpkt.pid, vpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }
            ret = qw_list_vmas(task, &vpkt);
            if (ret == 0 && copy_to_user((void __user *)arg, &vpkt, sizeof(vpkt)))
                ret = -EFAULT;
            put_task_struct(task);
            goto out;
        }

        /* 原始字节读取 */
        if (cmd == CMD_RAW_READ) {
            kai_raw_read_packet_t rpkt;
            void *rbuf;
            if (copy_from_user(&rpkt, (void __user *)arg, sizeof(rpkt))) {
                ret = -EFAULT;
                goto out;
            }
            if (rpkt.size == 0 || rpkt.size > 0x100000) {
                ret = -EINVAL;
                goto out;
            }
            task = get_cached_task_for_target(rpkt.pid, rpkt.pkg_name);
            if (!task) { ret = -ESRCH; goto out; }

            rbuf = kmalloc(rpkt.size, GFP_KERNEL);
            if (!rbuf) {
                put_task_struct(task);
                ret = -ENOMEM;
                goto out;
            }
            ret = manual_read_memory(task, rpkt.addr, rbuf, rpkt.size);
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

        /* === 原有命令（向后兼容） === */
        if (copy_from_user(&pkt, (void __user *)arg, sizeof(pkt))) {
            ret = -EFAULT;
            goto out;
        }

        task = get_cached_task_for_target(pkt.pid, pkt.pkg_name);
        if (!task) {
            ret = -ESRCH;
            goto out;
        }

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
                    ret = manual_read_memory(task, cur_addr, &ptr_val, sizeof(uint64_t));
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

            ret = manual_read_memory(task, final_addr, kbuf, total);
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

        /* 旧版 HWBP 命令 */
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
            /* 旧版兼容：不再需要单独更新 BLR 地址 */
            ret = 0;
            break;

        case CMD_QUERY_SNAPSHOT: {
            unsigned long obj_addr = pkt.addr;
            u32 result[3] = {0, 0, 0};
            int status;
            status = hw_bp_query_snapshot(obj_addr, &result[0], &result[1], &result[2]);
            if (pkt.user_buf && copy_to_user((void __user *)pkt.user_buf,
                    result, sizeof(result))) {
                ret = -EFAULT;
            } else {
                ret = status;
            }
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

/* ---------- 设备操作 ---------- */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = device_ioctl,
};

static struct miscdevice misc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = QINGWEI_DEVICE_NAME,
    .fops = &fops,
};

/* ---------- 模块初始/退出 ---------- */
static int __init qingwei_mcp_init(void)
{
    int ret = misc_register(&misc_dev);
    if (ret) {
        pr_err("misc_register failed\n");
        return ret;
    }

    /* 初始化多硬件断点系统 */
    ret = hw_bp_multi_init();
    if (ret < 0)
        pr_warn("HWBP init failed, hardware breakpoints disabled\n");

    strcpy((char *)THIS_MODULE->name, QW_MODULE_HIDE);
    pr_info("device /dev/%s ready (shown as '%s' in lsmod)\n",
            QINGWEI_DEVICE_NAME, QW_MODULE_HIDE);
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
