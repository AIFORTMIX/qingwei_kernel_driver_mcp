// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "qingwei_mcp_cs: " fmt

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "callstack.h"
#include "sw_bp.h"

/* 内存读写函数声明 */
extern int manual_read_memory(struct task_struct *task, unsigned long vaddr,
                              void *kbuf, size_t len);

/*
 * ARM64 调用栈捕获
 * 沿 frame pointer (FP/X29) 链回溯
 * 每个栈帧：FP 指向 {prev_fp, lr} 对
 */
int qw_capture_callstack(struct task_struct *task,
                                kai_callstack_packet_t *pkt)
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

    /* 尝试从软件断点获取寄存器快照 */
    if (pkt->bp_id > 0) {
        memset(&reg_pkt, 0, sizeof(reg_pkt));
        ret = sw_bp_read_regs(pkt->bp_id, &reg_pkt);
        if (ret == 0 && reg_pkt.valid_mask) {
            /* 第一帧：当前 PC */
            frames[count].pc = reg_pkt.pc;
            frames[count].sp = reg_pkt.sp;
            frames[count].fp = reg_pkt.regs[29]; /* X29 = FP */
            frames[count].lr = reg_pkt.regs[30]; /* X30 = LR */
            snprintf(frames[count].symbol, sizeof(frames[count].symbol),
                     "pc:0x%lx", reg_pkt.pc);
            count++;

            fp = reg_pkt.regs[29];
        } else {
            /* 无法获取寄存器，返回空 */
            pkt->frame_count = 0;
            kfree(frames);
            return -ENOENT;
        }
    } else {
        pkt->frame_count = 0;
        kfree(frames);
        return -EINVAL;
    }

    /* 沿 FP 链回溯 */
    while (count < pkt->max_frames && fp && fp > 0x1000) {
        unsigned long frame[2]; /* {prev_fp, lr} */

        ret = manual_read_memory(task, fp, frame, sizeof(frame));
        if (ret < (int)sizeof(frame))
            break;

        frames[count].fp = frame[0];
        frames[count].lr = frame[1];
        frames[count].pc = frame[1]; /* LR 即返回地址 */
        frames[count].sp = fp + 16;
        snprintf(frames[count].symbol, sizeof(frames[count].symbol),
                 "lr:0x%lx", frame[1]);
        count++;

        if (frame[0] <= fp) /* 防止死循环 */
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
