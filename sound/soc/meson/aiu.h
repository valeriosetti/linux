/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_AIU_H
#define _MESON_AIU_H

struct clk;
struct clk_bulk_data;
struct device;
struct of_phandle_args;
struct snd_soc_dai;
struct snd_soc_dai_ops;

enum aiu_clk_ids {
	PCLK = 0,
	AOCLK,
	MCLK,
	MIXER,
	AUDIN,
};

struct aiu_interface {
	struct clk_bulk_data *clks;
	unsigned int clk_num;
	int irq;
	int audin_irq;
};

struct aiu_platform_data {
	bool has_acodec;
	bool has_clk_ctrl_more_i2s_div;
};

struct aiu {
	struct clk *spdif_mclk;
	struct aiu_interface i2s;
	struct aiu_interface spdif;
	const struct aiu_platform_data *platform;
	struct regmap *aiu_regmap;
	struct regmap *audin_regmap;
};

#define AIU_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |	\
		     SNDRV_PCM_FMTBIT_S20_LE |	\
		     SNDRV_PCM_FMTBIT_S24_LE)

int aiu_of_xlate_dai_name(struct snd_soc_component *component,
			  const struct of_phandle_args *args,
			  const char **dai_name,
			  unsigned int component_id);

int aiu_hdmi_ctrl_register_component(struct device *dev);
int aiu_acodec_ctrl_register_component(struct device *dev);

int aiu_fifo_i2s_dai_probe(struct snd_soc_dai *dai);
int aiu_fifo_spdif_dai_probe(struct snd_soc_dai *dai);

extern const struct snd_soc_dai_ops aiu_fifo_i2s_dai_ops;
extern const struct snd_soc_dai_ops aiu_fifo_spdif_dai_ops;
extern const struct snd_soc_dai_ops aiu_encoder_i2s_dai_ops;
extern const struct snd_soc_dai_ops aiu_encoder_spdif_dai_ops;

#define AIU_IEC958_BPF			0x000
#define AIU_958_MISC			0x010
#define AIU_IEC958_DCU_FF_CTRL		0x01c
#define AIU_958_CHSTAT_L0		0x020
#define AIU_958_CHSTAT_L1		0x024
#define AIU_958_CTRL			0x028
#define AIU_I2S_SOURCE_DESC		0x034
#define AIU_I2S_DAC_CFG			0x040
#define AIU_I2S_SYNC			0x044
#define AIU_I2S_MISC			0x048
#define AIU_RST_SOFT			0x054
#define AIU_CLK_CTRL			0x058
#define AIU_CLK_CTRL_MORE		0x064
#define AIU_CODEC_DAC_LRCLK_CTRL	0x0a0
#define AIU_HDMI_CLK_DATA_CTRL		0x0a8
#define AIU_ACODEC_CTRL			0x0b0
#define AIU_958_CHSTAT_R0		0x0c0
#define AIU_958_CHSTAT_R1		0x0c4
#define AIU_MEM_I2S_START		0x180
#define AIU_MEM_I2S_MASKS		0x18c
#define AIU_MEM_I2S_CONTROL		0x190
#define AIU_MEM_IEC958_START		0x194
#define AIU_MEM_IEC958_CONTROL		0x1a4
#define AIU_MEM_I2S_BUF_CNTL		0x1d8
#define AIU_MEM_IEC958_BUF_CNTL		0x1fc

#define AUDIN_REGS_OFFSET		0x4c00

/* I2SIN_CTRL register and bits */
#define AUDIN_I2SIN_CTRL	(AUDIN_REGS_OFFSET + 0x040)	/* Reg index = 0x10 */
	#define AUDIN_I2SIN_CTRL_I2SIN_DIR		BIT(0)
	#define AUDIN_I2SIN_CTRL_I2SIN_CLK_SEL		BIT(1)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SEL	BIT(2)
	#define AUDIN_I2SIN_CTRL_I2SIN_POS_SYNC		BIT(3)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_SKEW_MASK	GENMASK(6, 4)
	#define AUDIN_I2SIN_CTRL_I2SIN_LRCLK_INV	BIT(7)
	#define AUDIN_I2SIN_CTRL_I2SIN_SIZE_MASK	GENMASK(9, 8)
	#define AUDIN_I2SIN_CTRL_I2SIN_CHAN_EN_MASK	GENMASK(13, 10)
	#define AUDIN_I2SIN_CTRL_I2SIN_EN		BIT(15)

/* FIFO0 registers */
#define AUDIN_FIFO0_START	(AUDIN_REGS_OFFSET + 0x080)	/* Reg index = 0x20 */
#define AUDIN_FIFO0_END		(AUDIN_REGS_OFFSET + 0x084)	/* Reg index = 0x21 */
#define AUDIN_FIFO0_PTR		(AUDIN_REGS_OFFSET + 0x088)	/* Reg index = 0x22 */
#define AUDIN_FIFO0_INTR	(AUDIN_REGS_OFFSET + 0x08C)	/* Reg index = 0x23 */
#define AUDIN_FIFO0_RDPTR	(AUDIN_REGS_OFFSET + 0x090)	/* Reg index = 0x24 */
#define AUDIN_FIFO0_WRAP	(AUDIN_REGS_OFFSET + 0x0C4)	/* Reg index = 0x31 */

/* FIFO1 registers */
#define AUDIN_FIFO1_START	(AUDIN_REGS_OFFSET + 0x0CC)	/* Reg index = 0x33 */
#define AUDIN_FIFO1_END		(AUDIN_REGS_OFFSET + 0x0D0)	/* Reg index = 0x34 */
#define AUDIN_FIFO1_PTR		(AUDIN_REGS_OFFSET + 0x0D4)	/* Reg index = 0x35 */
#define AUDIN_FIFO1_INTR	(AUDIN_REGS_OFFSET + 0x0D8)	/* Reg index = 0x36 */
#define AUDIN_FIFO1_RDPTR	(AUDIN_REGS_OFFSET + 0x0DC)	/* Reg index = 0x37 */
#define AUDIN_FIFO1_WRAP	(AUDIN_REGS_OFFSET + 0x110)	/* Reg index = 0x44 */

/* FIFO2 registers */
#define AUDIN_FIFO2_START	(AUDIN_REGS_OFFSET + 0x114)	/* Reg index = 0x45 */
#define AUDIN_FIFO2_END		(AUDIN_REGS_OFFSET + 0x118)	/* Reg index = 0x46 */
#define AUDIN_FIFO2_PTR		(AUDIN_REGS_OFFSET + 0x11C)	/* Reg index = 0x47 */
#define AUDIN_FIFO2_INTR	(AUDIN_REGS_OFFSET + 0x120)	/* Reg index = 0x48 */
#define AUDIN_FIFO2_RDPTR	(AUDIN_REGS_OFFSET + 0x124)	/* Reg index = 0x49 */
#define AUDIN_FIFO2_WRAP	(AUDIN_REGS_OFFSET + 0x140)	/* Reg index = 0x50 */

/* FIFOx CTRL registers and bits */
#define AUDIN_FIFO0_CTRL	(AUDIN_REGS_OFFSET + 0x094)	/* Reg index = 0x25 */
#define AUDIN_FIFO1_CTRL	(AUDIN_REGS_OFFSET + 0x0E0)	/* Reg index = 0x38 */
#define AUDIN_FIFO2_CTRL	(AUDIN_REGS_OFFSET + 0x128)	/* Reg index = 0x4a */
	#define AUDIN_FIFO_CTRL_EN		BIT(0)
	#define AUDIN_FIFO_CTRL_RST		BIT(1)
	#define AUDIN_FIFO_CTRL_LOAD		BIT(2)
	#define AUDIN_FIFO_CTRL_DIN_SEL_OFF	3
	#define AUDIN_FIFO_CTRL_DIN_SEL_MASK	GENMASK(5, 3)
	#define AUDIN_FIFO_CTRL_ENDIAN_MASK	GENMASK(10, 8)
	#define AUDIN_FIFO_CTRL_CHAN_MASK	GENMASK(14, 11)
	#define AUDIN_FIFO_CTRL_UG		BIT(15)

/* FIFOx_CTRL1 registers and bits */
#define AUDIN_FIFO0_CTRL1	(AUDIN_REGS_OFFSET + 0x098)	/* Reg index = 0x26 */
#define AUDIN_FIFO1_CTRL1	(AUDIN_REGS_OFFSET + 0x0E4)	/* Reg index = 0x39 */
#define AUDIN_FIFO2_CTRL1	(AUDIN_REGS_OFFSET + 0x12C)	/* Reg index = 0x4b */
	#define AUDIN_FIFO_CTRL1_DIN_POS_2		BIT(7)
	#define AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK	GENMASK(3, 2)
	#define AUDIN_FIFO_CTRL1_DIN_POS_01_MASK	GENMASK(1, 0)

/* INT_CTRL register and bits */
#define AUDIN_INT_CTRL		(AUDIN_REGS_OFFSET + 0x144)	/* Reg index = 0x51 */
	#define AUDIN_INT_CTRL_FIFO0_OVERFLOW		BIT(0)
	#define AUDIN_INT_CTRL_FIFO0_ADDR_TRIG		BIT(1)
	#define AUDIN_INT_CTRL_FIFO1_OVERFLOW		BIT(2)
	#define AUDIN_INT_CTRL_FIFO1_ADDR_TRIG		BIT(3)
	#define AUDIN_INT_CTRL_FIFO2_OVERFLOW		BIT(11)
	#define AUDIN_INT_CTRL_FIFO2_ADDR_TRIG		BIT(12)

/* FIFO_INT register and bits */
#define AUDIN_FIFO_INT		(AUDIN_REGS_OFFSET + 0x148)	/* Reg index = 0x52 */
	#define AUDIN_FIFO_INT_FIFO0_OVERFLOW		BIT(0)
	#define AUDIN_FIFO_INT_FIFO0_ADDR_TRIG		BIT(1)
	#define AUDIN_FIFO_INT_FIFO1_OVERFLOW		BIT(2)
	#define AUDIN_FIFO_INT_FIFO1_ADDR_TRIG		BIT(3)
	#define AUDIN_FIFO_INT_FIFO2_OVERFLOW		BIT(11)
	#define AUDIN_FIFO_INT_FIFO2_ADDR_TRIG		BIT(12)

#define AUDIN_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
		       SNDRV_PCM_FMTBIT_S24_LE)

extern const struct snd_soc_dai_ops audin_toddr_dai_ops;
extern const struct snd_soc_dai_ops audin_i2s_decoder_dai_ops;
extern struct device_attribute dev_attr_dump_regs;

int audin_create_debugfs(struct snd_soc_component *component);
int audin_toddr_dai_probe(struct snd_soc_dai *dai);
int audin_toddr_dai_remove(struct snd_soc_dai *dai);
int audin_toddr_pcm_new(struct snd_soc_pcm_runtime *rtd,
			struct snd_soc_dai *dai);
snd_pcm_uframes_t audin_toddr_pointer(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream);

#endif /* _MESON_AIU_H */
