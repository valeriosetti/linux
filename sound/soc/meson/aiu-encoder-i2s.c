// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mutex.h>
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

static void aiu_encoder_i2s_divider_enable(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV_EN,
				      enable ? AIU_CLK_CTRL_I2S_DIV_EN : 0);
}

static int aiu_encoder_i2s_set_legacy_div(struct snd_soc_component *component,
					  struct snd_pcm_hw_params *params,
					  unsigned int bs)
{
	switch (bs) {
	case 1:
	case 2:
	case 4:
	case 8:
		/* These are the only valid legacy dividers */
		break;

	default:
		dev_err(component->dev, "Unsupported i2s divider: %u\n", bs);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_I2S_DIV,
						 __ffs(bs)));

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_MORE_I2S_DIV,
						 0));

	return 0;
}

static int aiu_encoder_i2s_set_more_div(struct snd_soc_component *component,
					struct snd_pcm_hw_params *params,
					unsigned int bs)
{
	/*
	 * NOTE: this HW is odd.
	 * In most configuration, the i2s divider is 'mclk / blck'.
	 * However, in 16 bits - 8ch mode, this factor needs to be
	 * increased by 50% to get the correct output rate.
	 * No idea why !
	 */
	if (params_width(params) == 16 && params_channels(params) == 8) {
		if (bs % 2) {
			dev_err(component->dev,
				"Cannot increase i2s divider by 50%%\n");
			return -EINVAL;
		}
		bs += bs / 2;
	}

	/* Use CLK_MORE for mclk to bclk divider */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_I2S_DIV, 0));

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_MORE_I2S_DIV,
						 bs - 1));

	return 0;
}

static int aiu_encoder_i2s_set_clocks(struct snd_soc_component *component,
				      struct snd_pcm_hw_params *params)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	unsigned int srate = params_rate(params);
	unsigned int fs, bs;
	int ret;

	/* Get the oversampling factor */
	fs = DIV_ROUND_CLOSEST(clk_get_rate(aiu->i2s.clks[MCLK].clk), srate);

	if (fs % 64)
		return -EINVAL;

	/* Send data MSB first */
	snd_soc_component_update_bits(component, AIU_I2S_DAC_CFG,
				      AIU_I2S_DAC_CFG_MSB_FIRST,
				      AIU_I2S_DAC_CFG_MSB_FIRST);

	/* Set bclk to lrlck ratio */
	snd_soc_component_update_bits(component, AIU_CODEC_DAC_LRCLK_CTRL,
				      AIU_CODEC_DAC_LRCLK_CTRL_DIV,
				      FIELD_PREP(AIU_CODEC_DAC_LRCLK_CTRL_DIV,
						 64 - 1));

	bs = fs / 64;

	if (aiu->platform->has_clk_ctrl_more_i2s_div)
		ret = aiu_encoder_i2s_set_more_div(component, params, bs);
	else
		ret = aiu_encoder_i2s_set_legacy_div(component, params, bs);

	if (ret)
		return ret;

	/* Make sure amclk is used for HDMI i2s as well */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK);

	return 0;
}

static bool is_any_stream_active(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct stream_data *stream_data;
	int stream;
	bool is_any_active = false;

	/* Just pick one as we're only interested on the common data. */
	stream_data = snd_soc_dai_dma_data_get(dai, substream->stream);

	mutex_lock(&(stream_data->common->lock));

	for_each_pcm_streams(stream) {
		stream_data = snd_soc_dai_dma_data_get(dai, stream);
		is_any_active |= stream_data->active;
	}

	mutex_unlock(&(stream_data->common->lock));

	return is_any_active;
}

static int aiu_encoder_i2s_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct stream_data *stream_data;

	stream_data = snd_soc_dai_dma_data_get(dai, substream->stream);
	stream_data->common->channels = params_channels(params);
	stream_data->common->physical_width = params_physical_width(params);
	stream_data->common->rate = params_rate(params);

	/* If any stream is already running do not attempt to change clocks */
	if (is_any_stream_active(substream, dai))
		return 0;

	return aiu_encoder_i2s_set_clocks(component, params);
}

static int aiu_encoder_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct stream_data *stream_data = snd_soc_dai_dma_data_get(dai, 0);

	stream_data->common->fmt = fmt;

	return 0;
}

static int aiu_encoder_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (WARN_ON(clk_id != 0))
		return -EINVAL;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = clk_set_rate(aiu->i2s.clks[MCLK].clk, freq);
	if (ret)
		dev_err(dai->dev, "Failed to set sysclk to %uHz", freq);

	return ret;
}

static int aiu_encoder_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct stream_data *stream_data =
		snd_soc_dai_dma_data_get(dai, substream->stream);
	struct formatter *formatter = stream_data->formatter;
	int ret;

	if (formatter == NULL) {
		dev_err(dai->dev, "No formatter attached");
		return -EINVAL;
	}

	mutex_lock(&(stream_data->common->lock));

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			ret = formatter->ops->prepare(formatter, stream_data);
			if (ret)
				return ret;
			ret = formatter->ops->enable(formatter);
			if (ret)
				return ret;
			stream_data->active = true;
			aiu_encoder_i2s_divider_enable(dai->component, true);
			break;
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		case SNDRV_PCM_TRIGGER_STOP:
			stream_data->active = false;
			ret = formatter->ops->disable(formatter);
			if (ret)
				return ret;
			break;
		default:
			return -EINVAL;
	}

	mutex_unlock(&(stream_data->common->lock));

	return 0;
}

static const unsigned int hw_channels[] = {2, 8};
static const struct snd_pcm_hw_constraint_list hw_channel_constraints = {
	.list = hw_channels,
	.count = ARRAY_SIZE(hw_channels),
	.mask = 0,
};

static int aiu_encoder_i2s_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/* Make sure the encoder gets either 2 or 8 channels */
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &hw_channel_constraints);
	if (ret) {
		dev_err(dai->dev, "adding channels constraints failed\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(aiu->i2s.clk_num, aiu->i2s.clks);
	if (ret)
		dev_err(dai->dev, "failed to enable i2s clocks\n");

	return ret;
}

static void aiu_encoder_i2s_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);

	if (!is_any_stream_active(substream, dai)) {
		aiu_encoder_i2s_divider_enable(dai->component, false);
		clk_bulk_disable_unprepare(aiu->i2s.clk_num, aiu->i2s.clks);
	}
}

static int aiu_encoder_i2s_remove(struct snd_soc_dai *dai)
{
	struct stream_data *stream_data;
	bool common_freed = false;
	int stream;

	for_each_pcm_streams(stream) {
		stream_data = snd_soc_dai_dma_data_get(dai, stream);

		if ((!common_freed) && (stream_data->common != NULL)) {
			kfree(stream_data->common);
			common_freed = true;
		}

		if (stream_data) {
			WARN_ON(stream_data->formatter != NULL);
			kfree(stream_data);
		}
	}

	return 0;
}

static int aiu_encoder_i2s_probe(struct snd_soc_dai *dai)
{
	struct stream_common_data *common;
	struct stream_data *sd;
	int stream;

	common = devm_kzalloc(dai->dev, sizeof(*common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;

	mutex_init(&(common->lock));

	for_each_pcm_streams(stream) {
		if (!snd_soc_dai_get_widget(dai, stream))
			continue;

		sd = devm_kzalloc(dai->dev, sizeof(*sd), GFP_KERNEL);
		if (sd == NULL) {
			aiu_encoder_i2s_remove(dai);
			return -ENOMEM;
		}
		sd->common = common;

		snd_soc_dai_dma_data_set(dai, stream, sd);
	};

	return 0;
}

const struct snd_soc_dai_ops aiu_encoder_i2s_dai_ops = {
	.probe		= aiu_encoder_i2s_probe,
	.remove		= aiu_encoder_i2s_remove,
	.hw_params	= aiu_encoder_i2s_hw_params,
	.set_fmt	= aiu_encoder_i2s_set_fmt,
	.set_sysclk	= aiu_encoder_i2s_set_sysclk,
	.startup	= aiu_encoder_i2s_startup,
	.shutdown	= aiu_encoder_i2s_shutdown,
	.trigger	= aiu_encoder_i2s_trigger,
};
