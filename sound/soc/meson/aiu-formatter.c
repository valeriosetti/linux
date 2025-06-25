// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2025 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "gx-formatter.h"

static struct snd_soc_dai *
aiu_formatter_get_be(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dai *be;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (!p->connect)
			continue;

		if (p->sink->id == snd_soc_dapm_dai_in)
			return (struct snd_soc_dai *)p->sink->priv;

		be = aiu_formatter_get_be(p->sink);
		if (be)
			return be;
	}

	return NULL;
}

static struct gx_stream *
aiu_formatter_get_stream(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dai *be = aiu_formatter_get_be(w);

	if (!be)
		return NULL;

	return snd_soc_dai_dma_data_get_playback(be);
}

static void aiu_formatter_enable(struct regmap *map)
{

}

static void aiu_formatter_disable(struct regmap *map)
{

}

static int aiu_formatter_prepare(struct regmap *map,
				 const struct gx_formatter_hw *quirks,
				 struct gx_stream *ts)
{
	return 0;
}

static const struct regmap_config aiu_formatter_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x2ac,
};

static const struct snd_soc_dapm_widget aiu_formatter_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("IN",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA_E("FRMT", SND_SOC_NOPM, 0, 0, NULL, 0,
			   gx_formatter_event,
			   (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_AIF_OUT("OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route aiu_formatter_dapm_routes[] = {
	{ "FRMT", NULL, "IN" },
	{ "OUT", NULL, "FRMT" },
};

static const struct snd_soc_component_driver aiu_formatter_component = {
	.dapm_widgets		= aiu_formatter_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(aiu_formatter_dapm_widgets),
	.dapm_routes		= aiu_formatter_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(aiu_formatter_dapm_routes),
};

static const struct gx_formatter_ops aiu_formatter_ops = {
	.get_stream	= aiu_formatter_get_stream,
	.prepare	= aiu_formatter_prepare,
	.enable		= aiu_formatter_enable,
	.disable	= aiu_formatter_disable,
};

const struct gx_formatter_driver aiu_formatter_drv = {
	.component_drv	= &aiu_formatter_component,
	.regmap_cfg	= &aiu_formatter_regmap_cfg,
	.ops		= &aiu_formatter_ops,
};

static struct platform_driver aiu_formatter_pdrv = {
	.probe = gx_formatter_probe,
	.driver = {
		.name = "aiu-formatter",
	},
};
module_platform_driver(aiu_formatter_pdrv);
