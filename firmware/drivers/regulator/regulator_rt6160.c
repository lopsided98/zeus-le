/*
 * Copyright 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT richtek_rt6160

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/linear_range.h>
#include <zephyr/sys/util.h>
#include <zeus/dt-bindings/regulator/rt6160.h>

LOG_MODULE_REGISTER(rt6160, CONFIG_REGULATOR_LOG_LEVEL);

#define RT6160_CONTROL_ADDR                   0x01
#define RT6160_CONTROL_I2C_SDA_SLEW           GENMASK(6, 5)
#define RT6160_CONTROL_I2C_SDA_SLEW_HIGH      0
#define RT6160_CONTROL_I2C_SDA_SLEW_MEDIUM    1
#define RT6160_CONTROL_I2C_SDA_SLEW_LOW       2
#define RT6160_CONTROL_I2C_SDA_SLEW_VERY_LOW  3
#define RT6160_CONTROL_ULTRASONIC_MODE        BIT(4)
#define RT6160_CONTROL_FORCED_PWM             BIT(3)
#define RT6160_CONTROL_RAMP_PWM               BIT(2)
#define RT6160_CONTROL_DVS_SLEW_RATE          GENMASK(1, 0)
#define RT6160_CONTROL_DVS_SLEW_RATE_1_V_MS   0
#define RT6160_CONTROL_DVS_SLEW_RATE_2_5_V_MS 1
#define RT6160_CONTROL_DVS_SLEW_RATE_5_V_MS   2
#define RT6160_CONTROL_DVS_SLEW_RATE_10_V_MS  3

#define RT6160_STATUS_ADDR 0x02
#define RT6160_STATUS_HD   BIT(4)
#define RT6160_STATUS_UV   BIT(3)
#define RT6160_STATUS_OC   BIT(2)
#define RT6160_STATUS_TSD  BIT(1)
#define RT6160_STATUS_NPG  BIT(0)

#define RT6160_DEVID_ADDR                 0x03
#define RT6160_DEVID_MANUFACTURER         GENMASK(7, 4)
#define RT6160_DEVID_MANUFACTURER_RICHTEK 0xa
#define RT6160_DEVID_MAJOR                GENMASK(3, 2)
#define RT6160_DEVID_MINOR                GENMASK(1, 0)

#define RT6160_VOUT1_ADDR 0x04

#define RT6160_VOUT2_ADDR 0x05

static const struct linear_range vout_range = LINEAR_RANGE_INIT(2025000u, 25000u, 0, 127);

struct regulator_rt6160_data {
	struct regulator_common_data common;
};

struct regulator_rt6160_config {
	struct regulator_common_config common;
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec en_gpio;
	struct gpio_dt_spec vsel_gpio;
	bool ramp_pwm;
};

static int regulator_rt6160_set_enable(const struct device *dev, bool enable)
{
	const struct regulator_rt6160_config *config = dev->config;

	if (config->en_gpio.port != NULL) {
		return gpio_pin_set_dt(&config->en_gpio, enable);
	} else {
		return -ENOTSUP;
	}
}

static int regulator_rt6160_enable(const struct device *dev)
{
	return regulator_rt6160_set_enable(dev, true);
}

static int regulator_rt6160_disable(const struct device *dev)
{
	return regulator_rt6160_set_enable(dev, false);
}

static int regulator_rt6160_set_mode(const struct device *dev, regulator_mode_t mode)
{
	const struct regulator_rt6160_config *config = dev->config;
	uint8_t control = 0;

	if (config->ramp_pwm) {
		control |= RT6160_CONTROL_RAMP_PWM;
	}

	switch (mode) {
	case RT6160_MODE_AUTO_PFM:
		break;
	case RT6160_MODE_ULTRASONIC:
		control |= RT6160_CONTROL_ULTRASONIC_MODE;
		break;
	case RT6160_MODE_FORCED_PWM:
		control |= RT6160_CONTROL_FORCED_PWM;
		break;
	default:
		return -ENOTSUP;
	}

	return i2c_reg_write_byte_dt(&config->i2c, RT6160_CONTROL_ADDR, control);
}

static int regulator_rt6160_get_mode(const struct device *dev, regulator_mode_t *mode)
{
	const struct regulator_rt6160_config *config = dev->config;
	uint8_t control;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->i2c, RT6160_CONTROL_ADDR, &control);
	if (ret < 0) {
		return ret;
	}

	if (control & RT6160_CONTROL_FORCED_PWM) {
		*mode = RT6160_MODE_FORCED_PWM;
	} else if (control & RT6160_CONTROL_ULTRASONIC_MODE) {
		*mode = RT6160_MODE_ULTRASONIC;
	} else {
		*mode = RT6160_MODE_AUTO_PFM;
	}

	return 0;
}

static inline unsigned int regulator_rt6160_count_voltages(const struct device *dev)
{
	return linear_range_values_count(&vout_range);
}

static int regulator_rt6160_list_voltage(const struct device *dev, unsigned int idx,
					 int32_t *volt_uv)
{
	return linear_range_get_value(&vout_range, idx, volt_uv);
}

static int regulator_rt6160_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv)
{
	const struct regulator_rt6160_config *config = dev->config;
	uint16_t idx;
	int ret;

	ret = linear_range_get_win_index(&vout_range, min_uv, max_uv, &idx);
	if (ret < 0) {
		return ret;
	}

	return i2c_reg_write_byte_dt(&config->i2c, RT6160_VOUT1_ADDR, idx);
}

static int regulator_rt6160_get_voltage(const struct device *dev, int32_t *volt_uv)
{
	const struct regulator_rt6160_config *config = dev->config;
	uint8_t vout1;
	int ret;

	ret = i2c_reg_read_byte_dt(&config->i2c, RT6160_VOUT1_ADDR, &vout1);
	if (ret < 0) {
		return ret;
	}

	return linear_range_get_value(&vout_range, vout1, volt_uv);
}

static int regulator_rt6160_get_error_flags(const struct device *dev,
					    regulator_error_flags_t *flags)
{
	const struct regulator_rt6160_config *config = dev->config;
	uint8_t status;
	int ret;

	*flags = 0;

	ret = i2c_reg_read_byte_dt(&config->i2c, RT6160_STATUS_ADDR, &status);
	if (ret < 0) {
		return ret;
	}

	if (status & RT6160_STATUS_OC) {
		*flags |= REGULATOR_ERROR_OVER_CURRENT;
	}
	if (status & RT6160_STATUS_TSD) {
		*flags |= REGULATOR_ERROR_OVER_TEMP;
	}
	return 0;
}

static const struct regulator_driver_api api = {
	.enable = regulator_rt6160_enable,
	.disable = regulator_rt6160_disable,
	.set_mode = regulator_rt6160_set_mode,
	.get_mode = regulator_rt6160_get_mode,
	.set_voltage = regulator_rt6160_set_voltage,
	.get_voltage = regulator_rt6160_get_voltage,
	.list_voltage = regulator_rt6160_list_voltage,
	.count_voltages = regulator_rt6160_count_voltages,
	.get_error_flags = regulator_rt6160_get_error_flags,
};

static int regulator_rt6160_init(const struct device *dev)
{
	const struct regulator_rt6160_config *config = dev->config;
	bool enabled;
	uint8_t val;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, RT6160_DEVID_ADDR, &val);
	if (ret < 0) {
		LOG_ERR("No device found (err %d)", ret);
		return ret;
	}

	if (FIELD_GET(RT6160_DEVID_MANUFACTURER, val) != RT6160_DEVID_MANUFACTURER_RICHTEK) {
		LOG_ERR("Invalid device ID found: 0x%x!\n", val);
		return -ENOTSUP;
	}

	LOG_DBG("Found RT6160 rev %c%lu", (char)('A' + FIELD_GET(RT6160_DEVID_MAJOR, val)),
		FIELD_GET(RT6160_DEVID_MINOR, val));

	if (config->en_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->en_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->en_gpio,
					    config->common.flags & REGULATOR_INIT_ENABLED
						    ? GPIO_OUTPUT_ACTIVE
						    : GPIO_OUTPUT);
		if (ret < 0) {
			return ret;
		}

		enabled = gpio_pin_get_dt(&config->en_gpio);
	} else {
		/* No EN GPIO configured, so assume EN pin is hardwired high. */
		enabled = true;
	}

	if (config->vsel_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->vsel_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->vsel_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	regulator_common_data_init(dev);

	return regulator_common_init(dev, enabled);
}

#define REGULATOR_RT6160_INIT(inst)                                                                \
	static struct regulator_rt6160_data data_##inst;                                           \
                                                                                                   \
	static const struct regulator_rt6160_config config_##inst = {                              \
		.common = REGULATOR_DT_INST_COMMON_CONFIG_INIT(inst),                              \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.en_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, richtek_en_gpios, {}),                   \
		.vsel_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, richtek_vsel_gpios, {}),               \
		.ramp_pwm = DT_INST_PROP(inst, richtek_ramp_pwm),                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, regulator_rt6160_init, NULL, &data_##inst, &config_##inst,     \
			      POST_KERNEL, CONFIG_REGULATOR_RT6160_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_RT6160_INIT)
