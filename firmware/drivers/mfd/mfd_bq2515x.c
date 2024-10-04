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
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zeus/drivers/mfd/bq2515x.h>

LOG_MODULE_REGISTER(mfd_bq2515x, CONFIG_MFD_LOG_LEVEL);

struct mfd_bq2515x_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec lp_gpio;
	struct gpio_dt_spec int_gpio;
	uint8_t pg_mode;
};

struct mfd_bq2515x_data {
	struct k_mutex mutex;
	const struct device *dev;
	struct gpio_callback int_callback;
	struct k_work int_work;
	sys_slist_t callbacks;
	uint32_t int_mask;
	uint32_t pending_flags;
};

static int mfd_bq2515x_write_int_mask(const struct device *dev)
{
	const struct mfd_bq2515x_config *config = dev->config;
	struct mfd_bq2515x_data *data = dev->data;
	uint8_t buf[1 + sizeof(data->int_mask)] = {BQ2515X_MASK0_ADDR};
	memcpy(buf + 1, &data->int_mask, sizeof(data->int_mask));

	return i2c_write_dt(&config->i2c, buf, sizeof(buf));
}

static void bq2515x_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct mfd_bq2515x_data *data = CONTAINER_OF(cb, struct mfd_bq2515x_data, int_callback);

	k_work_submit(&data->int_work);
}

static void bq2515x_int_work_handler(struct k_work *work)
{
	struct mfd_bq2515x_data *data = CONTAINER_OF(work, struct mfd_bq2515x_data, int_work);
	uint32_t flags;
	int ret;

	/* Read (and clear) all flag registers */
	ret = mfd_bq2515x_reg_read_burst(data->dev, BQ2515X_FLAG0_ADDR, &flags, sizeof(flags));
	if (ret < 0) {
		k_work_submit(&data->int_work);
		return;
	}

	/* Treat 4 flag registers as a 32-bit LE integer */
	flags = sys_le32_to_cpu(flags);
	/*
	 * Add saved pending flags. This avoids different behavior depending on the
	 * order in which callbacks are added. Any pending interrupts are fired when
	 * a callback for them is added, but currently masked interrupts would be
	 * cleared and ignored, even if they are going to be unmasked as soon as the
	 * next callback is added. By saving the pending masked interrupts, we avoid
	 * this problem.
	 */
	flags |= data->pending_flags;
	/* Save flags that are currently masked */
	data->pending_flags = flags & data->int_mask;
	/* Filter only unmasked interrupts to fire right now */
	flags &= ~data->int_mask;

	if (flags != 0) {
		gpio_fire_callbacks(&data->callbacks, data->dev, flags);
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

	if (config->lp_gpio.port != NULL) {
		/* Configure low power mode GPIO */
		if (!gpio_is_ready_dt(&config->lp_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->lp_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}

		/* 1 ms to exit low power mode */
		k_msleep(1);
	}

	/* Check for valid device ID */
	ret = mfd_bq2515x_reg_read(dev, BQ2515X_DEVICE_ID_ADDR, &val);
	if (ret < 0) {
		LOG_ERR("Failed to read device ID (err %d)", ret);
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

	ret = mfd_bq2515x_write_int_mask(dev);
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

		/* TODO: this has to be a level interrupt to wake nRF53. How to avoid baking in
		 * platform specific assumptions? */
		ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_LEVEL_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	val = FIELD_PREP(BQ2515X_ICCTRL1_PG_MODE, config->pg_mode);
	ret = mfd_bq2515x_reg_write(dev, BQ2515X_ICCTRL1_ADDR, val);
	if (ret < 0) {
		return ret;
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
	int ret;

	data->int_mask &= ~callback->pin_mask;
	ret = mfd_bq2515x_write_int_mask(dev);
	if (ret < 0) {
		return ret;
	}
	/* Hardware doesn't trigger pending interrupts when they are unmasked,
	 * so check manually */
	k_work_submit(&data->int_work);

	return gpio_manage_callback(&data->callbacks, callback, true);
}

int mfd_bq2515x_remove_callback(const struct device *dev, struct gpio_callback *callback)
{
	struct mfd_bq2515x_data *data = dev->data;
	int ret;

	ret = gpio_manage_callback(&data->callbacks, callback, false);
	if (ret < 0) {
		return ret;
	}

	data->int_mask |= callback->pin_mask;
	return mfd_bq2515x_write_int_mask(dev);
}

#define MFD_BQ2515X_DEFINE(inst)                                                                   \
	static struct mfd_bq2515x_data data_##inst = {                                             \
		.int_mask = 0xffffffff,                                                            \
	};                                                                                         \
                                                                                                   \
	static const struct mfd_bq2515x_config config##inst = {                                    \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.lp_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, lp_gpios, {0}),                          \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                        \
		.pg_mode = DT_INST_ENUM_IDX_OR(inst, pg_mode, 0),                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, mfd_bq2515x_init, NULL, &data_##inst, &config##inst,           \
			      POST_KERNEL, CONFIG_MFD_BQ2515X_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_BQ2515X_DEFINE)
