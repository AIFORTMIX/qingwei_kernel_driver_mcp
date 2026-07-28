// SPDX-License-Identifier: GPL-2.0
/*
 * qingwei_mcp - ABSOLUTE MINIMAL TEST
 * Only printk in init/exit. Nothing else.
 */
#include <linux/module.h>
#include <linux/kernel.h>

static int __init qingwei_mcp_init(void)
{
    pr_err("=== qingwei_mcp ABSOLUTE MINIMAL loaded ===\n");
    return 0;
}

static void __exit qingwei_mcp_exit(void)
{
    pr_err("=== qingwei_mcp unloaded ===\n");
}

module_init(qingwei_mcp_init);
module_exit(qingwei_mcp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelAI Team");
MODULE_DESCRIPTION("qingwei_mcp: absolute minimal test");
