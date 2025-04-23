// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2025 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
#include "formatter-common.h"

#define AIU_I2S_SOURCE_DESC_MODE_8CH	BIT(0)
#define AIU_I2S_SOURCE_DESC_MODE_24BIT	BIT(5)
#define AIU_I2S_SOURCE_DESC_MODE_32BIT	BIT(9)
#define AIU_I2S_SOURCE_DESC_MODE_SPLIT	BIT(11)
#define AIU_RST_SOFT_I2S_FAST		BIT(0)

#define AIU_I2S_DAC_CFG_MSB_FIRST	BIT(2)
#define AIU_CLK_CTRL_I2S_DIV_EN		BIT(0)
#define AIU_CLK_CTRL_I2S_DIV		GENMASK(3, 2)
#define AIU_CLK_CTRL_AOCLK_INVERT	BIT(6)
#define AIU_CLK_CTRL_LRCLK_INVERT	BIT(7)
#define AIU_CLK_CTRL_LRCLK_SKEW		GENMASK(9, 8)
#define AIU_CLK_CTRL_MORE_HDMI_AMCLK	BIT(6)
#define AIU_CLK_CTRL_MORE_I2S_DIV	GENMASK(5, 0)
#define AIU_CODEC_DAC_LRCLK_CTRL_DIV	GENMASK(11, 0)

static int aiu_formatter_set_fmt(struct regmap *regmap, unsigned int fmt)
{
	unsigned int inv = fmt & SND_SOC_DAIFMT_INV_MASK;
	unsigned int val = 0;
	unsigned int skew;

	/* Only CPU Master / Codec Slave supported ATM */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP)
		return -EINVAL;

	if (inv == SND_SOC_DAIFMT_NB_IF ||
	    inv == SND_SOC_DAIFMT_IB_IF)
		val |= AIU_CLK_CTRL_LRCLK_INVERT;

	if (inv == SND_SOC_DAIFMT_IB_NF ||
	    inv == SND_SOC_DAIFMT_IB_IF)
		val |= AIU_CLK_CTRL_AOCLK_INVERT;

	/* bit-clock seems not to have the correct polarity by default. However
	 * it cannot be changed with the "bitclock-inversion" DT property
	 * otherwise this would propagate also to the external codec, thus
	 * making the change unrelevant
	 */
	val ^= AIU_CLK_CTRL_AOCLK_INVERT;

	/* Signal skew */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* Invert sample clock for i2s */
		val ^= AIU_CLK_CTRL_LRCLK_INVERT;
		skew = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		skew = 0;
		break;
	default:
		return -EINVAL;
	}

	val |= FIELD_PREP(AIU_CLK_CTRL_LRCLK_SKEW, skew);
	return regmap_update_bits(regmap, AIU_CLK_CTRL,
				  AIU_CLK_CTRL_LRCLK_INVERT |
				  AIU_CLK_CTRL_AOCLK_INVERT |
				  AIU_CLK_CTRL_LRCLK_SKEW,
				  val);
}

static int aiu_formatter_hw_params(struct regmap *regmap,
				   int channels, int physical_width)
{
	/* Always operate in split (classic interleaved) mode */
	unsigned int desc = AIU_I2S_SOURCE_DESC_MODE_SPLIT;
	unsigned int tmp;

	/* Reset required to update the pipeline */
	regmap_write(regmap, AIU_RST_SOFT, AIU_RST_SOFT_I2S_FAST);
	regmap_read(regmap, AIU_I2S_SYNC, &tmp);

	switch (physical_width) {
	case 16: /* Nothing to do */
		break;

	case 32:
		desc |= (AIU_I2S_SOURCE_DESC_MODE_24BIT |
			AIU_I2S_SOURCE_DESC_MODE_32BIT);
		break;

	default:
		return -EINVAL;
	}

	switch (channels) {
	case 2: /* Nothing to do */
		break;
	case 8:
		desc |= AIU_I2S_SOURCE_DESC_MODE_8CH;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, AIU_I2S_SOURCE_DESC,
				  AIU_I2S_SOURCE_DESC_MODE_8CH |
				  AIU_I2S_SOURCE_DESC_MODE_24BIT |
				  AIU_I2S_SOURCE_DESC_MODE_32BIT |
				  AIU_I2S_SOURCE_DESC_MODE_SPLIT,
				  desc);
}

static int aiu_formatter_prepare(struct formatter *formatter,
				 struct stream_data *stream_data)
{
	int ret;

	ret = aiu_formatter_set_fmt(formatter->regmap,
				stream_data->common->fmt);
	if (ret)
		return ret;

	ret = aiu_formatter_hw_params(formatter->regmap,
				stream_data->common->channels,
				stream_data->common->physical_width);
	if (ret)
		return ret;

	return 0;
}

static int aiu_formatter_enable(struct formatter *formatter)
{
	return 0;
}

static int aiu_formatter_disable(struct formatter *formatter)
{
	return 0;
}

const struct formatter_ops aiu_formatter_ops = {
	.prepare = aiu_formatter_prepare,
	.enable = aiu_formatter_enable,
	.disable = aiu_formatter_disable,
};

static struct snd_soc_dai *
aiu_formatter_get_be_dai(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dai *be;

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		if (!p->connect)
			continue;

		if (p->sink->id == snd_soc_dapm_dai_in)
			return (struct snd_soc_dai *)p->sink->priv;

		be = aiu_formatter_get_be_dai(p->sink);
		if (be)
			return be;
	}

	return NULL;
}

static int aiu_formatter_attach_to_be(struct snd_soc_dapm_widget *w,
					bool attach)
{
	struct snd_soc_dai *be_dai = aiu_formatter_get_be_dai(w);
	struct stream_data *stream_data;
	struct aiu *aiu = snd_soc_component_get_drvdata(
		snd_soc_dapm_to_component(w->dapm));

	if (be_dai == NULL) {
		return -ENODEV;
	}

	stream_data = snd_soc_dai_dma_data_get_playback(be_dai);
	if (stream_data == NULL)
		return -EINVAL;

	if (attach)
		stream_data->formatter = &aiu->formatter;
	else
		stream_data->formatter = NULL;

	return 0;
}

int aiu_formatter_event(struct snd_soc_dapm_widget *w,
		        struct snd_kcontrol *control,
		        int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	int ret = 0;
	(void) control;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = aiu_formatter_attach_to_be(w, true);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		ret = aiu_formatter_attach_to_be(w, false);
		break;

	default:
		dev_err(c->dev, "Unexpected event %d\n", event);
		return -EINVAL;
	}

	if (ret)
		dev_err(c->dev, "BE DAI %s failed\n",
			(event == SND_SOC_DAPM_PRE_PMU) ?
			"attach": "detach");

	return ret;
}
