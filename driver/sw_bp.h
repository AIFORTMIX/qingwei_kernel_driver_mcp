/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_SW_BP_H
#define QINGWEI_SW_BP_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int sw_bp_set(kai_swbp_setup_t *setup, struct task_struct *task);
int sw_bp_remove(kai_swbp_remove_t *remove);
int sw_bp_read_regs(u32 sw_bp_id, kai_reg_read_packet_t *pkt);
void sw_bp_cleanup_all(void);
int sw_bp_get_count(void);

#endif
