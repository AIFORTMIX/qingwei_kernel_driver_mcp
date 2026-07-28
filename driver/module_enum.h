/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_MODULE_ENUM_H
#define QINGWEI_MODULE_ENUM_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

int qw_list_modules(struct task_struct *task, kai_module_list_packet_t *pkt);

#endif
