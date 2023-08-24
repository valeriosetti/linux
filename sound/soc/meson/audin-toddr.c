// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "audin.h"

/* Bits of the FIFO CTRL register */
#define AUDIN_FIFO_CTRL_EN		BIT(0)
#define AUDIN_FIFO_CTRL_RST		BIT(1)
#define AUDIN_FIFO_CTRL_LOAD		BIT(2)
#define AUDIN_FIFO_CTRL_DIN_SEL_MASK	GENMASK(5, 3)
#define AUDIN_FIFO_CTRL_ENDIAN_MASK	GENMASK(10, 8)
#define AUDIN_FIFO_CTRL_CHAN_MASK	GENMASK(14, 11)
#define AUDIN_FIFO_CTRL_UG		BIT(15)

/* Bits of the FIFO CTRL1 register */
#define AUDIN_FIFO_CTRL1_DIN_POS_2		BIT(7)
#define AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK	GENMASK(3, 2)
#define AUDIN_FIFO_CTRL1_DIN_POS_01_MASK	GENMASK(1, 0)

/* Bits of the FIFO REQID register */
#define AUDIN_FIFO_REQID_AUDIN_REQ_BURST_NUM_MASK	GENMASK(17, 12)
#define AUDIN_FIFO_REQID_FIFO_REQ_BURST_NUM_MASK	GENMASK(11, 6)
#define AUDIN_FIFO_REQID_FIFO_REQ_ID_MASK		GENMASK(5, 0)

/* Bits of the INT CTRL register */
#define AUDIN_INT_CTRL_FIFO0_OVERFLOW		BIT(0)
#define AUDIN_INT_CTRL_FIFO0_ADDR_TRIG		BIT(1)
#define AUDIN_INT_CTRL_FIFO1_OVERFLOW		BIT(2)
#define AUDIN_INT_CTRL_FIFO1_ADDR_TRIG		BIT(3)
#define AUDIN_INT_CTRL_FIFO2_OVERFLOW		BIT(11)
#define AUDIN_INT_CTRL_FIFO2_ADDR_TRIG		BIT(12)

/* Bits of the FIFO INT register */
#define AUDIN_FIFO_INT_FIFO0_OVERFLOW		BIT(0)
#define AUDIN_FIFO_INT_FIFO0_ADDR_TRIG		BIT(1)
#define AUDIN_FIFO_INT_FIFO1_OVERFLOW		BIT(2)
#define AUDIN_FIFO_INT_FIFO1_ADDR_TRIG		BIT(3)
#define AUDIN_FIFO_INT_FIFO2_OVERFLOW		BIT(11)
#define AUDIN_FIFO_INT_FIFO2_ADDR_TRIG		BIT(12)

struct fifo_regs {
	unsigned int start;
	unsigned int end;
	unsigned int ptr;
	unsigned int intr;
	unsigned int rdptr;
	unsigned int ctrl;
	unsigned int ctrl1;
	unsigned int reqid;
	unsigned int wrap;
};

struct fifo_regs_bit_masks {
	unsigned int overflow_en;
	unsigned int addr_trigger_en;
	unsigned int overflow_set;
	unsigned int addr_trigger_set;
};

#define AUDIN_FIFO_COUNT	3

struct fifo_regs audin_fifo_regs[AUDIN_FIFO_COUNT] = {
	[0] = {
		.start	= AUDIN_FIFO0_START,
		.end	= AUDIN_FIFO0_END,
		.ptr	= AUDIN_FIFO0_PTR,
		.intr	= AUDIN_FIFO0_INTR,
		.rdptr	= AUDIN_FIFO0_RDPTR,
		.ctrl	= AUDIN_FIFO0_CTRL,
		.ctrl1	= AUDIN_FIFO0_CTRL1,
		.reqid	= AUDIN_FIFO0_REQID,
		.wrap	= AUDIN_FIFO0_WRAP,
	},
	[1] = {
		.start	= AUDIN_FIFO1_START,
		.end	= AUDIN_FIFO1_END,
		.ptr	= AUDIN_FIFO1_PTR,
		.intr	= AUDIN_FIFO1_INTR,
		.rdptr	= AUDIN_FIFO1_RDPTR,
		.ctrl	= AUDIN_FIFO1_CTRL,
		.ctrl1	= AUDIN_FIFO1_CTRL1,
		.reqid	= AUDIN_FIFO1_REQID,
		.wrap	= AUDIN_FIFO1_WRAP,
	},
	[2] = {
		.start	= AUDIN_FIFO2_START,
		.end	= AUDIN_FIFO2_END,
		.ptr	= AUDIN_FIFO2_PTR,
		.intr	= AUDIN_FIFO2_INTR,
		.rdptr	= AUDIN_FIFO2_RDPTR,
		.ctrl	= AUDIN_FIFO2_CTRL,
		.ctrl1	= AUDIN_FIFO2_CTRL1,
		.reqid	= AUDIN_FIFO2_REQID,
		.wrap	= AUDIN_FIFO2_WRAP,
	}
};

struct fifo_regs_bit_masks auding_fifo_regs_bit_masks[AUDIN_FIFO_COUNT] = {
	[0] = {
		.overflow_en = AUDIN_INT_CTRL_FIFO0_OVERFLOW,
		.addr_trigger_en = AUDIN_INT_CTRL_FIFO0_ADDR_TRIG,
		.overflow_set = AUDIN_FIFO_INT_FIFO0_OVERFLOW,
		.addr_trigger_set = AUDIN_FIFO_INT_FIFO0_ADDR_TRIG,
	},
	[1] = {
		.overflow_en = AUDIN_INT_CTRL_FIFO1_OVERFLOW,
		.addr_trigger_en = AUDIN_INT_CTRL_FIFO1_ADDR_TRIG,
		.overflow_set = AUDIN_FIFO_INT_FIFO1_OVERFLOW,
		.addr_trigger_set = AUDIN_FIFO_INT_FIFO1_ADDR_TRIG,
	},
	[2] = {
		.overflow_en = AUDIN_INT_CTRL_FIFO2_OVERFLOW,
		.addr_trigger_en = AUDIN_INT_CTRL_FIFO2_ADDR_TRIG,
		.overflow_set = AUDIN_FIFO_INT_FIFO2_OVERFLOW,
		.addr_trigger_set = AUDIN_FIFO_INT_FIFO2_ADDR_TRIG,
	},

};

/* This value is connected to the interrupt's periodicity: an IRQ will be
 * generated when the input buffer has been written to multiple of this
 * value. */
#define AUDIN_FIFO_I2S_BLOCK		4096
#define AUDIN_FIFO_I2S_BLOCK_MASK	(~(AUDIN_FIFO_I2S_BLOCK - 1))

static struct snd_pcm_hardware toddr_pcm_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = AUDIN_FORMATS,
	.rate_min = 5512,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = 2*AUDIN_FIFO_I2S_BLOCK,
	.period_bytes_max = AUDIN_FIFO_I2S_BLOCK * USHRT_MAX,
	.periods_min = 2,
	.periods_max = UINT_MAX,

	/* No real justification for this */
	.buffer_bytes_max = 1 * 1024 * 1024,
};

struct audin_fifo {
	struct fifo_regs *reg;
	struct fifo_regs_bit_masks *reg_bit_masks;
	struct snd_pcm_hardware *pcm_hw;
};

static int audin_toddr_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct audin_fifo *fifo = dai->capture_dma_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_soc_component_update_bits(component, fifo->reg->ctrl,
					      AUDIN_FIFO_CTRL_EN,
					      AUDIN_FIFO_CTRL_EN);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_soc_component_update_bits(component, fifo->reg->ctrl,
					      AUDIN_FIFO_CTRL_EN, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int audin_toddr_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct audin_fifo *fifo = dai->capture_dma_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	dma_addr_t dma_end = runtime->dma_addr + runtime->dma_bytes - 8;
	unsigned int val;

	/* Setup memory boundaries */
	snd_soc_component_write(component, fifo->reg->start, runtime->dma_addr);
	snd_soc_component_write(component, fifo->reg->ptr, runtime->dma_addr);
	snd_soc_component_write(component, fifo->reg->end, dma_end);

	/* Set when to trigger the 1st interrupt */
	snd_soc_component_write(component, fifo->reg->intr,
				runtime->dma_addr + AUDIN_FIFO_I2S_BLOCK - 1);

	/* Load new addresses */
	val = AUDIN_FIFO_CTRL_LOAD | AUDIN_FIFO_CTRL_UG;
	snd_soc_component_update_bits(component, fifo->reg->ctrl, val, val);

	/* Reset */
	snd_soc_component_update_bits(dai->component, fifo->reg->ctrl,
				      AUDIN_FIFO_CTRL_RST,
				      AUDIN_FIFO_CTRL_RST);

	return 0;
}

static int audin_toddr_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct audin_fifo *fifo = dai->capture_dma_data;
	unsigned int val;

	/* FIFO_CTRL_ENDIAN and _FIFO_CTRL1_DIN_POS regs are undocumented
	 * in the manual. Manufacturer's driver was only configuring the endianness
	 * to an hardcoded 4, which however does not work for 16 bit mode.
	 * Therefore through several attempts I found these working configurations:
	 * - 16 bit mode:
	 * 		- DIN_POS = 1
	 * 		- ENDIAN = 6
	 * - 32 bit
	 * 		- DIN_POS = 0
	 * 		- ENDIAN = 4 */

	switch (params_physical_width(params)) {
	case 16:
		val = FIELD_PREP(AUDIN_FIFO_CTRL_ENDIAN_MASK, 6);
		snd_soc_component_update_bits(component, fifo->reg->ctrl,
									  AUDIN_FIFO_CTRL_ENDIAN_MASK, val);

		val = FIELD_PREP(AUDIN_FIFO_CTRL1_DIN_POS_01_MASK, 1);
		snd_soc_component_update_bits(component, fifo->reg->ctrl1,
									  AUDIN_FIFO_CTRL1_DIN_POS_01_MASK, val);

		/* Set sample size to 2 bytes (16 bit) */
		val = FIELD_PREP(AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK, 1);
		snd_soc_component_update_bits(component, fifo->reg->ctrl1,
				      				  AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK, val);
		break;
	case 24:
	case 32:
		val = FIELD_PREP(AUDIN_FIFO_CTRL_ENDIAN_MASK, 4);
		snd_soc_component_update_bits(component, fifo->reg->ctrl,
									  AUDIN_FIFO_CTRL_ENDIAN_MASK, val);

		val = FIELD_PREP(AUDIN_FIFO_CTRL1_DIN_POS_01_MASK, 0);
		snd_soc_component_update_bits(component, fifo->reg->ctrl1,
									  AUDIN_FIFO_CTRL1_DIN_POS_01_MASK, val);

		/* Set sample size to 3 bytes (24 bit) */
		val = FIELD_PREP(AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK, 2);
		snd_soc_component_update_bits(component, fifo->reg->ctrl1,
				      				  AUDIN_FIFO_CTRL1_DIN_BYTE_NUM_MASK, val);
		break;
	default:
		dev_err(dai->dev, "Unsupported physical width %u\n",
			params_physical_width(params));
		return -EINVAL;
	}

	/* Set channel count. Since the platform has a single pin for I2S input
	 * it can only support 2 channels, so we hardcode this value. */
	val = FIELD_PREP(AUDIN_FIFO_CTRL_CHAN_MASK, 2);
	snd_soc_component_update_bits(component, fifo->reg->ctrl,
				      AUDIN_FIFO_CTRL_CHAN_MASK, val);

	return 0;
}

static irqreturn_t audin_fifo_isr(int irq, void *substream)
{
	struct snd_pcm_substream *pcm_substream = substream;
	struct snd_soc_pcm_runtime *rtd = pcm_substream->private_data;
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct audin_fifo *fifo = dai->capture_dma_data;
	unsigned int irq_val, old_int_val, new_int_val, start, end;

	snd_pcm_period_elapsed(pcm_substream);

	irq_val = snd_soc_component_read(dai->component, AUDIN_FIFO_INT);
	if (irq_val & AUDIN_FIFO_INT_FIFO0_OVERFLOW) {
		dev_warn(dai->dev, "Warning: FIFO overflow\n");
	}

	/* Set IRQ for the next block (wrap when necessary)*/
	start = snd_soc_component_read(dai->component, fifo->reg->start);
	end = snd_soc_component_read(dai->component, fifo->reg->end);
	old_int_val = snd_soc_component_read(dai->component, fifo->reg->intr);
	new_int_val = old_int_val + AUDIN_FIFO_I2S_BLOCK;
	if (new_int_val > end)
		new_int_val = start + AUDIN_FIFO_I2S_BLOCK - 1;
	snd_soc_component_write(dai->component, fifo->reg->intr, new_int_val);

	/* Clear IRQ flag(s) by writing 1 on the bits which are set */
	snd_soc_component_update_bits(dai->component, AUDIN_FIFO_INT,
								  AUDIN_FIFO_INT_FIFO0_OVERFLOW |
								  AUDIN_FIFO_INT_FIFO0_ADDR_TRIG, irq_val);

	return IRQ_HANDLED;
}

static struct audin *audin_from_dai(struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	return snd_soc_component_get_drvdata(component);
}

int audin_toddr_startup(struct snd_pcm_substream *substream,
		 	struct snd_soc_dai *dai)
{
	struct audin_fifo *fifo = dai->capture_dma_data;
	struct audin *audin = audin_from_dai(dai);
	unsigned int val;
	int ret;

	snd_soc_set_runtime_hwparams(substream, fifo->pcm_hw);

	/* Check runtime parameters */
	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 AUDIN_FIFO_I2S_BLOCK);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 AUDIN_FIFO_I2S_BLOCK);
	if (ret)
		return ret;

	ret = clk_prepare_enable(audin->pclk);
	if (ret)
		return ret;

	/* Select I2S source. This is hardcoded since it's the only one
	 * that has been tested. According to the manual options are:
	 * - 0: SPDIF in
	 * - 1: I2S in
	 * - 2: PCM in
	 * - 3: HDMI in
	 * - 4: demodulator in */
	val = FIELD_PREP(AUDIN_FIFO_CTRL_DIN_SEL_MASK, 1);
	snd_soc_component_update_bits(dai->component, fifo->reg->ctrl,
				      AUDIN_FIFO_CTRL_DIN_SEL_MASK, val);

	/* Clear any previous pending IRQ flag before enabling it */
	val = AUDIN_FIFO_INT_FIFO0_ADDR_TRIG | AUDIN_FIFO_INT_FIFO0_OVERFLOW;
	snd_soc_component_update_bits(dai->component, AUDIN_FIFO_INT, val, val);

	ret = request_irq(audin->irq, audin_fifo_isr, 0, dev_name(dai->dev),
			  substream);
	if (ret) {
		clk_disable_unprepare(audin->pclk);
		return ret;
	}
	
	/* Unmask FIFO IRQ bits */
	val = AUDIN_INT_CTRL_FIFO0_OVERFLOW | AUDIN_INT_CTRL_FIFO0_ADDR_TRIG;
	snd_soc_component_update_bits(dai->component, AUDIN_INT_CTRL, val, 0);

	return ret;
}

void audin_toddr_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct audin *audin = audin_from_dai(dai);

	free_irq(audin->irq, substream);
	// clk_disable_unprepare(audin->pclk);
}

const struct snd_soc_dai_ops audin_toddr_dai_ops = {
	.trigger	= audin_toddr_trigger,
	.prepare	= audin_toddr_prepare,
	.hw_params	= audin_toddr_hw_params,
	.startup	= audin_toddr_startup,
	.shutdown	= audin_toddr_shutdown,
};

snd_pcm_uframes_t audin_toddr_pointer(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct audin_fifo *fifo = dai->capture_dma_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int start, ptr;

	start = snd_soc_component_read(component, fifo->reg->start);
	ptr = snd_soc_component_read(component, fifo->reg->ptr);

	return bytes_to_frames(runtime, ptr - start);
}

static __maybe_unused void dump_buffer(uint8_t *buf, unsigned int len, int size)
{
	int i;
	char output[2000] = { 0 };
	char *out_ptr = output;
	uint32_t *u32_ptr = (uint32_t *) buf;
	uint16_t *u16_ptr = (uint16_t *) buf;
	int16_t s16_val;

	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			out_ptr += sprintf(out_ptr, "\n");
		}
		if (size == 0) { // Raw
			out_ptr += sprintf(out_ptr, "%02x ", buf[i]);
		} else if (size == 16) {
			s16_val = (int16_t) u16_ptr[i];
			out_ptr += sprintf(out_ptr, "%hd ", s16_val);
		} else if (size == 24) {
			/* Convert 24 bit to 16 bit for printing */
			s16_val = (int16_t) ((u32_ptr[i] >> 8) & 0xFFFF);
			out_ptr += sprintf(out_ptr, "%hd ", s16_val);
		}
	}
	DEBUG_MSG("%s", output);
}

static void reorder_buffer_s16(unsigned char *buf, unsigned long len)
{
	unsigned long i;
	uint16_t tmp_buf[32];
	uint16_t *ptr = (uint16_t *) buf;
	uint16_t *end = (uint16_t *) (buf + len);

	while (ptr < end) {
		for (i = 0; i < 16; i++) {
			tmp_buf[i * 2] = ptr[i];
			tmp_buf[1 + i * 2] = ptr[16 + i];
		}
		memcpy(ptr, tmp_buf, sizeof(tmp_buf));
		ptr += 32;
	}
}

static void reorder_buffer_s24_s32(unsigned char *buf, unsigned long len)
{
	unsigned long i;
	uint32_t tmp_buf[16];
	uint32_t *ptr = (uint32_t *) buf;
	uint32_t *end = (uint32_t *) (buf + len);

	while (ptr < end) {
		for (i = 0; i < 8; i++) {
			tmp_buf[i * 2] = ptr[i];
			tmp_buf[1 + i * 2] = ptr[8 + i];
		}
		memcpy(ptr, tmp_buf, sizeof(tmp_buf));
		ptr += 16;
	}
}

int audin_toddr_copy_user(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *buf,
			 unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	
	unsigned char *ptr = runtime->dma_area + pos;
	int ret;

	/* Buffer must be multiple of 64 bytes*/
	if (bytes % 64) {
		dev_err(component->dev,
			"Error: buffer size (%lx) is not multiple of 64", bytes);
		return -EINVAL;
	}

	// dump_buffer(ptr, 64, 0);

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		reorder_buffer_s16(ptr, bytes);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		reorder_buffer_s24_s32(ptr, bytes);
		break;
	default:
		dev_err(component->dev,
			"Error: unsupported format %x", runtime->format);
		return -EINVAL;
	}

	// dump_buffer(ptr, 32, 0);

	ret = copy_to_user(buf, ptr, bytes);
	if (ret < 0) {
		return ret;
	}

	return bytes;
}

int audin_toddr_dai_probe(struct snd_soc_dai *dai)
{
	struct audin_fifo *fifo;

	fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;

	if (dai->id >= AUDIN_FIFO_COUNT) {
		dev_err(dai->dev, "Error: invalid DAI ID (%d)", dai->id);
		return -EINVAL;
	}

	fifo->reg = &audin_fifo_regs[dai->id];
	fifo->reg_bit_masks = &auding_fifo_regs_bit_masks[dai->id];
	fifo->pcm_hw = &toddr_pcm_hw;

	dai->capture_dma_data = fifo;

	return 0;
}

int audin_toddr_dai_remove(struct snd_soc_dai *dai)
{
	kfree(dai->capture_dma_data);

	return 0;
}

int audin_toddr_pcm_new(struct snd_soc_pcm_runtime *rtd,
			struct snd_soc_dai *dai)
{
	struct snd_card *card = rtd->card->snd_card;
	struct audin_fifo *fifo = dai->capture_dma_data;
	size_t size = fifo->pcm_hw->buffer_bytes_max;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       card->dev, size, size);

	return 0;
}