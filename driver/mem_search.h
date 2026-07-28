/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_MEM_SEARCH_H
#define QINGWEI_MEM_SEARCH_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int qw_mem_search(struct task_struct *task, kai_mem_search_packet_t *pkt);

#endif
