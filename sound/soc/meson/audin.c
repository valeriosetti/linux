// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <dt-bindings/sound/meson-audin.h>
#include "audin.h"

static const struct snd_soc_dapm_route audin_cpu_dapm_routes[] = {
	{ "toDDR 0 Capture", NULL, "I2S Decoder Capture" },
};

static int audin_cpu_of_xlate_dai_name(struct snd_soc_component *component,
				     const struct of_phandle_args *args,
				     const char **dai_name)
{
	struct snd_soc_dai *dai;
	int id;

	if (args->args_count != 1)
		return -EINVAL;

	id = args->args[0];

	if (id < 0 || id >= component->num_dai)
		return -EINVAL;

	for_each_component_dais(component, dai) {
		if (id == 0)
			break;
		id--;
	}

	*dai_name = dai->driver->name;

	return 0;
}

static int audin_cpu_component_probe(struct snd_soc_component *component)
{
	struct audin *audin = snd_soc_component_get_drvdata(component);

	return clk_prepare_enable(audin->pclk);
}

static void audin_cpu_component_remove(struct snd_soc_component *component)
{
	struct audin *audin = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(audin->pclk);
}

static const struct snd_soc_component_driver audin_cpu_component = {
	.name			= "AUDIN CPU",
	.dapm_widgets		= NULL,
	.num_dapm_widgets	= 0,
	.dapm_routes		= audin_cpu_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(audin_cpu_dapm_routes),
	.of_xlate_dai_name	= audin_cpu_of_xlate_dai_name,
	.pointer		= audin_toddr_pointer,
	.copy_user		= audin_toddr_copy_user,
	.probe			= audin_cpu_component_probe,
	.remove			= audin_cpu_component_remove,
#ifdef CONFIG_DEBUG_FS
	.debugfs_prefix		= "cpu",
#endif
};

static struct snd_soc_dai_driver audin_cpu_dai_drv[] = {
	[CPU_AUDIN_TODDR_0] = {
		.name = "toDDR 0",
		.capture = {
			.stream_name	= "toDDR 0 Capture",
			.channels_min	= 1,
			.channels_max	= 8,
			.rates		= SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min	= 5512,
			.rate_max	= 192000,
			.formats	= AUDIN_FORMATS,
		},
		.ops		= &audin_toddr_dai_ops,
		.pcm_new	= audin_toddr_pcm_new,
		.probe		= audin_toddr_dai_probe,
		.remove		= audin_toddr_dai_remove,
	},
	[CPU_AUDIN_TODDR_1] = {
		.name = "toDDR 1",
		.capture = {
			.stream_name	= "toDDR 1 Capture",
			.channels_min	= 1,
			.channels_max	= 8,
			.rates		= SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min	= 5512,
			.rate_max	= 192000,
			.formats	= AUDIN_FORMATS,
		},
		.ops		= &audin_toddr_dai_ops,
		.pcm_new	= audin_toddr_pcm_new,
		.probe		= audin_toddr_dai_probe,
		.remove		= audin_toddr_dai_remove,
	},
	[CPU_AUDIN_TODDR_2] = {
		.name = "toDDR 2",
		.capture = {
			.stream_name	= "toDDR 2 Capture",
			.channels_min	= 1,
			.channels_max	= 8,
			.rates		= SNDRV_PCM_RATE_CONTINUOUS,
			.rate_min	= 5512,
			.rate_max	= 192000,
			.formats	= AUDIN_FORMATS,
		},
		.ops		= &audin_toddr_dai_ops,
		.pcm_new	= audin_toddr_pcm_new,
		.probe		= audin_toddr_dai_probe,
		.remove		= audin_toddr_dai_remove,
	},
	[CPU_AUDIN_I2S_DECODER] = {
		.name = "I2S Decoder",
		.capture = {
			.stream_name = "I2S Decoder Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = AUDIN_FORMATS,
		},
		.ops = &audin_i2s_decoder_dai_ops,
	},
};

static const struct regmap_config aiu_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x308,
};

static int audin_clk_get(struct device *dev)
{
	struct audin *audin = dev_get_drvdata(dev);
	int ret;

	audin->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(audin->pclk))
		return dev_err_probe(dev, PTR_ERR(audin->pclk),
					"Can't get pclk\n");

	audin->i2s_input_clk = devm_clk_get(dev, "i2s_input_clk");
	if (IS_ERR(audin->i2s_input_clk))
		return dev_err_probe(dev, PTR_ERR(audin->i2s_input_clk),
					"Can't get i2s_input_clk\n");

	ret = devm_add_action_or_reset(dev,
				       (void(*)(void *))clk_disable_unprepare,
				       audin->pclk);
	if (ret)
		dev_err(dev, "failed to add reset action on pclk");

	return ret;
}

static int audin_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	struct regmap *map;
	struct audin *audin;
	int ret;

	audin = devm_kzalloc(dev, sizeof(*audin), GFP_KERNEL);
	if (!audin)
		return -ENOMEM;

	platform_set_drvdata(pdev, audin);

	ret = device_reset(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset device\n");

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	map = devm_regmap_init_mmio(dev, regs, &aiu_regmap_cfg);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(map));
		return PTR_ERR(map);
	}
	audin->regmap = map;

	audin->irq = platform_get_irq_byname(pdev, "audin");
	if (audin->irq < 0)
		return audin->irq;

	ret = audin_clk_get(dev);
	if (ret)
		return ret;
	ret = snd_soc_register_component(dev, &audin_cpu_component,
					 audin_cpu_dai_drv,
					 ARRAY_SIZE(audin_cpu_dai_drv));
	if (ret) {
		dev_err(dev, "Failed to register cpu component\n");
		return ret;
	}

#if defined(CONFIG_DEBUG_FS)
	ret = audin_create_debugfs(pdev);
	if (ret) {
		dev_err(dev, "Error: unable to create debugfs");
		return ret;
	}
#endif

	dev_info(dev, "Probe completed\n");

	return 0;
}

static int audin_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static const struct of_device_id audin_of_match[] = {
	{ .compatible = "amlogic,audin-gxbb" },
	{}
};
MODULE_DEVICE_TABLE(of, aiu_of_match);

static struct platform_driver audin_pdrv = {
	.probe = audin_probe,
	.remove = audin_remove,
	.driver = {
		.name = "meson-audin",
		.of_match_table = audin_of_match,
	},
};
module_platform_driver(audin_pdrv);

MODULE_DESCRIPTION("Meson AUDIN Driver");
MODULE_AUTHOR("Valerio Setti <vsetti@baylibre.com>");
MODULE_LICENSE("GPL v2");
