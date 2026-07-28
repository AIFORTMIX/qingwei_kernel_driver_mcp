/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QINGWEI_HW_BP_MULTI_H
#define QINGWEI_HW_BP_MULTI_H

#include "qingwei_mcp.h"
#include <linux/sched.h>

/* 断点优先级 */
enum qw_bp_priority {
    QW_BP_PRI_LOW = 0,     /* 可被自动替换 */
    QW_BP_PRI_NORMAL = 1,  /* 默认 */
    QW_BP_PRI_HIGH = 2,    /* 不可被自动替换 */
};

#ifdef CONFIG_HAVE_HW_BREAKPOINT

int hw_bp_multi_init(void);
void hw_bp_multi_cleanup_all(void);
int hw_bp_multi_detect_count(void);

int hw_bp_multi_set(kai_hwbp_setup_t *setup, struct task_struct *task);
int hw_bp_multi_remove(kai_hwbp_remove_t *remove);
int hw_bp_multi_list(kai_hwbp_list_packet_t *pkt);

/* 自动槽位分配：bp_index == 0xFFFFFFFF 时自动寻找空闲槽位 */
int hw_bp_multi_set_auto(kai_hwbp_setup_t *setup, struct task_struct *task);

/* 硬件断点数量查询 */
int hw_bp_get_available_count(void);
int hw_bp_get_total_slots(void);

/* 断点使用状态查询 */
int hw_bp_get_usage(qingwei_mcp_bp_usage_t *usage);

/* 旧版兼容接口 */
int hw_bp_legacy_setup(struct task_struct *task, unsigned long bp_addr,
                        unsigned long blr_x8, unsigned long blr_x9);
void hw_bp_legacy_cleanup(void);
int hw_bp_query_snapshot(unsigned long obj_addr,
                          u32 *x_raw, u32 *y_raw, u32 *z_raw);
int hw_bp_get_stats(hwbp_stats_t *stats);

#else

static inline int hw_bp_multi_init(void) { return 0; }
static inline void hw_bp_multi_cleanup_all(void) {}
static inline int hw_bp_multi_detect_count(void) { return 0; }
static inline int hw_bp_multi_set(kai_hwbp_setup_t *s, struct task_struct *t) { return -ENOSYS; }
static inline int hw_bp_multi_remove(kai_hwbp_remove_t *r) { return -ENOSYS; }
static inline int hw_bp_multi_list(kai_hwbp_list_packet_t *p) { return -ENOSYS; }
static inline int hw_bp_multi_set_auto(kai_hwbp_setup_t *s, struct task_struct *t) { return -ENOSYS; }
static inline int hw_bp_get_available_count(void) { return 0; }
static inline int hw_bp_get_total_slots(void) { return 0; }
static inline int hw_bp_get_usage(qingwei_mcp_bp_usage_t *u) {
    memset(u, 0, sizeof(*u)); return 0;
}
static inline int hw_bp_legacy_setup(struct task_struct *t, unsigned long a,
                                      unsigned long x, unsigned long y) { return -ENOSYS; }
static inline void hw_bp_legacy_cleanup(void) {}
static inline int hw_bp_query_snapshot(unsigned long a, u32 *x, u32 *y, u32 *z) { return -ENOSYS; }
static inline int hw_bp_get_stats(hwbp_stats_t *s) { s->active = 0; return 0; }

#endif /* CONFIG_HAVE_HW_BREAKPOINT */

#endif /* QINGWEI_HW_BP_MULTI_H */
