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
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/linear_range.h>

#include "input_codec.h"

LOG_MODULE_REGISTER(tlv320adcx120, CONFIG_AUDIO_CODEC_LOG_LEVEL);

/*
 * Codec must not be woken up until at least 10 ms after suspend. This is also used as the
 * pm_device_runtime_put_async() delay to avoid constantly suspending the codec and then having to
 * wait the min suspend delay before resuming.
 */
#define CODEC_MIN_SUSPEND_MSEC 10

#define CODEC_NUM_CHANNELS        4
#define CODEC_NUM_ANALOG_CHANNELS 2

#define CODEC_DEFAULT_DVOL 201

static const struct linear_range analog_gain_range = LINEAR_RANGE_INIT(0, 1, 0, 84);
static const struct linear_range digital_gain_range = LINEAR_RANGE_INIT(-100 * 2, 1, 1, 255);

struct codec_channel_config {
	uint8_t channel;
	bool line_in;
	bool dc_coupled;
	uint16_t impedance_ohms;
	uint8_t slot;
};

struct codec_channel_data {
	bool mute;
	uint8_t dvol;
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
	uint8_t reg_page_cache;
	struct codec_channel_data channels[CODEC_NUM_CHANNELS];
	/* Time the codec last entered suspend; used to enforce minimum suspend time. */
	int64_t suspend_time_msec;
	bool started;
};

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
static void codec_read_all_regs(const struct device *dev);
#define CODEC_DUMP_REGS(dev) codec_read_all_regs((dev))
#else
#define CODEC_DUMP_REGS(dev)
#endif

static int codec_channel_to_index(audio_channel_t channel, uint8_t *channel_num)
{
	switch (channel) {
	case AUDIO_CHANNEL_FRONT_LEFT:
		*channel_num = 1;
		break;
	case AUDIO_CHANNEL_FRONT_RIGHT:
		*channel_num = 2;
		break;
	default:
		/* TODO: decide how to map the other channels */
		return -ENOTSUP;
	}
	return 0;
}

static struct codec_channel_data *codec_get_channel_data(const struct device *dev, uint8_t channel)
{
	struct codec_driver_data *data = dev->data;

	if (channel < 1 || channel > CODEC_NUM_CHANNELS) {
		return NULL;
	}

	return &data->channels[channel - 1];
}

static int codec_write_reg_no_pm(const struct device *dev, struct reg_addr reg, uint8_t val)
{
	struct codec_driver_data *const dev_data = dev->data;
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	/* set page if different */
	if (dev_data->reg_page_cache != reg.page) {
		ret = i2c_reg_write_byte_dt(&dev_cfg->bus, PAGE_CFG_ADDR, reg.page);
		if (ret < 0) {
			return ret;
		}
		dev_data->reg_page_cache = reg.page;
	}

	ret = i2c_reg_write_byte_dt(&dev_cfg->bus, reg.reg_addr, val);
	if (ret < 0) {
		return ret;
	}
	LOG_DBG("WR PG:%u REG:%02u VAL:0x%02x", reg.page, reg.reg_addr, val);
	return 0;
}

static int codec_write_reg(const struct device *dev, struct reg_addr reg, uint8_t val)
{
	int ret;

	ret = pm_device_runtime_get(dev);
	if (ret) {
		return ret;
	}

	ret = codec_write_reg_no_pm(dev, reg, val);

	pm_device_runtime_put_async(dev, K_MSEC(CODEC_MIN_SUSPEND_MSEC));

	return ret;
}

static int codec_read_reg(const struct device *dev, struct reg_addr reg, uint8_t *val)
{
	struct codec_driver_data *const dev_data = dev->data;
	const struct codec_driver_config *const dev_cfg = dev->config;
	int ret;

	ret = pm_device_runtime_get(dev);
	if (ret) {
		return ret;
	}

	/* set page if different */
	if (dev_data->reg_page_cache != reg.page) {
		ret = i2c_reg_write_byte_dt(&dev_cfg->bus, PAGE_CFG_ADDR, reg.page);
		if (ret < 0) {
			goto pm_put;
		}
		dev_data->reg_page_cache = reg.page;
	}

	i2c_reg_read_byte_dt(&dev_cfg->bus, reg.reg_addr, val);
	if (ret < 0) {
		goto pm_put;
	}
	LOG_DBG("RD PG:%u REG:%02u VAL:0x%02x", reg.page, reg.reg_addr, *val);

	ret = 0;

pm_put:
	pm_device_runtime_put_async(dev, K_MSEC(CODEC_MIN_SUSPEND_MSEC));
	return ret;
}

static int codec_soft_reset(const struct device *dev)
{
	struct codec_driver_data *const data = dev->data;
	int ret;
	/* Soft reset the ADC */
	ret = codec_write_reg(dev, SW_RESET_ADDR, SW_RESET_ASSERT);
	if (ret) {
		return ret;
	}

	/* Reset cached page address and property values */
	data->reg_page_cache = 0;
	for (size_t i = 0; i < ARRAY_SIZE(data->channels); ++i) {
		struct codec_channel_data *channel = &data->channels[i];
		channel->dvol = CODEC_DEFAULT_DVOL;
		channel->mute = false;
	}
	return 0;
}

static int codec_configure_power(const struct device *dev, bool sleep)
{
	const struct codec_driver_config *const cfg = dev->config;

	uint8_t val = FIELD_PREP(SLEEP_CFG_VREF_QCHG, SLEEP_CFG_VREF_QCHG_3_5_MS);
	if (cfg->internal_areg) {
		val |= SLEEP_CFG_AREG_SELECT;
	}
	if (!sleep) {
		val |= SLEEP_CFG_SLEEP_ENZ;
	}

	/* No PM get/put because this is used to implement PM */
	return codec_write_reg_no_pm(dev, SLEEP_CFG_ADDR, val);
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

	if (cfg->num_channels > CODEC_NUM_CHANNELS) {
		LOG_ERR("Too many (%u) channels configured", cfg->num_channels);
		return -EINVAL;
	}

	for (uint8_t i = 0; i < cfg->num_channels; ++i) {
		const struct codec_channel_config *channel = &cfg->channels[i];
		uint8_t val = 0;

		if (channel->channel < 1 || channel->channel > CODEC_NUM_CHANNELS) {
			LOG_ERR("Channel out of range: %u", channel->channel);
			return -EINVAL;
		}

		if (channel->channel <= CODEC_NUM_ANALOG_CHANNELS) {
			/* Only channels 1 and 2 have analog input */

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

			ret = codec_write_reg(dev, CH_CFG0_ADDR(channel->channel), val);
			if (ret < 0) {
				return ret;
			}
		}

		in_ch_en |= IN_CH_EN(channel->channel);
		asi_out_ch_en |= ASI_OUT_CH_EN(channel->channel);

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

static int codec_get_analog_gain(const struct device *dev, uint8_t channel, int32_t *gain)
{
	int ret;
	uint8_t val;

	/* Only first two channels have analog gain */
	if (channel < 1 || channel > CODEC_NUM_ANALOG_CHANNELS) {
		return -ENOTSUP;
	}

	ret = codec_read_reg(dev, CH_CFG1_ADDR(channel), &val);
	if (ret) {
		return ret;
	}

	return linear_range_get_value(&analog_gain_range, FIELD_GET(CH_CFG1_GAIN, val), gain);
}

static int codec_set_analog_gain(const struct device *dev, uint8_t channel, int32_t gain)
{
	int ret;
	uint16_t idx;

	/* Only first two channels have analog gain */
	if (channel < 1 || channel > CODEC_NUM_ANALOG_CHANNELS) {
		return -ENOTSUP;
	}

	ret = linear_range_get_index(&analog_gain_range, gain, &idx);
	if (ret) {
		return ret;
	}

	return codec_write_reg(dev, CH_CFG1_ADDR(channel), FIELD_PREP(CH_CFG1_GAIN, idx));
}

static int codec_get_digital_gain(const struct device *dev, uint8_t channel, int32_t *gain)
{
	struct codec_channel_data *channel_data = codec_get_channel_data(dev, channel);

	return linear_range_get_value(&digital_gain_range, channel_data->dvol, gain);
}

static int codec_set_digital_gain(const struct device *dev, uint8_t channel, int32_t gain)
{
	int ret;
	uint16_t dvol;
	struct codec_channel_data *channel_data = codec_get_channel_data(dev, channel);
	if (!channel_data) {
		return -EINVAL;
	}

	ret = linear_range_get_index(&digital_gain_range, gain, &dvol);
	if (ret) {
		return ret;
	}

	if (dvol == channel_data->dvol) {
		return 0;
	}

	if (!channel_data->mute) {
		ret = codec_write_reg(dev, CH_CFG2_ADDR(channel), FIELD_PREP(CH_CFG2_DVOL, dvol));
		if (ret) {
			return ret;
		}
	}

	channel_data->dvol = dvol;
	return 0;
}

static int codec_get_mute(const struct device *dev, uint8_t channel, bool *mute)
{
	struct codec_channel_data *channel_data = codec_get_channel_data(dev, channel);

	*mute = channel_data->mute;
	return 0;
}

static int codec_set_mute(const struct device *dev, uint8_t channel, bool mute)
{
	int ret;
	uint8_t dvol;
	struct codec_channel_data *channel_data = codec_get_channel_data(dev, channel);
	if (!channel_data) {
		return -EINVAL;
	}

	if (mute == channel_data->mute) {
		return 0;
	}

	if (mute) {
		dvol = 0;
	} else {
		dvol = channel_data->dvol;
	}

	ret = codec_write_reg(dev, CH_CFG2_ADDR(channel), FIELD_PREP(CH_CFG2_DVOL, dvol));
	if (ret) {
		return ret;
	}

	channel_data->mute = mute;
	return 0;
}

static int codec_initialize(const struct device *dev)
{
	const struct codec_driver_config *const config = dev->config;
	int ret;

	if (!device_is_ready(config->bus.bus)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_REGULATOR)
	if (config->avdd_supply != NULL) {
		if (!device_is_ready(config->avdd_supply)) {
			LOG_ERR("AVDD regulator not ready");
			return -ENODEV;
		}

		ret = regulator_enable(config->avdd_supply);
		if (ret) {
			return ret;
		}
	}
#endif

	ret = codec_soft_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = codec_configure_power(dev, false);
	if (ret < 0) {
		return ret;
	}

	ret = codec_configure_input(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->int_gpio.port) {
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			LOG_ERR("GPIO device not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (ret < 0) {
			return ret;
		}
	}

	return pm_device_runtime_enable(dev);
}

static int codec_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("dai_type must be AUDIO_DAI_TYPE_I2S");
		return -EINVAL;
	}

	return codec_configure_dai(dev, &cfg->dai_cfg);
}

static int codec_start_input(const struct device *dev)
{
	struct codec_driver_data *data = dev->data;
	int ret;

	if (data->started) {
		return -EALREADY;
	}

	ret = pm_device_runtime_get(dev);
	if (ret) {
		return ret;
	}

	/* Power on ADC */
	ret = codec_write_reg(dev, PWR_CFG_ADDR, PWR_CFG_ADC_PDZ | PWR_CFG_PLL_PDZ);
	if (ret) {
		goto error_i2c;
	}

	data->started = true;

	CODEC_DUMP_REGS(dev);
	return 0;

error_i2c:
	pm_device_runtime_put_async(dev, K_MSEC(CODEC_MIN_SUSPEND_MSEC));

	return ret;
}

static int codec_stop_input(const struct device *dev)
{
	struct codec_driver_data *data = dev->data;
	int ret;

	if (!data->started) {
		return -EALREADY;
	}

	/* Power off ADC */
	ret = codec_write_reg(dev, PWR_CFG_ADDR, 0);
	if (ret) {
		return ret;
	}

	ret = pm_device_runtime_put_async(dev, K_MSEC(CODEC_MIN_SUSPEND_MSEC));
	if (ret) {
		return ret;
	}

	data->started = false;

	return 0;
}

static int codec_get_property(const struct device *dev, enum input_codec_property property,
			      audio_channel_t channel, union input_codec_property_value *val)
{
	uint8_t channel_num;
	int ret;

	ret = codec_channel_to_index(channel, &channel_num);
	if (ret) {
		return ret;
	}

	switch (property) {
	case INPUT_CODEC_PROPERTY_ANALOG_GAIN:
		return codec_get_analog_gain(dev, channel_num, &val->gain);

	case INPUT_CODEC_PROPERTY_DIGITAL_GAIN:
		return codec_get_digital_gain(dev, channel_num, &val->gain);

	case INPUT_CODEC_PROPERTY_MUTE:
		return codec_get_mute(dev, channel_num, &val->mute);

	default:
		return -ENOTSUP;
	}
}

static int codec_set_property(const struct device *dev, enum input_codec_property property,
			      audio_channel_t channel, union input_codec_property_value val)
{
	uint8_t channel_num;
	int ret;

	ret = codec_channel_to_index(channel, &channel_num);
	if (ret) {
		return ret;
	}

	switch (property) {
	case INPUT_CODEC_PROPERTY_ANALOG_GAIN:
		return codec_set_analog_gain(dev, channel_num, val.gain);

	case INPUT_CODEC_PROPERTY_DIGITAL_GAIN:
		return codec_set_digital_gain(dev, channel_num, val.gain);

	case INPUT_CODEC_PROPERTY_MUTE:
		return codec_set_mute(dev, channel_num, val.mute);

	default:
		return -ENOTSUP;
	}
}

static int codec_apply_properties(const struct device *dev)
{
	/* nothing to do because there is nothing cached */
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int codec_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct codec_driver_data *data = dev->data;
	int ret;
	bool sleep;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		sleep = true;
		break;
	case PM_DEVICE_ACTION_RESUME:
		sleep = false;
		break;
	default:
		return -ENOTSUP;
	}

	if (!sleep) {
		/* Enforce minimum suspend time */
		int64_t suspend_duration_msec = k_uptime_get() - data->suspend_time_msec;
		if (suspend_duration_msec < CODEC_MIN_SUSPEND_MSEC) {
			k_sleep(K_MSEC(CODEC_MIN_SUSPEND_MSEC - suspend_duration_msec));
		}
	}

	ret = codec_configure_power(dev, sleep);
	if (ret) {
		return ret;
	}

	if (sleep) {
		data->suspend_time_msec = k_uptime_get();
	} else {
		/* Wait 1 ms for internal wakeup sequence to complete */
		k_sleep(K_MSEC(1));
	}

	return 0;
}
#endif

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
	for (uint8_t ch = 1; ch <= CODEC_NUM_CHANNELS; ++ch) {
		if (ch <= CODEC_NUM_ANALOG_CHANNELS) {
			codec_read_reg(dev, CH_CFG0_ADDR(ch), &val);
			codec_read_reg(dev, CH_CFG1_ADDR(ch), &val);
		}
		codec_read_reg(dev, CH_CFG2_ADDR(ch), &val);
		codec_read_reg(dev, CH_CFG3_ADDR(ch), &val);
		codec_read_reg(dev, CH_CFG4_ADDR(ch), &val);
	}
	codec_read_reg(dev, IN_CH_EN_ADDR, &val);
	codec_read_reg(dev, ASI_OUT_CH_EN_ADDR, &val);
	codec_read_reg(dev, PWR_CFG_ADDR, &val);
}
#endif

static const struct input_codec_api codec_driver_api = {
	.configure = codec_configure,
	.start_input = codec_start_input,
	.stop_input = codec_stop_input,
	.get_property = codec_get_property,
	.set_property = codec_set_property,
	.apply_properties = codec_apply_properties,
};

#define CHANNEL_CONFIG(id)                                                                         \
	{                                                                                          \
		.channel = DT_PROP(id, channel),                                                   \
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
	PM_DEVICE_DT_INST_DEFINE(inst, codec_pm_action);                                           \
	DEVICE_DT_INST_DEFINE(inst, codec_initialize, PM_DEVICE_DT_INST_GET(inst),                 \
			      &codec_device_data_##inst, &codec_device_config_##inst, POST_KERNEL, \
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &codec_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_CODEC)
