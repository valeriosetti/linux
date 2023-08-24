// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include "audin.h"

#if defined(CONFIG_DEBUG_FS)

/* Local temporary buffer to hold the data before being sent to user space */
static char output_buf[1024];

static ssize_t dump_single_register(struct regmap *regmap, const char *reg_name,
				    unsigned int reg, char *out_buf)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(regmap, reg, &reg_val);
	if (ret != 0) {
		return ret;
	}

	return sprintf(out_buf, "%s = %08x\n", reg_name, reg_val);
}

#define DUMP_OR_RET(addr) \
	{ \
		ssize_t tmp_len = 0; \
		tmp_len = dump_single_register(audin->regmap, #addr, addr, \
					       buf_ptr); \
		if (tmp_len < 0) { \
			dev_err(&pdev->dev, \
				"Error: unable to dump register %s\n", #addr); \
			return tmp_len; \
		} \
		buf_ptr += tmp_len; \
		len += tmp_len; \
	}

/* Dump the content of I2S's input and FIFO0 configuration registers */
static ssize_t dump_regs_read(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct platform_device *pdev = file->private_data;
	struct audin *audin = platform_get_drvdata(pdev);
	char *buf_ptr = output_buf;
	ssize_t len = 0;

	DUMP_OR_RET(AUDIN_I2SIN_CTRL);
	DUMP_OR_RET(AUDIN_FIFO0_START);
	DUMP_OR_RET(AUDIN_FIFO0_END);
	DUMP_OR_RET(AUDIN_FIFO0_PTR);
	DUMP_OR_RET(AUDIN_FIFO0_INTR);
	DUMP_OR_RET(AUDIN_FIFO0_RDPTR);
	DUMP_OR_RET(AUDIN_FIFO0_CTRL);
	DUMP_OR_RET(AUDIN_FIFO0_CTRL1);
	DUMP_OR_RET(AUDIN_FIFO0_LVL0);
	DUMP_OR_RET(AUDIN_FIFO0_LVL1);
	DUMP_OR_RET(AUDIN_FIFO0_LVL2);
	DUMP_OR_RET(AUDIN_FIFO0_REQID);
	DUMP_OR_RET(AUDIN_FIFO0_WRAP);
	DUMP_OR_RET(AUDIN_INT_CTRL);
	DUMP_OR_RET(AUDIN_FIFO_INT);

	return simple_read_from_buffer(user_buf, count, ppos, output_buf, len);
}

static const struct file_operations dump_regs_ops = {
	.open = simple_open,
	.read = dump_regs_read,
	.llseek = default_llseek,
};

int audin_create_debugfs(struct platform_device *pdev)
{
	struct dentry *debugfs_dir, *debugfs_file;
	int ret;

	debugfs_dir = debugfs_create_dir("audin", NULL);
	if (debugfs_dir == NULL) {
		return ret;
	}

	debugfs_file = debugfs_create_file("dump_regs", S_IRUGO, debugfs_dir,
					   pdev, &dump_regs_ops);
	if (debugfs_file == NULL) {
		return ret;
	}

	dev_info(&pdev->dev, "Debugfs created");

	return 0;
}

#endif /* CONFIG_DEBUG_FS */