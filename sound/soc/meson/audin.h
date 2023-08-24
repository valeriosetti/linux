/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Valerio Setti <vsetti@baylibre.com>
 */

#ifndef _MESON_AUDIN_H
#define _MESON_AUDIN_H

#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "audin-regs.h"

// DEBUG
#define TRACE_LINE		pr_warn("[DEBUG] %s - %d\n", __func__, __LINE__);
#define DEBUG_MSG(x, ...)	pr_warn("[DEBUG] " x "\n", __VA_ARGS__);

struct audin {
	struct clk *pclk;
	struct clk *i2s_input_clk;
	int irq;
	struct regmap *regmap;
	struct snd_soc_dai *enc_dai;
};

#define AUDIN_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
					   SNDRV_PCM_FMTBIT_S24_LE)

extern const struct snd_soc_dai_ops audin_toddr_dai_ops;
extern const struct snd_soc_dai_ops audin_i2s_decoder_dai_ops;
extern struct device_attribute dev_attr_dump_regs;

int audin_create_debugfs(struct platform_device *pdev);
int audin_toddr_dai_probe(struct snd_soc_dai *dai);
int audin_toddr_dai_remove(struct snd_soc_dai *dai);
int audin_toddr_pcm_new(struct snd_soc_pcm_runtime *rtd,
			struct snd_soc_dai *dai);
snd_pcm_uframes_t audin_toddr_pointer(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream);
int audin_toddr_copy_user(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *buf,
			 unsigned long bytes);

#endif /* _MESON_AUDIN_H */
