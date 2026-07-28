/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_THREAD_ENUM_H
#define QINGWEI_THREAD_ENUM_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int qw_list_threads(struct task_struct *task, kai_thread_list_packet_t *pkt);

#endif
