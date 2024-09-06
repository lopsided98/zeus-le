/*
 * Copyright (c) 2019 Intel Corporation
 * Copyright (c) 2024 Ben Wolsieffer
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tlv320adcx120

#include "tlv320adcx120.h"

#include <errno.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "input_codec.h"

LOG_MODULE_REGISTER(tlv320adcx120, CONFIG_AUDIO_CODEC_LOG_LEVEL);

#define CODEC_OUTPUT_VOLUME_MAX 0
#define CODEC_OUTPUT_VOLUME_MIN (-78 * 2)

struct codec_channel_config {
	uint8_t channel;
	bool line_in;
	bool dc_coupled;
	uint16_t impedance_ohms;
	uint8_t slot;
};

struct codec_driver_config {
	struct i2c_dt_spec bus;
	const struct device *avdd_supply;
	struct gpio_dt_spec int_gpio;
	bool internal_areg;
	uint8_t num_channels;
	const struct codec_channel_config *channels;
};

struct codec_driver_data {
	struct reg_addr reg_addr_cache;
};

static int codec_write_reg(const struct device *dev, struct reg_addr reg, uint8_t val);
static int codec_read_reg(const struct device *dev, struct reg_addr reg, uint8_t *val);
static int codec_soft_reset(const struct device *dev);
static int codec_configure_dai(const struct device *dev, const audio_dai_cfg_t *cfg);
static int codec_configure_power(const struct device *dev);
static int codec_configure_input(const struct device *dev);
static int codec_set_input_volume(const struct device *dev, int vol);

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
static void codec_read_all_regs(const struct device *dev);
#define CODEC_DUMP_REGS(dev) codec_read_all_regs((dev))
#else
#define CODEC_DUMP_REGS(dev)
#endif

static int codec_initialize(const struct device *dev)
{
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	if (!device_is_ready(dev_cfg->bus.bus)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	if (dev_cfg->int_gpio.port) {
		if (!gpio_is_ready_dt(&dev_cfg->int_gpio)) {
			LOG_ERR("GPIO device not ready");
			return -ENODEV;
		}
	}

#if IS_ENABLED(CONFIG_REGULATOR)
	if (dev_cfg->avdd_supply != NULL) {
		ret = regulator_enable(dev_cfg->avdd_supply);
		if (ret < 0) {
			return ret;
		}
	}
#else
	(void)ret;
#endif

	return 0;
}

static int codec_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("dai_type must be AUDIO_DAI_TYPE_I2S");
		return -EINVAL;
	}

	if (dev_cfg->int_gpio.port) {
		ret = gpio_pin_configure_dt(&dev_cfg->int_gpio, GPIO_INPUT);
		if (ret < 0) {
			return ret;
		}
	}

	ret = codec_soft_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = codec_configure_power(dev);
	if (ret < 0) {
		return ret;
	}

	ret = codec_configure_dai(dev, &cfg->dai_cfg);
	if (ret < 0) {
		return ret;
	}

	return codec_configure_input(dev);
}

static int codec_start_input(const struct device *dev)
{
	int ret;

	/* power on ADC */
	ret = codec_write_reg(dev, PWR_CFG_ADDR, PWR_CFG_ADC_PDZ | PWR_CFG_PLL_PDZ);
	if (ret < 0) {
		return ret;
	}

	/* unmute ADC channels */
	// codec_write_reg(dev, VOL_CTRL_ADDR, VOL_CTRL_UNMUTE_DEFAULT);

	CODEC_DUMP_REGS(dev);
	return 0;
}

static int codec_stop_input(const struct device *dev)
{
	/* mute DAC channels */
	// codec_write_reg(dev, VOL_CTRL_ADDR, VOL_CTRL_MUTE_DEFAULT);

	/* power off ADC */
	return codec_write_reg(dev, PWR_CFG_ADDR, 0);
}

static void codec_mute_input(const struct device *dev)
{
	/* mute DAC channels */
	// codec_write_reg(dev, VOL_CTRL_ADDR, VOL_CTRL_MUTE_DEFAULT);
}

static void codec_unmute_input(const struct device *dev)
{
	/* unmute DAC channels */
	// codec_write_reg(dev, VOL_CTRL_ADDR, VOL_CTRL_UNMUTE_DEFAULT);
}

static int codec_set_property(const struct device *dev, enum input_codec_property property,
			      audio_channel_t channel, union input_codec_property_value val)
{
	/* individual channel control not currently supported */
	if (channel != AUDIO_CHANNEL_ALL) {
		LOG_ERR("channel %u invalid. must be AUDIO_CHANNEL_ALL", channel);
		return -EINVAL;
	}

	switch (property) {
	case INPUT_CODEC_PROPERTY_VOLUME:
		return codec_set_input_volume(dev, val.vol);

	case INPUT_CODEC_PROPERTY_MUTE:
		if (val.mute) {
			codec_mute_input(dev);
		} else {
			codec_unmute_input(dev);
		}
		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static int codec_apply_properties(const struct device *dev)
{
	/* nothing to do because there is nothing cached */
	return 0;
}

static int codec_write_reg(const struct device *dev, struct reg_addr reg, uint8_t val)
{
	struct codec_driver_data *const dev_data = dev->data;
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	/* set page if different */
	if (dev_data->reg_addr_cache.page != reg.page) {
		ret = i2c_reg_write_byte_dt(&dev_cfg->bus, PAGE_CFG_ADDR, reg.page);
		if (ret < 0) {
			return ret;
		}
		dev_data->reg_addr_cache.page = reg.page;
	}

	ret = i2c_reg_write_byte_dt(&dev_cfg->bus, reg.reg_addr, val);
	if (ret < 0) {
		return ret;
	}
	LOG_DBG("WR PG:%u REG:%02u VAL:0x%02x", reg.page, reg.reg_addr, val);
	return 0;
}

static int codec_read_reg(const struct device *dev, struct reg_addr reg, uint8_t *val)
{
	struct codec_driver_data *const dev_data = dev->data;
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	/* set page if different */
	if (dev_data->reg_addr_cache.page != reg.page) {
		ret = i2c_reg_write_byte_dt(&dev_cfg->bus, PAGE_CFG_ADDR, reg.page);
		if (ret < 0) {
			return ret;
		}
		dev_data->reg_addr_cache.page = reg.page;
	}

	i2c_reg_read_byte_dt(&dev_cfg->bus, reg.reg_addr, val);
	if (ret < 0) {
		return ret;
	}
	LOG_DBG("RD PG:%u REG:%02u VAL:0x%02x", reg.page, reg.reg_addr, *val);
	return 0;
}

static int codec_soft_reset(const struct device *dev)
{
	/* soft reset the DAC */
	return codec_write_reg(dev, SW_RESET_ADDR, SW_RESET_ASSERT);
}

static int codec_configure_power(const struct device *dev)
{
	const struct codec_driver_config *const cfg = dev->config;

	uint8_t val =
		FIELD_PREP(SLEEP_CFG_VREF_QCHG, SLEEP_CFG_VREF_QCHG_3_5_MS) | SLEEP_CFG_SLEEP_ENZ;
	if (cfg->internal_areg) {
		val |= SLEEP_CFG_AREG_SELECT;
	}

	return codec_write_reg(dev, SLEEP_CFG_ADDR, val);
}

static int codec_configure_dai(const struct device *dev, const audio_dai_cfg_t *cfg)
{
	int ret;
	uint8_t val = 0;

	/* configure I2S interface */
	uint8_t wlen;
	switch (cfg->i2s.word_size) {
	case AUDIO_PCM_WIDTH_16_BITS:
		wlen = ASI_CFG0_ASI_WLEN_16;
		break;
	case AUDIO_PCM_WIDTH_20_BITS:
		wlen = ASI_CFG0_ASI_WLEN_20;
		break;
	case AUDIO_PCM_WIDTH_24_BITS:
		wlen = ASI_CFG0_ASI_WLEN_24;
		break;
	case AUDIO_PCM_WIDTH_32_BITS:
		wlen = ASI_CFG0_ASI_WLEN_32;
		break;
	default:
		LOG_ERR("Unsupported PCM sample bit width %u", cfg->i2s.word_size);
		return -EINVAL;
	}
	val |= FIELD_PREP(ASI_CFG0_ASI_WLEN, wlen);

	uint8_t fmt;
	switch (cfg->i2s.format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
		fmt = ASI_CFG0_ASI_FORMAT_I2S;
		break;
	case I2S_FMT_DATA_FORMAT_PCM_LONG:
		fmt = ASI_CFG0_ASI_FORMAT_TDM;
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
		fmt = ASI_CFG0_ASI_FORMAT_LJ;
		break;
	default:
		LOG_ERR("Unsupported data format: 0x%02x", cfg->i2s.format);
		return -EINVAL;
	}
	val |= FIELD_PREP(ASI_CFG0_ASI_FORMAT, fmt);

	if (cfg->i2s.format & I2S_FMT_DATA_ORDER_LSB) {
		LOG_ERR("LSB first ordering not supported");
		return -EINVAL;
	}

	if (cfg->i2s.format & I2S_FMT_BIT_CLK_INV) {
		val |= ASI_CFG0_BCLK_POL;
	}

	if (cfg->i2s.format & I2S_FMT_FRAME_CLK_INV) {
		val |= ASI_CFG0_FSYNC_POL;
	}

	ret = codec_write_reg(dev, ASI_CFG0_ADDR, val);
	if (ret < 0) {
		return ret;
	}

	val = FIELD_PREP(MST_CFG0_MCLK_FREQ_SEL, MST_CFG0_MCLK_FREQ_SEL_13_MHZ);

	if ((cfg->i2s.options & I2S_OPT_BIT_CLK_SLAVE) &&
	    (cfg->i2s.options & I2S_OPT_FRAME_CLK_SLAVE)) {
		val |= MST_CFG0_MST_SLV_CFG;
	} else if ((cfg->i2s.options & I2S_OPT_BIT_CLK_SLAVE) ||
		   (cfg->i2s.options & I2S_OPT_FRAME_CLK_SLAVE)) {
		LOG_ERR("Master/slave status for bit clock and frame clock must match");
		return -EINVAL;
	}

	if (cfg->i2s.options & I2S_OPT_BIT_CLK_GATED) {
		val |= MST_CFG0_BCLK_FSYNC_GATE;
	}

	return codec_write_reg(dev, MST_CFG0_ADDR, val);
}

static int codec_configure_input(const struct device *dev)
{
	const struct codec_driver_config *const cfg = dev->config;
	int ret;
	uint8_t in_ch_en = 0;
	uint8_t asi_out_ch_en = 0;

	if (cfg->num_channels > 4) {
		LOG_ERR("Too many (%u) channels configured", cfg->num_channels);
		return -EINVAL;
	}

	for (uint8_t i = 0; i < cfg->num_channels; ++i) {
		const struct codec_channel_config *channel = &cfg->channels[i];
		uint8_t val = 0;

		if (channel->channel < 1 || channel->channel > 4) {
			LOG_ERR("Channel out of range: %u", channel->channel);
			return -EINVAL;
		}

		if (channel->line_in) {
			val |= CH_CFG0_INTYP;
		}
		if (channel->dc_coupled) {
			val |= CH_CFG0_DC;
		}

		uint8_t imp;
		switch (channel->impedance_ohms) {
		case 2500:
			imp = CH_CFG0_IMP_2_5_KOHM;
			break;
		case 10000:
			imp = CH_CFG0_IMP_10_KOHM;
			break;
		case 20000:
			imp = CH_CFG0_IMP_20_KOHM;
			break;
		default:
			return -EINVAL;
		}
		val |= FIELD_PREP(CH_CFG0_IMP, imp);

		in_ch_en |= IN_CH_EN(channel->channel);
		asi_out_ch_en |= ASI_OUT_CH_EN(channel->channel);

		ret = codec_write_reg(dev, CH_CFG0_ADDR(channel->channel), val);
		if (ret < 0) {
			return ret;
		}

		if (channel->slot > 63) {
			LOG_ERR("ASI slot out of range: %u > 63", channel->slot);
			return -EINVAL;
		}

		ret = codec_write_reg(dev, ASI_CH_ADDR(channel->channel),
				      FIELD_PREP(ASI_CH_SLOT, channel->slot));
		if (ret < 0) {
			return ret;
		}
	}

	ret = codec_write_reg(dev, IN_CH_EN_ADDR, in_ch_en);
	if (ret < 0) {
		return ret;
	}
	return codec_write_reg(dev, ASI_OUT_CH_EN_ADDR, asi_out_ch_en);
}

static int codec_set_input_volume(const struct device *dev, int vol)
{
	return -ENOTSUP;
	// uint8_t vol_val;
	// int vol_index;
	// uint8_t vol_array[] = {107, 108, 110, 113, 116, 120, 125, 128, 132, 138, 144};

	if ((vol > CODEC_OUTPUT_VOLUME_MAX) || (vol < CODEC_OUTPUT_VOLUME_MIN)) {
		LOG_ERR("Invalid volume %d.%d dB", vol >> 1, ((uint32_t)vol & 1) ? 5 : 0);
		return -EINVAL;
	}

	/* remove sign */
	vol = -vol;

	/* if volume is near floor, set minimum */
	// if (vol > HPX_ANA_VOL_FLOOR) {
	// 	vol_val = HPX_ANA_VOL_FLOOR;
	// } else if (vol > HPX_ANA_VOL_LOW_THRESH) {
	// 	/* lookup low volume values */
	// 	for (vol_index = 0; vol_index < ARRAY_SIZE(vol_array); vol_index++) {
	// 		if (vol_array[vol_index] >= vol) {
	// 			break;
	// 		}
	// 	}
	// 	vol_val = HPX_ANA_VOL_LOW_THRESH + vol_index + 1;
	// } else {
	// 	vol_val = (uint8_t)vol;
	// }

	// codec_write_reg(dev, HPL_ANA_VOL_CTRL_ADDR, HPX_ANA_VOL(vol_val));
	// codec_write_reg(dev, HPR_ANA_VOL_CTRL_ADDR, HPX_ANA_VOL(vol_val));
	return 0;
}

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
static void codec_read_all_regs(const struct device *dev)
{
	uint8_t val;

	codec_read_reg(dev, SW_RESET_ADDR, &val);
	codec_read_reg(dev, SLEEP_CFG_ADDR, &val);
	codec_read_reg(dev, SHDN_CFG_ADDR, &val);
	codec_read_reg(dev, ASI_CFG0_ADDR, &val);
	codec_read_reg(dev, ASI_CFG1_ADDR, &val);
	codec_read_reg(dev, ASI_CFG2_ADDR, &val);
	codec_read_reg(dev, ASI_MIX_CFG_ADDR, &val);
	codec_read_reg(dev, ASI_CH_ADDR(1), &val);
	codec_read_reg(dev, ASI_CH_ADDR(2), &val);
	codec_read_reg(dev, ASI_CH_ADDR(3), &val);
	codec_read_reg(dev, ASI_CH_ADDR(4), &val);
	codec_read_reg(dev, MST_CFG0_ADDR, &val);
	codec_read_reg(dev, MST_CFG1_ADDR, &val);
	codec_read_reg(dev, CH_CFG0_ADDR(1), &val);
	codec_read_reg(dev, CH_CFG0_ADDR(2), &val);
	codec_read_reg(dev, CH_CFG0_ADDR(3), &val);
	codec_read_reg(dev, CH_CFG0_ADDR(4), &val);
	codec_read_reg(dev, CH_CFG1_ADDR(1), &val);
	codec_read_reg(dev, CH_CFG1_ADDR(2), &val);
	codec_read_reg(dev, CH_CFG1_ADDR(3), &val);
	codec_read_reg(dev, CH_CFG1_ADDR(4), &val);
	codec_read_reg(dev, CH_CFG2_ADDR(1), &val);
	codec_read_reg(dev, CH_CFG2_ADDR(2), &val);
	codec_read_reg(dev, CH_CFG2_ADDR(3), &val);
	codec_read_reg(dev, CH_CFG2_ADDR(4), &val);
	codec_read_reg(dev, CH_CFG3_ADDR(1), &val);
	codec_read_reg(dev, CH_CFG3_ADDR(2), &val);
	codec_read_reg(dev, CH_CFG3_ADDR(3), &val);
	codec_read_reg(dev, CH_CFG3_ADDR(4), &val);
	codec_read_reg(dev, CH_CFG4_ADDR(1), &val);
	codec_read_reg(dev, CH_CFG4_ADDR(2), &val);
	codec_read_reg(dev, CH_CFG4_ADDR(3), &val);
	codec_read_reg(dev, CH_CFG4_ADDR(4), &val);
	codec_read_reg(dev, IN_CH_EN_ADDR, &val);
	codec_read_reg(dev, ASI_OUT_CH_EN_ADDR, &val);
	codec_read_reg(dev, PWR_CFG_ADDR, &val);
}
#endif

static const struct input_codec_api codec_driver_api = {
	.configure = codec_configure,
	.start_input = codec_start_input,
	.stop_input = codec_stop_input,
	.set_property = codec_set_property,
	.apply_properties = codec_apply_properties,
};

#define CHANNEL_CONFIG(id)                                                                         \
	{                                                                                          \
		.channel = DT_PROP(id, channel),                                                       \
		.line_in = DT_PROP(id, line_in),                                                   \
		.dc_coupled = DT_PROP(id, dc_coupled),                                             \
		.impedance_ohms = DT_PROP(id, impedance_ohms),                                     \
		.slot = DT_PROP(id, slot),                                                         \
	},

#define CREATE_CODEC(inst)                                                                         \
	static const struct codec_channel_config codec_channel_configs_##inst[] = {                \
		DT_INST_FOREACH_CHILD(inst, CHANNEL_CONFIG)};                                      \
                                                                                                   \
	static const struct codec_driver_config codec_device_config_##inst = {                     \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.avdd_supply = COND_CODE_1(                                                        \
			DT_INST_NODE_HAS_PROP(inst, avdd_supply),                                  \
			(DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), avdd_supply))), (NULL)),      \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                        \
		.internal_areg = DT_INST_PROP(inst, internal_areg),                                \
		.num_channels = ARRAY_SIZE(codec_channel_configs_##inst),                          \
		.channels = codec_channel_configs_##inst,                                          \
	};                                                                                         \
	static struct codec_driver_data codec_device_data_##inst;                                  \
	DEVICE_DT_INST_DEFINE(inst, codec_initialize, NULL, &codec_device_data_##inst,             \
			      &codec_device_config_##inst, POST_KERNEL,                            \
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &codec_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_CODEC)
