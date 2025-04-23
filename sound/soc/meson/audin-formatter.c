// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2025 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "formatter-common.h"

/* I2SIN_CTRL register and bits */
#define AUDIN_I2SIN_CTRL	0x0
	#define AUDIN_I2SIN_CTRL_I2SIN_DIR		BIT(0)
	#define AUDIN_I2SIN_CTRL_I2SIN_CLK_SEL		BIT(1)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SEL	BIT(2)
	#define AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC		BIT(3)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK	GENMASK(6, 4)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_INV	BIT(7)
	#define AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK	GENMASK(9, 8)
	#define AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK	GENMASK(13, 10)
	#define AUDIN_I2SIN_CTRL_I2SIN_EN		BIT(15)

static const char * const clk_ids[] = {
	"pclk",
	"i2s_input_clk",
};

struct audin_formatter_data {
	struct clk_bulk_data clks[ARRAY_SIZE(clk_ids)];
	struct formatter formatter;
};

static int audin_formatter_hw_params(struct regmap *regmap,
				     int channels, int physical_width)
{
	unsigned int val;
	int ret;

	/* I2S decoder always outputs 24bits to the FIFO according to the
	 * manual. The only thing we can do is mask some bits as follows:
	 * - 0: 16 bit
	 * - 1: 18 bits (not exposed as supported format)
	 * - 2: 20 bits (not exposed as supported format)
	 * - 3: 24 bits
	 *
	 * We force 24 bit output here and filter unnecessary ones at the FIFO
	 * stage. This should ease the future support of 24 bit format.
	 * Note: data is left-justified, so in case of 16 bits samples, this
	 *       means that the LSB is to be discarded at FIFO level and the
	 *       relevant part is in bits [23:8].
	 */
	if (physical_width != 16)
		return -EINVAL;

	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK, 3);
	ret = regmap_update_bits(regmap, AUDIN_I2SIN_CTRL,
				 AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK, val);
	if (ret)
		return ret;

	/* The manual claims that this platform supports up to 4 streams
	 * (8 channels), but only 1 stream (2 channels) is supported ATM.
	 */
	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK, 1);
	ret = regmap_update_bits(regmap, AUDIN_I2SIN_CTRL,
				 AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK, val);
	if (ret)
		return ret;

	return 0;
}

static int audin_formatter_set_fmt(struct regmap *regmap, unsigned int fmt)
{
	unsigned int val = 0;
	int ret;

	/* Only CPU Master / Codec Slave supported ATM */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP)
		return -EINVAL;

	/* Use clocks from AIU and not from the pads since we only want to
	 * support master mode.
	 */
	val = AUDIN_I2SIN_CTRL_I2SIN_CLK_SEL |
	      AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SEL |
	      AUDIN_I2SIN_CTRL_I2SIN_DIR;
	ret = regmap_update_bits(regmap, AUDIN_I2SIN_CTRL, val, val);
	if (ret)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
		val = AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(regmap, AUDIN_I2SIN_CTRL,
				 AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC, val);
	if (ret)
		return ret;

	/* MSB data starts 1 clock cycle after LRCLK transition, as per I2S
	 * specs.
	 */
	val = FIELD_PREP(AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK, 1);
	ret = regmap_update_bits(regmap, AUDIN_I2SIN_CTRL,
				 AUDIN_I2SIN_CTRL_I2SIN_LRCLK_INV |
				 AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK,
				 val);
	if (ret)
		return ret;

	return 0;
}

static int audin_formatter_prepare(struct formatter *formatter,
				   struct stream_data *stream_data)
{
	int ret;

	ret = audin_formatter_set_fmt(formatter->regmap,
				stream_data->common->fmt);
	if (ret)
		return ret;

	ret = audin_formatter_hw_params(formatter->regmap,
					stream_data->common->channels,
					stream_data->common->physical_width);
	if (ret)
		return ret;

	return 0;
}

static int audin_formatter_enable(struct formatter *formatter)
{
	return regmap_update_bits(formatter->regmap, AUDIN_I2SIN_CTRL,
				  AUDIN_I2SIN_CTRL_I2SIN_EN,
				  AUDIN_I2SIN_CTRL_I2SIN_EN);
}

static int audin_formatter_disable(struct formatter *formatter)
{
	return regmap_update_bits(formatter->regmap, AUDIN_I2SIN_CTRL,
				  AUDIN_I2SIN_CTRL_I2SIN_EN, 0);
}

static const struct formatter_ops audin_formatter_ops = {
	.prepare = audin_formatter_prepare,
	.enable = audin_formatter_enable,
	.disable = audin_formatter_disable,
};

static struct snd_soc_dai *
audin_formatter_get_be_dai(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;
	struct snd_soc_dai *be;

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		if (!p->connect)
			continue;

		if (p->source->id == snd_soc_dapm_dai_out)
			return (struct snd_soc_dai *)p->source->priv;

		be = audin_formatter_get_be_dai(p->source);
		if (be)
			return be;
	}

	return NULL;
}

static int audin_formatter_attach_to_be(struct snd_soc_dapm_widget *w,
					bool attach)
{
	struct snd_soc_dai *be_dai = audin_formatter_get_be_dai(w);
	struct stream_data *stream_data;
	struct audin_formatter_data *audin_frmt = snd_soc_component_get_drvdata(
		snd_soc_dapm_to_component(w->dapm));

	if (be_dai == NULL)
		return -ENODEV;

	stream_data = snd_soc_dai_dma_data_get_capture(be_dai);
	if (stream_data == NULL)
		return -EINVAL;

	if (attach)
		stream_data->formatter = &audin_frmt->formatter;
	else
		stream_data->formatter = NULL;

	return 0;
}

static int audin_formatter_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *control,
				 int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	int ret = 0;
	(void) control;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = audin_formatter_attach_to_be(w, true);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		ret = audin_formatter_attach_to_be(w, false);
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

static const struct snd_soc_dapm_widget audin_formatter_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("IN",  NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA_E("FRMT", SND_SOC_NOPM, 0, 0, NULL, 0,
			   audin_formatter_event,
			   (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_AIF_OUT("OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route audin_formatter_dapm_routes[] = {
	{ "FRMT", NULL,  "IN" },
	{ "OUT", NULL, "FRMT" },
};

static const struct snd_soc_component_driver formatter_component = {
	.dapm_widgets		= audin_formatter_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(audin_formatter_dapm_widgets),
	.dapm_routes		= audin_formatter_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(audin_formatter_dapm_routes),
};

static const struct regmap_config audin_formatter_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x3,
};

static int audin_formatter_clk_get(struct device *dev,
				   struct audin_formatter_data *data)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(clk_ids); i++)
		data->clks[i].id = clk_ids[i];

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(clk_ids), data->clks);
	if (ret) {
		dev_err(dev, "Failed to get bulk clocks %d\n", ret);
		return ret;
	}

	return 0;
}

static int audin_formatter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct regmap *map;
	struct audin_formatter_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &audin_formatter_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "Failed to init regmap: %ld\n",
			PTR_ERR(map));
		return PTR_ERR(map);
	}

	ret = audin_formatter_clk_get(dev, data);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(clk_ids), data->clks);
	if (ret)
		return ret;

	data->formatter.ops = &audin_formatter_ops;
	data->formatter.regmap = map;

	ret = snd_soc_register_component(dev, &formatter_component, NULL, 0);
	if (ret) {
		dev_err(dev, "Failed to register component\n");
		return ret;
	}

	return 0;
}

static void audin_formatter_remove(struct platform_device *pdev)
{
	struct audin_formatter_data *data = platform_get_drvdata(pdev);
	snd_soc_unregister_component(&pdev->dev);
	clk_bulk_disable_unprepare(ARRAY_SIZE(clk_ids), data->clks);
}

static const struct of_device_id audin_formatter_of_match[] = {
	{ .compatible = "amlogic,audin-formatter-gxbb", .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, audin_of_match);

static struct platform_driver audin_formatter_pdrv = {
	.probe = audin_formatter_probe,
	.remove = audin_formatter_remove,
	.driver = {
		.name = "meson-audin-formatter",
		.of_match_table = audin_formatter_of_match,
	},
};
module_platform_driver(audin_formatter_pdrv);

MODULE_DESCRIPTION("Meson AUDIN Formatter Driver");
MODULE_AUTHOR("Valerio Setti <vsetti@baylibre.com>");
MODULE_LICENSE("GPL v2");
