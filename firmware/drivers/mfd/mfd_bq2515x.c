/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 *
 * BQ25150 Datasheet: https://www.ti.com/lit/gpn/bq25150
 * BQ25155 Datasheet: https://www.ti.com/lit/gpn/bq25155
 * BQ25157 Datasheet: https://www.ti.com/lit/gpn/bq25157
 */

#define DT_DRV_COMPAT ti_bq2515x

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zeus/drivers/mfd/bq2515x.h>

LOG_MODULE_REGISTER(mfd_bq2515x, CONFIG_MFD_LOG_LEVEL);

struct mfd_bq2515x_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
};

struct mfd_bq2515x_data {
	struct k_mutex mutex;
	const struct device *dev;
	struct gpio_callback int_callback;
	struct k_work int_work;
	sys_slist_t callbacks;
};

struct event_reg {
	uint8_t offset;
	uint8_t mask;
};

static const struct event_reg event_reg[BQ2515X_EVENT_MAX] = {
	[BQ2515X_EVENT_CHARGE_CV] = {0, BIT(6)},
	[BQ2515X_EVENT_CHARGE_DONE] = {0, BIT(5)},
	[BQ2515X_EVENT_IINLIM_ACTIVE] = {0, BIT(4)},
	[BQ2515X_EVENT_VDPPM_ACTIVE] = {0, BIT(3)},
	[BQ2515X_EVENT_VINDPM_ACTIVE] = {0, BIT(2)},
	[BQ2515X_EVENT_THERMREG_ACTIVE] = {0, BIT(1)},
	[BQ2515X_EVENT_VIN_PGOOD] = {0, BIT(0)},
	[BQ2515X_EVENT_VIN_OVP_FAULT] = {1, BIT(7)},
	[BQ2515X_EVENT_BAT_OCP_FAULT] = {1, BIT(5)},
	[BQ2515X_EVENT_BAT_UVLO_FAULT] = {1, BIT(4)},
	[BQ2515X_EVENT_TS_COLD] = {1, BIT(3)},
	[BQ2515X_EVENT_TS_COOL] = {1, BIT(2)},
	[BQ2515X_EVENT_TS_WARM] = {1, BIT(1)},
	[BQ2515X_EVENT_TS_HOT] = {1, BIT(0)},
	[BQ2515X_EVENT_ADC_READY] = {2, BIT(7)},
	[BQ2515X_EVENT_COMP1_ALARM] = {2, BIT(6)},
	[BQ2515X_EVENT_COMP2_ALARM] = {2, BIT(5)},
	[BQ2515X_EVENT_COMP3_ALARM] = {2, BIT(4)},
	[BQ2515X_EVENT_TS_OPEN] = {2, BIT(0)},
	[BQ2515X_EVENT_WD_FAULT] = {3, BIT(6)},
	[BQ2515X_EVENT_SAFETY_TIMER_FAULT] = {3, BIT(5)},
	[BQ2515X_EVENT_LDO_OCP_FAULT] = {3, BIT(4)},
	[BQ2515X_EVENT_MRWAKE1_TIMEOUT] = {3, BIT(2)},
	[BQ2515X_EVENT_MRWAKE2_TIMEOUT] = {3, BIT(1)},
	[BQ2515X_EVENT_MRRESET_WARN] = {3, BIT(0)},
};

static void bq2515x_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct mfd_bq2515x_data *data = CONTAINER_OF(cb, struct mfd_bq2515x_data, int_callback);

	k_work_submit(&data->int_work);
}

static void bq2515x_int_work_handler(struct k_work *work)
{
	struct mfd_bq2515x_data *data = CONTAINER_OF(work, struct mfd_bq2515x_data, int_work);
	uint8_t buf[4];
	int ret;

	/* Read (and clear) all flag registers */
	ret = mfd_bq2515x_reg_read_burst(data->dev, BQ2515X_FLAG0_ADDR, buf, sizeof(buf));
	if (ret < 0) {
		k_work_submit(&data->int_work);
		return;
	}

	uint32_t pin_mask = 0;
	for (int i = 0; i < BQ2515X_EVENT_MAX; i++) {
		struct event_reg event = event_reg[i];
		if (buf[event.offset] & event.mask) {
			pin_mask |= BIT(i);
		}
	}

	if (pin_mask != 0) {
		gpio_fire_callbacks(&data->callbacks, data->dev, pin_mask);
	} else {
		LOG_WRN("Spurious interrupt");
	}
}

static int mfd_bq2515x_init(const struct device *dev)
{
	const struct mfd_bq2515x_config *config = dev->config;
	struct mfd_bq2515x_data *data = dev->data;
	uint8_t val;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		return -ENODEV;
	}

	k_mutex_init(&data->mutex);

	data->dev = dev;

	/* Check for valid device ID */
	ret = mfd_bq2515x_reg_read(dev, BQ2515X_DEVICE_ID_ADDR, &val);
	if (ret < 0) {
		return ret;
	}

	switch (val) {
	case BQ25150_DEVICE_ID:
	case BQ25155_DEVICE_ID:
	case BQ25157_DEVICE_ID:
		break;
	default:
		LOG_ERR("Invalid device id: 0x%02x", val);
		return -ENODEV;
	}

	ret = mfd_bq2515x_software_reset(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->int_gpio.port != NULL) {
		/* Configure interrupt GPIO */
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (ret < 0) {
			return ret;
		}

		k_work_init(&data->int_work, bq2515x_int_work_handler);

		gpio_init_callback(&data->int_callback, bq2515x_int_handler,
				   BIT(config->int_gpio.pin));

		ret = gpio_add_callback(config->int_gpio.port, &data->int_callback);
		if (ret < 0) {
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

int mfd_bq2515x_reg_read_burst(const struct device *dev, uint8_t reg_addr, void *data, size_t len)
{
	const struct mfd_bq2515x_config *config = dev->config;
	uint8_t buff[] = {reg_addr};

	return i2c_write_read_dt(&config->i2c, buff, sizeof(buff), data, len);
}

int mfd_bq2515x_reg_read(const struct device *dev, uint8_t reg_addr, uint8_t *value)
{
	return mfd_bq2515x_reg_read_burst(dev, reg_addr, value, 1U);
}

int mfd_bq2515x_reg_write(const struct device *dev, uint8_t reg_addr, uint8_t value)
{
	const struct mfd_bq2515x_config *config = dev->config;
	struct mfd_bq2515x_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->mutex, K_FOREVER);

	ret = i2c_reg_write_byte_dt(&config->i2c, reg_addr, value);

	k_mutex_unlock(&data->mutex);
	return ret;
}

int mfd_bq2515x_reg_write2(const struct device *dev, uint8_t reg_addr, uint8_t value1,
			   uint8_t value2)
{
	const struct mfd_bq2515x_config *config = dev->config;
	struct mfd_bq2515x_data *data = dev->data;
	uint8_t buff[] = {reg_addr, value1, value2};
	int ret;

	k_mutex_lock(&data->mutex, K_FOREVER);

	ret = i2c_write_dt(&config->i2c, buff, sizeof(buff));

	k_mutex_unlock(&data->mutex);
	return ret;
}

int mfd_bq2515x_reg_update(const struct device *dev, uint8_t reg_addr, uint8_t mask, uint8_t value)
{
	const struct mfd_bq2515x_config *config = dev->config;
	struct mfd_bq2515x_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->mutex, K_FOREVER);

	ret = i2c_reg_update_byte_dt(&config->i2c, reg_addr, mask, value);

	k_mutex_unlock(&data->mutex);
	return ret;
}

int mfd_bq2515x_software_reset(const struct device *dev)
{
	return mfd_bq2515x_reg_update(dev, BQ2515X_ICCTRL0_ADDR, BQ2515X_ICCTRL0_SW_RESET,
				      BQ2515X_ICCTRL0_SW_RESET);
}

int mfd_bq2515x_add_callback(const struct device *dev, struct gpio_callback *callback)
{
	struct mfd_bq2515x_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, true);
}

int mfd_bq2515x_remove_callback(const struct device *dev, struct gpio_callback *callback)
{
	struct mfd_bq2515x_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, false);
}

#define MFD_BQ2515X_DEFINE(inst)                                                                   \
	static struct mfd_bq2515x_data data_##inst;                                                \
                                                                                                   \
	static const struct mfd_bq2515x_config config##inst = {                                    \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                        \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, mfd_bq2515x_init, NULL, &data_##inst, &config##inst,           \
			      POST_KERNEL, CONFIG_MFD_BQ2515X_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_BQ2515X_DEFINE)