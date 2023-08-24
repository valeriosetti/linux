// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "audin.h"

/* Bits of the CTRL register */
#define AUDIN_I2SIN_CTRL_I2SIN_DIR			BIT(0)
#define AUDIN_I2SIN_CTRL_I2SIN_CLK_SEL			BIT(1)
#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SEL		BIT(2)
#define AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC			BIT(3)
#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK		GENMASK(6, 4)
#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_INV		BIT(7)
#define AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK		GENMASK(9, 8)
#define AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK		GENMASK(13, 10)
#define AUDIN_I2SIN_CTRL_I2SIN_EN			BIT(15)

static int audin_i2s_decoder_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);
	int val, ret;

	/* Forward the command to the AIU I2S encoder */
	ret = snd_soc_dai_hw_params(audin->enc_dai, substream, params);
	if (ret < 0) {
		dev_err(dai->dev, "Error: enc_dai failed in hw_params");
		return ret;
	}

	/* I2S decoder always outputs 24bits to the FIFO according to the
	 * manual. The only thing we can do is mask some bits as follows:
	 * - 0: 16 bit
	 * - 1: 18 bits (not exposed as supported format)
	 * - 2: 20 bits (not exposed as supported format)
	 * - 3: 24 bits
	 *
	 * We force 24 bit output here and filter unnecessary ones at the FIFO
	 * stage.*/
	switch (params_physical_width(params)) {
		case 16:
		case 24:
		case 32:
			val = 3;
			break;
		default:
			dev_err(dai->dev, "Error: wrong sample width %d",
				params_physical_width(params));
			return -EINVAL;
	}
	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK, val);
	snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
				      AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK, val);

	/* This SOC only has 1 pin for I2S input, so it cannot support more
	 * than 1 channel. Therefore we enforce and hardcode this value. */
	if (params_channels(params) != 2) {
		dev_warn(dai->dev, "Warning: unsupported channel number %d",
			params_channels(params));
		return -EINVAL;
	}
	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK, 1);
	snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
				      AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK, val);

	return 0;
}

static int audin_i2s_decoder_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);
	
	/* Forward the command to the AIU I2S encoder */
	snd_soc_dai_hw_free(audin->enc_dai, substream, 0);
	
	return 0;
}

static int audin_i2s_decoder_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val;

	/* Only master mode is supported. */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP) {
		dev_err(dai->dev, "Error: I2S decoder can only work in "
				  "master mode");
		return -EINVAL;
	}

	/* Use clocks from AIU and not from the pads since we only want to
	 * support master mode. */
	val = AUDIN_I2SIN_CTRL_I2SIN_CLK_SEL |
	      AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SEL |
	      AUDIN_I2SIN_CTRL_I2SIN_DIR;
	snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL, val, val);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
		val = AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		val = 0;
		break;
	default:
		dev_err(dai->dev, "Error: unsupported format %x", fmt);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
				      AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC, val);

	/* Manufacturer's driver set these harcoded values for I2S case*/
	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK, 1);
	snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
				      AUDIN_I2SIN_CTRL_I2SIN_LRCLK_INV |
				      AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK,
				      val);

	return 0;
}

static int audin_i2s_decoder_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);
	
	/* Just forward this to the AIU I2C encoder */
	return snd_soc_dai_set_sysclk(audin->enc_dai, clk_id, freq, dir);
}

int audin_i2s_decoder_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
					      AUDIN_I2SIN_CTRL_I2SIN_EN,
					      AUDIN_I2SIN_CTRL_I2SIN_EN);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_soc_component_update_bits(component, AUDIN_I2SIN_CTRL,
					      AUDIN_I2SIN_CTRL_I2SIN_EN, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Only 2 channels are supported */
static const unsigned int hw_channels[] = {2, 2};
static const struct snd_pcm_hw_constraint_list hw_channel_constraints = {
	.list = hw_channels,
	.count = ARRAY_SIZE(hw_channels),
	.mask = 0,
};

/* Unfortunately AIU's I2S encoder controls (with its regmap) the relevant
 * clocks and their output-enable for the entire I2S block. AUDIN memory region
 * is not contiguous with AIU, so a different sound component was instantiated.
 * This cause AUDIN not to have access to the AIU registers which are used to
 * set/enable I2S clock, which makes AUDIN basically a slave of AIU from this
 * point of view.
 * As a consequence we need a reference to AIU's I2S encoder in order to
 * forward necessary commands to it: startup, shutdown and hw_params. */
static int find_aiu_enc_dai(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);
	struct snd_soc_dai_link_component dai_link = {
		.dai_name = "I2S Encoder"
	};

	audin->enc_dai = snd_soc_find_dai(&dai_link);
	if (audin->enc_dai == NULL) {
		dev_err(dai->dev, "Error: encoder DAI not found");
		return -EINVAL;
	}

	return 0;
}

/* Clock configuraion is meant to work only as master of the bus. */
static int audin_i2s_decoder_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/* Check runtime parameters */
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &hw_channel_constraints);
	if (ret) {
		dev_err(dai->dev, "adding channels constraints failed\n");
		return ret;
	}

	/* Get a reference to the AIU's I2S encoder dai */
	ret = find_aiu_enc_dai(substream, dai);
	if (ret) {
		dev_err(dai->dev, "Error: enc_dai failed in startup");
		return ret;
	}

	/* Forward the startup command to the AIU I2S encoder */
	ret = snd_soc_dai_startup(audin->enc_dai, substream);
	if (ret) {
		return ret;
	}

	ret = clk_prepare_enable(audin->i2s_input_clk);
	if (ret){
		dev_err(dai->dev, "failed to enable i2s_input_clk\n");
		return ret;
	}

	return 0;
}

static void audin_i2s_decoder_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct audin *audin = snd_soc_component_get_drvdata(dai->component);

	clk_disable_unprepare(audin->i2s_input_clk);

	snd_soc_dai_shutdown(audin->enc_dai, substream, 0);
}

const struct snd_soc_dai_ops audin_i2s_decoder_dai_ops = {
	.hw_params	= audin_i2s_decoder_hw_params,
	.hw_free	= audin_i2s_decoder_hw_free,
	.set_fmt	= audin_i2s_decoder_set_fmt,
	.set_sysclk	= audin_i2s_decoder_set_sysclk,
	.trigger	= audin_i2s_decoder_trigger,
	.startup	= audin_i2s_decoder_startup,
	.shutdown	= audin_i2s_decoder_shutdown,
};

