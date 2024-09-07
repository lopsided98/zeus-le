/*
 * Copyright (c) 2020 Seagate Technology LLC
 * Copyright (c) 2022 Grinn
 * Copyright (c) 2024 Ben Wolsiefer
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LP58xx LED controller
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>
#include <zeus/drivers/led/lp58xx.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lp58xx, CONFIG_LED_LOG_LEVEL);

#define LP5810_NUM_CHANNELS 4
#define LP5811_NUM_CHANNELS 4
#define LP5812_NUM_CHANNELS 12
#define LP5813_NUM_CHANNELS 12

#define LP58XX_MAX_BRIGHTNESS 100U

/* Base registers */
#define LP58XX_CHIP_EN_ADDR 0x000

#define LP58XX_DEV_CONFIG_0_ADDR        0x001
#define LP58XX_DEV_CONFIG_0_MAX_CURRENT BIT(0)

#define LP58XX_DEV_CONFIG_12_ADDR             0x00d
#define LP58XX_DEV_CONFIG_12_CLAMP_SEL        BIT(5)
#define LP58XX_DEV_CONFIG_12_CLAMP_DIS        BIT(4)
#define LP58XX_DEV_CONFIG_12_LOD_ACTION       BIT(3)
#define LP58XX_DEV_CONFIG_12_LSD_ACTION       BIT(2)
#define LP58XX_DEV_CONFIG_12_LSD_THRESHOLD    GENMASK(1, 0)
#define LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_35 0
#define LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_45 1
#define LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_55 2
#define LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_65 3

#define LP58XX_CMD_UPDATE_ADDR 0x010
#define LP58XX_CMD_UPDATE_KEY  0x55

#define LP58XX_LED_ENABLE_ADDR 0x020

#define LP58XX_RESET_ADDR 0x023
#define LP58XX_RESET_KEY  0x66

#define LP58XX_MANUAL_DC_ADDR(chan) (0x30 + (chan))

#define LP58XX_MANUAL_PWM_ADDR(chan) (0x40 + (chan))

#define LP58XX_TSD_CONFIG_STATUS_ADDR 0x300
#define LP58XX_TSD_CONFIG_STATUS_ERR  BIT(0)

#define LP58XX_LOD_STATUS_0_ADDR 0x301

#define LP58XX_LSD_STATUS_0_ADDR 0x303

struct lp58xx_config {
	struct i2c_dt_spec bus;
	uint8_t num_channels;
	uint8_t num_leds;
	uint8_t channel_offset;
	uint32_t max_current_ua;
	uint8_t lsd_threshold_percent;
};

struct lp58xx_data {
	uint8_t *chan_buf;
};

static int lp58xx_reg_write(const struct device *dev, uint16_t reg, uint8_t val)
{
	const struct lp58xx_config *config = dev->config;

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = config->bus.addr | ((reg >> 8) & 0xff);

	return i2c_reg_write_byte(config->bus.bus, addr, reg & 0xff, val);
}

static int lp58xx_reg_read(const struct device *dev, uint16_t reg, uint8_t *val)
{
	const struct lp58xx_config *config = dev->config;

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = config->bus.addr | ((reg >> 8) & 0xff);

	return i2c_reg_read_byte(config->bus.bus, addr, reg & 0xff, val);
}

static int lp58xx_burst_write(const struct device *dev, uint16_t reg, const uint8_t *buf,
			      uint8_t len)
{
	const struct lp58xx_config *config = dev->config;
	struct lp58xx_data *data = dev->data;

	if (len > config->num_channels) {
		return -EINVAL;
	}

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = config->bus.addr | ((reg >> 8) & 0xff);

	/*
	 * Unfortunately this controller doesn't support commands split into
	 * two I2C messages.
	 */
	data->chan_buf[0] = reg & 0xff;
	memcpy(data->chan_buf + 1, buf, len);
	return i2c_write(config->bus.bus, data->chan_buf, len + 1, addr);
}

static int lp58xx_write_channel_bit(const struct device *dev, uint8_t reg, uint8_t channel,
				    bool value)
{
	const struct lp58xx_config *config = dev->config;

	if (channel > config->num_channels) {
		return -EINVAL;
	}

	channel += config->channel_offset;
	uint8_t bit;
	if (channel > 7) {
		reg += 1;
		bit = channel - 8;
	} else {
		bit = channel;
	}

	return i2c_reg_update_byte_dt(&config->bus, reg, BIT(bit), ((uint8_t)value) << bit);
}

static int lp58xx_set_current(const struct device *dev, uint8_t led, uint16_t current_ua)
{
	const struct lp58xx_config *config = dev->config;

	if (led >= config->num_channels) {
		return -EINVAL;
	}

	if (current_ua > config->max_current_ua) {
		LOG_ERR("%s: current out of bounds: %u uA > %u uA", dev->name, current_ua,
			config->max_current_ua);
		return -EINVAL;
	}

	uint8_t val = ((uint32_t)current_ua * 0xff) / config->max_current_ua;

	return lp58xx_reg_write(dev, LP58XX_MANUAL_DC_ADDR(config->channel_offset + led), val);
}

static int lp58xx_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct lp58xx_config *config = dev->config;
	int err;
	uint8_t val;

	if (led >= config->num_channels) {
		return -EINVAL;
	}

	if (value > LP58XX_MAX_BRIGHTNESS) {
		LOG_ERR("%s: brightness value out of bounds: val=%d, max=%d", dev->name, value,
			LP58XX_MAX_BRIGHTNESS);
		return -EINVAL;
	}

	err = lp58xx_write_channel_bit(dev, LP58XX_LED_ENABLE_ADDR, led, value != 0);
	if (err < 0) {
		return err;
	}

	uint8_t pwm = (value * 0xff) / LP58XX_MAX_BRIGHTNESS;

	err = lp58xx_reg_write(dev, LP58XX_MANUAL_PWM_ADDR(config->channel_offset + led), pwm);
	if (err < 0) {
		return err;
	}

	err = lp58xx_reg_read(dev, LP58XX_LOD_STATUS_0_ADDR, &val);
	if (err < 0) {
		return err;
	}
	if (val) {
		LOG_WRN("%s: LOD fault: 0x%02x", dev->name, val);
	}

	err = lp58xx_reg_read(dev, LP58XX_LSD_STATUS_0_ADDR, &val);
	if (err < 0) {
		return err;
	}
	if (val) {
		LOG_WRN("%s: LSD fault: 0x%02x", dev->name, val);
	}

	return 0;
}

static int lp58xx_on(const struct device *dev, uint32_t led)
{
	return lp58xx_set_brightness(dev, led, 100);
}

static int lp58xx_off(const struct device *dev, uint32_t led)
{

	return lp58xx_set_brightness(dev, led, 0);
}

static int lp58xx_write_channels(const struct device *dev, uint32_t start_channel,
				 uint32_t num_channels, const uint8_t *buf)
{
	const struct lp58xx_config *config = dev->config;

	if (start_channel + num_channels > config->num_channels) {
		return -EINVAL;
	}

	return lp58xx_burst_write(dev,
				  LP58XX_MANUAL_PWM_ADDR(config->channel_offset + start_channel),
				  buf, num_channels);
}

static int lp58xx_reset(const struct device *dev)
{
	uint8_t val;
	int tries = 5;
	int ret;
	/* Always NAKs, so ignore return value */
	lp58xx_reg_write(dev, LP58XX_RESET_ADDR, LP58XX_RESET_KEY);

	/* Next transaction sometimes fails, so try a few reads until the chip is
	 * responding again. */
	do {
		ret = lp58xx_reg_read(dev, LP58XX_RESET_ADDR, &val);
		tries--;
	} while (ret < 0 && tries > 0);
	return ret;
}

static int lp58xx_enable(const struct device *dev, bool enable)
{
	return lp58xx_reg_write(dev, LP58XX_CHIP_EN_ADDR, enable);
}

static int lp58xx_init(const struct device *dev)
{
	const struct lp58xx_config *config = dev->config;
	uint8_t lsd_threshold;
	uint8_t val;
	int err;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("%s: I2C device not ready", dev->name);
		return -ENODEV;
	}

	/* Reset device */
	err = lp58xx_reset(dev);
	if (err < 0) {
		LOG_ERR("%s: failed to reset", dev->name);
		return err;
	}

	/* Enable device */
	err = lp58xx_enable(dev, true);
	if (err < 0) {
		LOG_ERR("%s: failed to enable", dev->name);
		return err;
	}

	/* Setup config registers */
	val = 0;
	if (config->max_current_ua == 51000) {
		val |= LP58XX_DEV_CONFIG_0_MAX_CURRENT;
	}

	err = lp58xx_reg_write(dev, LP58XX_DEV_CONFIG_0_ADDR, val);
	if (err < 0) {
		return err;
	}

	switch (config->lsd_threshold_percent) {
	case 35:
		lsd_threshold = LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_35;
		break;
	case 45:
		lsd_threshold = LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_45;
		break;
	case 55:
		lsd_threshold = LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_55;
		break;
	case 65:
		lsd_threshold = LP58XX_DEV_CONFIG_12_LSD_THRESHOLD_65;
		break;
	default:
		return -EINVAL;
	}
	val = LP58XX_DEV_CONFIG_12_LOD_ACTION |
	      FIELD_PREP(LP58XX_DEV_CONFIG_12_LSD_THRESHOLD, lsd_threshold);

	err = lp58xx_reg_write(dev, LP58XX_DEV_CONFIG_12_ADDR, val);
	if (err < 0) {
		return err;
	}

	/* Apply configuration */
	err = lp58xx_reg_write(dev, LP58XX_CMD_UPDATE_ADDR, LP58XX_CMD_UPDATE_KEY);
	if (err < 0) {
		return err;
	}

	/* Check for configuration errors */
	err = lp58xx_reg_read(dev, LP58XX_TSD_CONFIG_STATUS_ADDR, &val);
	if (err < 0) {
		return err;
	}

	if (val & LP58XX_TSD_CONFIG_STATUS_ERR) {
		LOG_ERR("%s: config error", dev->name);
		return -EINVAL;
	}

	err = lp58xx_set_current(dev, 0, 6000);
	if (err < 0) {
		return err;
	}
	err = lp58xx_set_current(dev, 1, 7000);
	if (err < 0) {
		return err;
	}
	err = lp58xx_set_current(dev, 2, 9800);
	if (err < 0) {
		return err;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int lp58xx_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		return lp58xx_enable(dev, false);
	case PM_DEVICE_ACTION_RESUME:
		return lp58xx_enable(dev, true);
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

static const struct led_driver_api lp58xx_led_api = {
	.on = lp58xx_on,
	.off = lp58xx_off,
	.set_brightness = lp58xx_set_brightness,
	.write_channels = lp58xx_write_channels,
};

#define LP58XX_DEVICE(n, id)                                                                       \
	static const struct lp58xx_config lp##id##_config_##n = {                                  \
		.bus = I2C_DT_SPEC_INST_GET(n),                                                    \
		.num_channels = LP##id##_NUM_CHANNELS,                                             \
		.channel_offset = 0,                                                               \
		.max_current_ua = DT_INST_PROP(n, max_current_microamps),                          \
		.lsd_threshold_percent = DT_INST_PROP(n, lsd_threshold_percent),                   \
	};                                                                                         \
                                                                                                   \
	static uint8_t lp##id##_chan_buf_##n[LP##id##_NUM_CHANNELS + 1];                           \
                                                                                                   \
	static struct lp58xx_data lp##id##_data_##n = {                                            \
		.chan_buf = lp##id##_chan_buf_##n,                                                 \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(n, lp58xx_pm_action);                                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, lp58xx_init, PM_DEVICE_DT_INST_GET(n), &lp##id##_data_##n,        \
			      &lp##id##_config_##n, POST_KERNEL, CONFIG_LED_INIT_PRIORITY,         \
			      &lp58xx_led_api);

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_lp5810
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP58XX_DEVICE, 5810)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_lp5811
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP58XX_DEVICE, 5811)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_lp5812
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP58XX_DEVICE, 5812)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_lp5813
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP58XX_DEVICE, 5813)
