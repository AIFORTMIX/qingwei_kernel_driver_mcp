/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_PROC_LOOKUP_H
#define QINGWEI_PROC_LOOKUP_H

#include <linux/sched.h>
#include "qingwei_mcp.h"

int qw_find_pid_by_pkgname(const char *pkg_name, enum kai_match_mode mode);
int qw_extract_cmdline(struct task_struct *task, char *buf, size_t buf_size);

#endif /* QINGWEI_PROC_LOOKUP_H */
