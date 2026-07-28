/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_CALLSTACK_H
#define QINGWEI_CALLSTACK_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int qw_capture_callstack(struct task_struct *task,
                                kai_callstack_packet_t *pkt);

#endif
