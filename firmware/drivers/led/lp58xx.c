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
#include <zephyr/sys/byteorder.h>
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

#define LP58XX_DEV_CONFIG_3_ADDR 0x004

#define LP58XX_DEV_CONFIG_5_ADDR 0x006

#define LP58XX_DEV_CONFIG_7_ADDR 0x008

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

#define LP58XX_CMD_START_ADDR 0x011
#define LP58XX_CMD_START_KEY  0xff

#define LP58XX_CMD_STOP_ADDR 0x012
#define LP58XX_CMD_STOP_KEY  0xaa

#define LP58XX_CMD_PAUSE_ADDR 0x013
#define LP58XX_CMD_PAUSE_KEY  0x33

#define LP58XX_CMD_CONTINUE_ADDR 0x014
#define LP58XX_CMD_CONTINUE_KEY  0xcc

#define LP58XX_LED_ENABLE_ADDR 0x020

#define LP58XX_RESET_ADDR 0x023
#define LP58XX_RESET_KEY  0x66

#define LP58XX_MANUAL_DC_ADDR(ch) (0x30 + (ch))

#define LP58XX_MANUAL_PWM_ADDR(ch) (0x40 + (ch))

#define LP58XX_AUTO_DC_ADDR(ch) (0x50 + (ch))

#define LP58XX_AE_SIZE     0x1a
#define LP58XX_AE_ADDR(ch) (0x080 + (ch) * LP58XX_AE_SIZE)

#define LP58XX_AE_PAUSE_OFFSET 0x00
#define LP58XX_AE_PAUSE_START  GENMASK(7, 4)
#define LP58XX_AE_PAUSE_END    GENMASK(3, 0)

#define LP58XX_AE_PLAYBACK_OFFSET  0x01
#define LP58XX_AE_PLAYBACK_AEU_NUM GENMASK(5, 4)
#define LP58XX_AE_PLAYBACK_REPEAT  GENMASK(3, 0)

#define LP58XX_AEU_SIZE          0x08
#define LP58XX_AEU_OFFSET(aeu)   (0x02 + (aeu) * LP58XX_AEU_SIZE)
#define LP58XX_AEU_ADDR(ch, aeu) (LP58XX_AE_ADDR(ch) + LP58XX_AEU_OFFSET(aeu))

#define LP58XX_AEU_PWM_OFFSET(pwm) (pwm)

#define LP58XX_AEU_T12_OFFSET 0x5
#define LP58XX_AEU_T12_T2     GENMASK(7, 4)
#define LP58XX_AEU_T12_T1     GENMASK(3, 0)

#define LP58XX_AEU_T34_OFFSET 0x6
#define LP58XX_AEU_T34_T4     GENMASK(7, 4)
#define LP58XX_AEU_T34_T3     GENMASK(3, 0)

#define LP58XX_AEU_PLAYBACK_OFFSET 0x07
#define LP58XX_AEU_PLAYBACK_REPEAT GENMASK(1, 0)

#define LP58XX_TSD_CONFIG_STATUS_ADDR 0x300
#define LP58XX_TSD_CONFIG_STATUS_ERR  BIT(0)

#define LP58XX_LOD_STATUS_0_ADDR 0x301

#define LP58XX_LSD_STATUS_0_ADDR 0x303

#define LP58XX_AUTO_PWM_ADDR(ch) (0x305 + (ch))

struct lp58xx_led_config {
	const uint16_t *manual_current_ua;
	const uint16_t *auto_current_ua;
	bool exponential_dimming;
	uint8_t phase_align;
};

struct lp58xx_config {
	struct i2c_dt_spec bus;
	uint8_t num_channels;
	uint8_t num_leds;
	const struct led_info *led_infos;
	const struct lp58xx_led_config *led_configs;
	uint32_t max_current_ua;
	uint8_t lsd_threshold_percent;
	size_t write_buf_len;
	uint8_t *write_buf;
};

static int lp58xx_reg_write(const struct device *dev, uint16_t reg, uint8_t val)
{
	const struct lp58xx_config *cfg = dev->config;

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = cfg->bus.addr | ((reg >> 8) & 0xff);

	return i2c_reg_write_byte(cfg->bus.bus, addr, reg & 0xff, val);
}

static int lp58xx_reg_read(const struct device *dev, uint16_t reg, uint8_t *val)
{
	const struct lp58xx_config *cfg = dev->config;

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = cfg->bus.addr | ((reg >> 8) & 0xff);

	return i2c_reg_read_byte(cfg->bus.bus, addr, reg & 0xff, val);
}

static int lp58xx_burst_write(const struct device *dev, uint16_t reg, const uint8_t *buf,
			      uint8_t len)
{
	const struct lp58xx_config *cfg = dev->config;

	if (len + 1 > cfg->write_buf_len) {
		return -EINVAL;
	}

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = cfg->bus.addr | ((reg >> 8) & 0xff);

	/*
	 * Unfortunately this controller doesn't support commands split into
	 * two I2C messages.
	 */
	cfg->write_buf[0] = reg & 0xff;
	memcpy(cfg->write_buf + 1, buf, len);
	return i2c_write(cfg->bus.bus, cfg->write_buf, len + 1, addr);
}

static int lp58xx_burst_read(const struct device *dev, uint16_t reg, uint8_t *buf, uint8_t len)
{
	const struct lp58xx_config *cfg = dev->config;

	if (len + 1 > cfg->write_buf_len) {
		return -EINVAL;
	}

	/* Two MSBs of register address are placed in device address */
	uint8_t addr = cfg->bus.addr | ((reg >> 8) & 0xff);
	uint8_t reg_lsb = reg & 0xff;

	return i2c_write_read(cfg->bus.bus, addr, &reg_lsb, sizeof(reg_lsb), buf, len);
}

static int lp58xx_write_channel_field(const struct device *dev, uint8_t reg, uint8_t channel,
				      uint8_t bits, uint8_t value)
{
	const struct lp58xx_config *cfg = dev->config;
	uint8_t bit;
	uint8_t shift;

	if (channel > cfg->num_channels) {
		return -EINVAL;
	}
	if (!(bits == 1 || bits == 2 || bits == 4 || bits == 8)) {
		return -EINVAL;
	}

	bit = channel * bits;
	reg += bit / 8;
	shift = bit % 8;

	return i2c_reg_update_byte_dt(&cfg->bus, reg, GENMASK(shift + bits - 1, shift),
				      value << shift);
}

static int lp58xx_current_from_microamps(const struct device *dev, uint16_t current_ua)
{
	const struct lp58xx_config *cfg = dev->config;

	if (current_ua > cfg->max_current_ua) {
		LOG_ERR("%s: current out of bounds: %u uA > %u uA", dev->name, current_ua,
			cfg->max_current_ua);
		return -EINVAL;
	}

	/* Round down so we don't exceed the configured current */
	return ((uint32_t)current_ua * 0xff) / cfg->max_current_ua;
}

static int lp58xx_set_manual_current(const struct device *dev, uint8_t channel, uint16_t current_ua)
{
	const struct lp58xx_config *cfg = dev->config;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	return lp58xx_reg_write(dev, LP58XX_MANUAL_DC_ADDR(channel),
				lp58xx_current_from_microamps(dev, current_ua));
}

static int lp58xx_set_auto_current(const struct device *dev, uint8_t channel, uint16_t current_ua)
{
	const struct lp58xx_config *cfg = dev->config;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	return lp58xx_reg_write(dev, LP58XX_AUTO_DC_ADDR(channel),
				lp58xx_current_from_microamps(dev, current_ua));
}

static int lp58xx_update_config(const struct device *dev)
{
	int ret;
	uint8_t val;

	/* Apply configuration */
	ret = lp58xx_reg_write(dev, LP58XX_CMD_UPDATE_ADDR, LP58XX_CMD_UPDATE_KEY);
	if (ret < 0) {
		return ret;
	}

	/* Check for configuration errors */
	ret = lp58xx_reg_read(dev, LP58XX_TSD_CONFIG_STATUS_ADDR, &val);
	if (ret < 0) {
		return ret;
	}

	if (val & LP58XX_TSD_CONFIG_STATUS_ERR) {
		LOG_ERR("%s: config error", dev->name);
		return -EINVAL;
	}

	return 0;
}

static const struct led_info *lp58xx_led_to_info(const struct lp58xx_config *cfg, uint32_t led)
{
	if (led < cfg->num_leds) {
		return &cfg->led_infos[led];
	}

	return NULL;
}

static const struct lp58xx_led_config *lp58xx_led_to_config(const struct lp58xx_config *cfg,
							    uint32_t led)
{
	if (led < cfg->num_leds) {
		return &cfg->led_configs[led];
	}

	return NULL;
}

static int lp58xx_get_info(const struct device *dev, uint32_t led, const struct led_info **info)
{
	const struct lp58xx_config *cfg = dev->config;
	const struct led_info *led_info = lp58xx_led_to_info(cfg, led);

	if (!led_info) {
		return -EINVAL;
	}

	*info = led_info;

	return 0;
}

static int lp58xx_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct lp58xx_config *cfg = dev->config;
	int ret;
	uint8_t val;

	if (led >= cfg->num_channels) {
		return -EINVAL;
	}

	if (value > LP58XX_MAX_BRIGHTNESS) {
		LOG_ERR("%s: brightness value out of bounds: val=%d, max=%d", dev->name, value,
			LP58XX_MAX_BRIGHTNESS);
		return -EINVAL;
	}

	uint8_t pwm = (value * 0xff) / LP58XX_MAX_BRIGHTNESS;

	ret = lp58xx_reg_write(dev, LP58XX_MANUAL_PWM_ADDR(led), pwm);
	if (ret < 0) {
		return ret;
	}

	ret = lp58xx_reg_read(dev, LP58XX_LOD_STATUS_0_ADDR, &val);
	if (ret < 0) {
		return ret;
	}
	if (val) {
		LOG_WRN("%s: LOD fault: 0x%02x", dev->name, val);
	}

	ret = lp58xx_reg_read(dev, LP58XX_LSD_STATUS_0_ADDR, &val);
	if (ret < 0) {
		return ret;
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

static int lp58xx_blink(const struct device *dev, uint32_t led, uint32_t delay_on,
			uint32_t delay_off)
{
	int ret;

	const struct lp58xx_ae_config ae_cfg = {
		.pause_start_msec = 0,
		.pause_end_msec = 0,
		.num_aeu = 1,
		.repeat = 1,
		.aeu = {{
			.pwm = {255, 255, 0, 0, 0},
			.time_msec = {delay_on, 0, delay_off, 0},
			.repeat = LP58XX_AEU_REPEAT_INFINITE,
		}},
	};

	ret = lp58xx_ae_configure(dev, led, &ae_cfg);
	if (ret < 0) {
		return ret;
	}

	return lp58xx_start(dev);
}

static int lp58xx_write_channels(const struct device *dev, uint32_t start_channel,
				 uint32_t num_channels, const uint8_t *buf)
{
	const struct lp58xx_config *cfg = dev->config;

	if (start_channel + num_channels > cfg->num_channels) {
		return -EINVAL;
	}

	return lp58xx_burst_write(dev, LP58XX_MANUAL_PWM_ADDR(start_channel), buf, num_channels);
}

static int lp58xx_set_color(const struct device *dev, uint32_t led, uint8_t num_colors,
			    const uint8_t *color)
{
	const struct lp58xx_config *config = dev->config;
	const struct led_info *led_info = lp58xx_led_to_info(config, led);

	if (!led_info) {
		return -ENODEV;
	}

	if (num_colors != led_info->num_colors) {
		LOG_ERR("%s: invalid number of colors: got=%d, expected=%d", dev->name, num_colors,
			led_info->num_colors);
		return -EINVAL;
	}

	return lp58xx_write_channels(dev, led_info->index, num_colors, color);
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

static int lp58xx_leds_configure(const struct device *dev)
{
	const struct lp58xx_config *cfg = dev->config;
	int ret;

	uint16_t led_en = 0;
	uint16_t exp_en = 0;
	for (uint8_t led = 0; led < cfg->num_leds; ++led) {
		const struct led_info *led_info = lp58xx_led_to_info(cfg, led);
		const struct lp58xx_led_config *led_config = lp58xx_led_to_config(cfg, led);

		for (uint8_t i = 0; i < led_info->num_colors; ++i) {
			uint8_t ch = led_info->index + i;
			led_en |= BIT(ch);
			ret = lp58xx_set_manual_current(dev, ch, led_config->manual_current_ua[i]);
			if (ret < 0) {
				return ret;
			}
			ret = lp58xx_set_auto_current(dev, ch, led_config->auto_current_ua[i]);
			if (ret < 0) {
				return ret;
			}
			if (led_config->exponential_dimming) {
				exp_en |= BIT(ch);
			}
		}
	}

	led_en = sys_cpu_to_le16(led_en);
	exp_en = sys_cpu_to_le16(exp_en);

	ret = lp58xx_burst_write(dev, LP58XX_DEV_CONFIG_5_ADDR, (uint8_t *)&exp_en, sizeof(exp_en));
	if (ret < 0) {
		return ret;
	}

	ret = lp58xx_burst_write(dev, LP58XX_LED_ENABLE_ADDR, (uint8_t *)&led_en, sizeof(led_en));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int lp58xx_init(const struct device *dev)
{
	const struct lp58xx_config *cfg = dev->config;
	uint8_t lsd_threshold;
	uint8_t val;
	int ret;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		LOG_ERR("%s: I2C device not ready", dev->name);
		return -ENODEV;
	}

	/* Reset device */
	ret = lp58xx_reset(dev);
	if (ret < 0) {
		LOG_ERR("%s: failed to reset", dev->name);
		return ret;
	}

	/* Enable device */
	ret = lp58xx_enable(dev, true);
	if (ret < 0) {
		LOG_ERR("%s: failed to enable", dev->name);
		return ret;
	}

	/* Setup config registers */
	val = 0;
	if (cfg->max_current_ua == 51000) {
		val |= LP58XX_DEV_CONFIG_0_MAX_CURRENT;
	}

	ret = lp58xx_reg_write(dev, LP58XX_DEV_CONFIG_0_ADDR, val);
	if (ret < 0) {
		return ret;
	}

	switch (cfg->lsd_threshold_percent) {
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

	ret = lp58xx_reg_write(dev, LP58XX_DEV_CONFIG_12_ADDR, val);
	if (ret < 0) {
		return ret;
	}

	ret = lp58xx_leds_configure(dev);
	if (ret < 0) {
		return ret;
	}

	/* Apply configuration */
	return lp58xx_update_config(dev);
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
	.blink = lp58xx_blink,
	.get_info = lp58xx_get_info,
	.set_brightness = lp58xx_set_brightness,
	.set_color = lp58xx_set_color,
	.write_channels = lp58xx_write_channels,
};

static uint8_t lp58xx_ae_time_from_msec(uint16_t msec)
{
	// No obvious equation to calculate register value, so use a lookup table to round to the
	// nearest supported value.
	if (msec < 45) {
		return 0x0; // 0 ms
	} else if (msec < 135) {
		return 0x1; // 90 ms
	} else if (msec < 270) {
		return 0x2; // 180 ms
	} else if (msec < 450) {
		return 0x3; // 360 ms
	} else if (msec < 670) {
		return 0x4; // 540 ms
	} else if (msec < 935) {
		return 0x5; // 800 ms
	} else if (msec < 1295) {
		return 0x6; // 1070 ms
	} else if (msec < 1790) {
		return 0x7; // 1520 ms
	} else if (msec < 2280) {
		return 0x8; // 2060 ms
	} else if (msec < 2770) {
		return 0x9; // 2500 ms
	} else if (msec < 3530) {
		return 0xa; // 3040 ms
	} else if (msec < 4515) {
		return 0xb; // 4020 ms
	} else if (msec < 5500) {
		return 0xc; // 5010 ms
	} else if (msec < 6525) {
		return 0xd; // 5990 ms
	} else if (msec < 7555) {
		return 0xe; // 7060 ms
	} else {
		return 0xf; // 8050 ms
	}
}

static int lp58xx_aeu_config_generate(const struct lp58xx_aeu_config *aeu_cfg, uint8_t buf[])
{
	ARRAY_FOR_EACH(aeu_cfg->pwm, i) {
		buf[LP58XX_AEU_PWM_OFFSET(i)] = aeu_cfg->pwm[i];
	}
	buf[LP58XX_AEU_T12_OFFSET] =
		FIELD_PREP(LP58XX_AEU_T12_T1, lp58xx_ae_time_from_msec(aeu_cfg->time_msec[0])) |
		FIELD_PREP(LP58XX_AEU_T12_T2, lp58xx_ae_time_from_msec(aeu_cfg->time_msec[1]));
	buf[LP58XX_AEU_T34_OFFSET] =
		FIELD_PREP(LP58XX_AEU_T34_T3, lp58xx_ae_time_from_msec(aeu_cfg->time_msec[2])) |
		FIELD_PREP(LP58XX_AEU_T34_T4, lp58xx_ae_time_from_msec(aeu_cfg->time_msec[3]));

	if (aeu_cfg->repeat > LP58XX_AEU_REPEAT_INFINITE) {
		return -EINVAL;
	}
	buf[LP58XX_AEU_PLAYBACK_OFFSET] = FIELD_PREP(LP58XX_AEU_PLAYBACK_REPEAT, aeu_cfg->repeat);
	return 0;
}

static int lp58xx_ae_config_generate(const struct lp58xx_ae_config *ae_cfg, uint8_t buf[])
{
	int ret;

	buf[LP58XX_AE_PAUSE_OFFSET] =
		FIELD_PREP(LP58XX_AE_PAUSE_START,
			   lp58xx_ae_time_from_msec(ae_cfg->pause_start_msec)) |
		FIELD_PREP(LP58XX_AE_PAUSE_END, lp58xx_ae_time_from_msec(ae_cfg->pause_end_msec));

	if (ae_cfg->num_aeu < 1 || ae_cfg->num_aeu > 3) {
		return -EINVAL;
	}
	if (ae_cfg->repeat > LP58XX_AE_REPEAT_INFINITE) {
		return -EINVAL;
	}
	buf[LP58XX_AE_PLAYBACK_OFFSET] =
		FIELD_PREP(LP58XX_AE_PLAYBACK_AEU_NUM, ae_cfg->num_aeu - 1) |
		FIELD_PREP(LP58XX_AE_PLAYBACK_REPEAT, ae_cfg->repeat);

	ARRAY_FOR_EACH(ae_cfg->aeu, i) {
		ret = lp58xx_aeu_config_generate(&ae_cfg->aeu[i], buf + LP58XX_AEU_OFFSET(i));
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int lp58xx_ae_configure(const struct device *dev, uint8_t channel,
			const struct lp58xx_ae_config *ae_cfg)
{
	const struct lp58xx_config *cfg = dev->config;
	uint8_t buf[LP58XX_AE_SIZE];
	int ret;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	ret = lp58xx_ae_config_generate(ae_cfg, buf);
	if (ret < 0) {
		return ret;
	}

	ret = lp58xx_burst_write(dev, LP58XX_AE_ADDR(channel), buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	/* Switch LED to autonomous mode */
	ret = lp58xx_write_channel_field(dev, LP58XX_DEV_CONFIG_3_ADDR, channel, 1, 1);
	if (ret < 0) {
		return ret;
	}

	/* Apply config */
	return lp58xx_update_config(dev);
}

int lp58xx_get_auto_pwm(const struct device *dev, uint8_t start_channel, uint8_t num_channels,
			uint8_t *buf)
{
	return lp58xx_burst_read(dev, LP58XX_AUTO_PWM_ADDR(start_channel), buf, num_channels);
}

int lp58xx_start(const struct device *dev)
{
	return lp58xx_reg_write(dev, LP58XX_CMD_START_ADDR, LP58XX_CMD_START_KEY);
}

int lp58xx_stop(const struct device *dev)
{
	return lp58xx_reg_write(dev, LP58XX_CMD_STOP_ADDR, LP58XX_CMD_STOP_KEY);
}

int lp58xx_pause(const struct device *dev)
{
	return lp58xx_reg_write(dev, LP58XX_CMD_PAUSE_ADDR, LP58XX_CMD_PAUSE_KEY);
}

int lp58xx_continue(const struct device *dev)
{
	return lp58xx_reg_write(dev, LP58XX_CMD_CONTINUE_ADDR, LP58XX_CMD_CONTINUE_KEY);
}

#define LED_CHANNELS(led_node_id)                                                                  \
	BUILD_ASSERT(DT_PROP_LEN(led_node_id, color_mapping) ==                                    \
			     DT_PROP_LEN(led_node_id, manual_current_microamp),                    \
		     "Wrong number of manual mode currents defined");                              \
	BUILD_ASSERT(DT_PROP_LEN(led_node_id, color_mapping) ==                                    \
			     DT_PROP_LEN(led_node_id, auto_current_microamp),                      \
		     "Wrong number of autonomous mode currents defined");                          \
	BUILD_ASSERT(DT_PROP_LEN(led_node_id, color_mapping) ==                                    \
			     DT_PROP_LEN(led_node_id, phase_align),                                \
		     "Wrong number of phase alignments defined");                                  \
	static const uint8_t color_mapping_##led_node_id[] = DT_PROP(led_node_id, color_mapping);  \
	static const uint16_t lp58xx_manual_current_ua_##led_node_id[] =                           \
		DT_PROP(led_node_id, manual_current_microamp);                                     \
	static const uint16_t lp58xx_auto_current_ua_##led_node_id[] =                             \
		DT_PROP(led_node_id, auto_current_microamp);
// TODO: configure phase alignment

#define LED_INFO(led_node_id)                                                                      \
	{                                                                                          \
		.label = DT_PROP(led_node_id, label),                                              \
		.index = DT_PROP(led_node_id, index),                                              \
		.num_colors = DT_PROP_LEN(led_node_id, color_mapping),                             \
		.color_mapping = color_mapping_##led_node_id,                                      \
	},

#define LED_CONFIG(led_node_id)                                                                    \
	{                                                                                          \
		.manual_current_ua = lp58xx_manual_current_ua_##led_node_id,                       \
		.auto_current_ua = lp58xx_auto_current_ua_##led_node_id,                           \
		.exponential_dimming = DT_PROP(led_node_id, exponential_dimming),                  \
	},

#define LP58XX_DEVICE(n, id)                                                                       \
	DT_INST_FOREACH_CHILD(n, LED_CHANNELS)                                                     \
                                                                                                   \
	static const struct led_info lp58xx_led_infos_##n[] = {                                    \
		DT_INST_FOREACH_CHILD(n, LED_INFO)};                                               \
	static const struct lp58xx_led_config lp58xx_led_configs_##n[] = {                         \
		DT_INST_FOREACH_CHILD(n, LED_CONFIG)};                                             \
	BUILD_ASSERT(ARRAY_SIZE(lp58xx_led_infos_##n) > 0, "No LEDs defined for " #id);            \
                                                                                                   \
	static uint8_t lp58xx_write_buf_##n[MAX(LP##id##_NUM_CHANNELS, LP58XX_AE_SIZE) + 1];       \
                                                                                                   \
	static const struct lp58xx_config lp58xx_config_##n = {                                    \
		.bus = I2C_DT_SPEC_INST_GET(n),                                                    \
		.num_channels = LP##id##_NUM_CHANNELS,                                             \
		.num_leds = ARRAY_SIZE(lp58xx_led_infos_##n),                                      \
		.led_infos = lp58xx_led_infos_##n,                                                 \
		.led_configs = lp58xx_led_configs_##n,                                             \
		.max_current_ua = DT_INST_PROP(n, max_current_microamp),                           \
		.lsd_threshold_percent = DT_INST_PROP(n, lsd_threshold_percent),                   \
		.write_buf_len = sizeof(lp58xx_write_buf_##n),                                     \
		.write_buf = lp58xx_write_buf_##n,                                                 \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(n, lp58xx_pm_action);                                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, lp58xx_init, PM_DEVICE_DT_INST_GET(n), NULL, &lp58xx_config_##n,  \
			      POST_KERNEL, CONFIG_LED_INIT_PRIORITY, &lp58xx_led_api);

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
