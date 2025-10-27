// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2025 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
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
	/* Nothing to do */
}

static void aiu_formatter_disable(struct regmap *map)
{
	/* Nothing to do */
}

static int aiu_formatter_prepare(struct regmap *map,
				 const struct gx_formatter_hw *quirks,
				 struct gx_stream *ts)
{
	/* TBD */

	return 0;
}

const struct gx_formatter_ops aiu_formatter_ops = {
	.get_stream	= aiu_formatter_get_stream,
	.prepare	= aiu_formatter_prepare,
	.enable		= aiu_formatter_enable,
	.disable	= aiu_formatter_disable,
};
