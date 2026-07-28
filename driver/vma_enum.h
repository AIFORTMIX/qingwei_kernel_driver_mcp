/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_VMA_ENUM_H
#define QINGWEI_VMA_ENUM_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int qw_list_vmas(struct task_struct *task, kai_vma_list_packet_t *pkt);

#endif
