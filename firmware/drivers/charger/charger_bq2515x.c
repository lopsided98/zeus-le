/*
 * Copyright 2024 Google LLC
 * Copyright 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq2515x_charger

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zeus/drivers/mfd/bq2515x.h>

LOG_MODULE_REGISTER(bq2515x_charger, CONFIG_CHARGER_LOG_LEVEL);

/* Charging current limits */
#define BQ2515X_CHARGE_CURRENT_MIN_UA 1250
#define BQ2515X_CHARGE_CURRENT_MAX_UA 500000

/* Input current limits */
#define BQ2515X_INPUT_CURRENT_MIN_MA 50
#define BQ2515X_INPUT_CURRENT_MAX_MA 600

struct charger_bq2515x_data {
	struct gpio_callback event_cb;
	bool ce_gpio_active;
};

struct charger_bq2515x_config {
	const struct device *mfd;
	struct gpio_dt_spec ce_gpio;
	uint32_t initial_charge_current_ua;
	uint32_t initial_input_current_limit_ma;
};

/*
 * For ICHG <= 318.75mA, 1.25mA resolution
 * For ICHG > 318.75mA, 2.5mA resuolution
 * Maximum programmable current = 500mA
 *
 * Return: ICHG value between 0 and 127, ICHARGE_RANGE bit
 */
static uint8_t bq2515x_ua_to_ichg(uint32_t current_ua, bool *icharge_range)
{
	if (!IN_RANGE(current_ua, BQ2515X_CHARGE_CURRENT_MIN_UA, BQ2515X_CHARGE_CURRENT_MAX_UA)) {
		LOG_WRN("charging current out of range: %duA, "
			"clamping to the nearest limit",
			current_ua);
	}
	current_ua =
		CLAMP(current_ua, BQ2515X_CHARGE_CURRENT_MIN_UA, BQ2515X_CHARGE_CURRENT_MAX_UA);

	/* Round down to avoid exceeding requested limit */
	if (current_ua <= 318750) {
		*icharge_range = false;
		return current_ua / 1250;
	} else {
		*icharge_range = true;
		return current_ua / 2500;
	}
}

static uint32_t bq2515x_ichg_to_ua(uint8_t ichg, bool ichg_range)
{
	if (ichg_range) {
		return ichg * 1250;
	} else {
		return ichg * 2500;
	}
}

static uint8_t bq2515x_ma_to_ilim(uint32_t current_ma)
{
	if (!IN_RANGE(current_ma, BQ2515X_INPUT_CURRENT_MIN_MA, BQ2515X_INPUT_CURRENT_MAX_MA)) {
		LOG_WRN("input current out of range: %duA, "
			"clamping to the nearest limit",
			current_ma);
	}
	current_ma = CLAMP(current_ma, BQ2515X_INPUT_CURRENT_MIN_MA, BQ2515X_INPUT_CURRENT_MAX_MA);

	/* Round down to avoid exceeding requested limit */
	if (current_ma < 175) {
		return current_ma / 50;
	} else {
		return current_ma / 100 + 1;
	}
}

static uint32_t bq2515x_ilim_to_ma(uint8_t ilim)
{
	if (ilim <= 0x2) {
		return ilim * 50 + 50;
	} else {
		return ilim * 100 - 100;
	}
}

static int bq2515x_charge_enable(const struct device *dev, const bool enable)
{
	const struct charger_bq2515x_config *config = dev->config;
	struct charger_bq2515x_data *data = dev->data;
	int ret;

	if (config->ce_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&config->ce_gpio, enable);
		if (ret == 0) {
			data->ce_gpio_active = enable;
		}
	} else {
		uint8_t value = enable ? 0 : BQ2515X_ICCTRL2_CHARGER_DISABLE;

		ret = mfd_bq2515x_reg_update(config->mfd, BQ2515X_ICCTRL2_ADDR,
					     BQ2515X_ICCTRL2_CHARGER_DISABLE, value);
	}

	return ret;
}

static int bq2515x_set_charge_current(const struct device *dev, uint32_t const_charge_current_ua)
{
	const struct charger_bq2515x_config *config = dev->config;
	uint8_t pchrgctrl;
	uint8_t ichg;
	bool icharge_range;
	int ret;

	ichg = bq2515x_ua_to_ichg(const_charge_current_ua, &icharge_range);

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_PCHRGCTRL_ADDR, &pchrgctrl);
	if (ret < 0) {
		return ret;
	}

	if (icharge_range) {
		pchrgctrl |= BQ2515X_PCHRGCTRL_ICHARGE_RANGE;
	} else {
		pchrgctrl &= ~BQ2515X_PCHRGCTRL_ICHARGE_RANGE;
	}

	/* Write both registers in one transaction so we don't set the wrong current for a short
	 * period. */
	ret = mfd_bq2515x_reg_write2(config->mfd, BQ2515X_ICHG_CTRL_ADDR, ichg, pchrgctrl);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int bq2515x_set_input_current_limit(const struct device *dev,
					   uint32_t input_current_regulation_ua)
{
	const struct charger_bq2515x_config *config = dev->config;
	int ret;
	uint8_t ilim = bq2515x_ma_to_ilim(input_current_regulation_ua / 1000);

	ret = mfd_bq2515x_reg_write(config->mfd, BQ2515X_ILIMCTRL_ADDR,
				    FIELD_PREP(BQ2515X_ILIMCTRL_ILIM, ilim));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int bq2515x_get_online(const struct device *dev, enum charger_online *online)
{
	const struct charger_bq2515x_config *config = dev->config;
	uint8_t val;
	int ret;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_STAT0_ADDR, &val);
	if (ret < 0) {
		return ret;
	}

	if ((val & BQ2515X_STAT0_VIN_PGOOD_STAT) != 0x00) {
		*online = CHARGER_ONLINE_FIXED;
	} else {
		*online = CHARGER_ONLINE_OFFLINE;
	}

	return 0;
}

static int bq2515x_get_status(const struct device *dev, enum charger_status *status)
{
	const struct charger_bq2515x_config *config = dev->config;
	struct charger_bq2515x_data *data = dev->data;
	uint8_t stat0;
	uint8_t icctrl2;
	int ret;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_STAT0_ADDR, &stat0);
	if (ret < 0) {
		return ret;
	}

	if (!(stat0 & BQ2515X_STAT0_VIN_PGOOD_STAT)) {
		*status = CHARGER_STATUS_DISCHARGING;
		return 0;
	}

	if (!data->ce_gpio_active) {
		*status = CHARGER_STATUS_NOT_CHARGING;
		return 0;
	}

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_ICCTRL2_ADDR, &icctrl2);
	if (ret < 0) {
		return ret;
	}

	if (icctrl2 & BQ2515X_ICCTRL2_CHARGER_DISABLE) {
		*status = CHARGER_STATUS_NOT_CHARGING;
	} else if (stat0 & BQ2515X_STAT0_CHRG_CV_STAT) {
		*status = CHARGER_STATUS_CHARGING;
	} else if (stat0 & BQ2515X_STAT0_CHARGE_DONE_STAT) {
		*status = CHARGER_STATUS_FULL;
	} else {
		*status = CHARGER_STATUS_CHARGING;
	}

	return 0;
}

static int bq2515x_get_charge_current(const struct device *dev, uint32_t *const_charge_current_ua)
{
	const struct charger_bq2515x_config *config = dev->config;
	uint8_t rd_buf[2];
	int ret;

	ret = mfd_bq2515x_reg_read_burst(config->mfd, BQ2515X_ICHG_CTRL_ADDR, rd_buf,
					 sizeof(rd_buf));
	if (ret < 0) {
		return ret;
	}

	*const_charge_current_ua =
		bq2515x_ichg_to_ua(rd_buf[0], rd_buf[1] & BQ2515X_PCHRGCTRL_ICHARGE_RANGE);

	return 0;
}

static int bq2515x_get_input_current_limit(const struct device *dev,
					   uint32_t *input_current_regulation_ua)
{
	const struct charger_bq2515x_config *config = dev->config;
	uint8_t val;
	int ret;

	ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_ILIMCTRL_ADDR, &val);
	if (ret < 0) {
		return ret;
	}

	*input_current_regulation_ua =
		bq2515x_ilim_to_ma(FIELD_GET(val, BQ2515X_ILIMCTRL_ILIM)) * 1000;

	return 0;
}

static int bq2515x_get_prop(const struct device *dev, charger_prop_t prop,
			    union charger_propval *val)
{
	switch (prop) {
	case CHARGER_PROP_ONLINE:
		return bq2515x_get_online(dev, &val->online);
	case CHARGER_PROP_STATUS:
		return bq2515x_get_status(dev, &val->status);
	case CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA:
		return bq2515x_get_charge_current(dev, &val->const_charge_current_ua);
	case CHARGER_PROP_INPUT_REGULATION_CURRENT_UA:
		return bq2515x_get_input_current_limit(dev,
						       &val->input_current_regulation_current_ua);
	default:
		return -ENOTSUP;
	}
}

static int bq2515x_set_prop(const struct device *dev, charger_prop_t prop,
			    const union charger_propval *val)
{
	switch (prop) {
	case CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA:
		return bq2515x_set_charge_current(dev, val->const_charge_current_ua);
	case CHARGER_PROP_INPUT_REGULATION_CURRENT_UA:
		return bq2515x_set_input_current_limit(dev,
						       val->input_current_regulation_current_ua);
	default:
		return -ENOTSUP;
	}
}

static const struct charger_driver_api bq2515x_charger_api = {
	.get_property = bq2515x_get_prop,
	.set_property = bq2515x_set_prop,
	.charge_enable = bq2515x_charge_enable,
};

static void bq2515x_event_handler(const struct device *dev, struct gpio_callback *cb,
				  gpio_port_pins_t events)
{
	if (events & BIT(BQ2515X_EVENT_IINLIM_ACTIVE)) {
		LOG_WRN("IINLIM active");
	}
	if (events & BIT(BQ2515X_EVENT_VINDPM_ACTIVE)) {
		LOG_WRN("VIN DPM active");
	}
	if (events & BIT(BQ2515X_EVENT_VDPPM_ACTIVE)) {
		LOG_WRN("DPPM active");
	}
}

static int charger_bq2525x_init(const struct device *dev)
{
	const struct charger_bq2515x_config *config = dev->config;
	struct charger_bq2515x_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	gpio_init_callback(&data->event_cb, bq2515x_event_handler,
			   BIT(BQ2515X_EVENT_IINLIM_ACTIVE) | BIT(BQ2515X_EVENT_VINDPM_ACTIVE) |
				   BIT(BQ2515X_EVENT_VDPPM_ACTIVE));
	mfd_bq2515x_add_callback(config->mfd, &data->event_cb);

	/* Disable watchdog */
	ret = mfd_bq2515x_reg_update(config->mfd, BQ2515X_CHARGERCTRL0_ADDR,
				     BQ2515X_CHARGERCTRL0_WATCHDOG_DISABLE,
				     BQ2515X_CHARGERCTRL0_WATCHDOG_DISABLE);

	if (config->ce_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->ce_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->ce_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	if (config->initial_charge_current_ua > 0) {
		ret = bq2515x_set_charge_current(dev, config->initial_charge_current_ua);
		if (ret < 0) {
			return ret;
		}
	}

	if (config->initial_input_current_limit_ma > 0) {
		ret = bq2515x_set_input_current_limit(dev, config->initial_input_current_limit_ma *
								   1000);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

#define CHARGER_BQ2525X_INIT(inst)                                                                 \
	static struct charger_bq2515x_data data_##inst = {                                         \
		.ce_gpio_active = true,                                                            \
	};                                                                                         \
                                                                                                   \
	static const struct charger_bq2515x_config config_##inst = {                               \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.ce_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, ce_gpios, {}),                           \
		.initial_charge_current_ua =                                                       \
			DT_INST_PROP(inst, constant_charge_current_max_microamp),                  \
		.initial_input_current_limit_ma = DT_INST_PROP(inst, input_current_max_milliamp),  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, charger_bq2525x_init, NULL, &data_##inst, &config_##inst,      \
			      POST_KERNEL, CONFIG_CHARGER_INIT_PRIORITY, &bq2515x_charger_api);

DT_INST_FOREACH_STATUS_OKAY(CHARGER_BQ2525X_INIT)
