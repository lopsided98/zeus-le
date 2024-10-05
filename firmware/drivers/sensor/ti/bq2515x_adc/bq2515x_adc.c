/*
 * Copyright (c) 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq2515x_adc

#include <zephyr/drivers/sensor.h>
#include <zeus/drivers/mfd/bq2515x.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zeus/drivers/sensor/bq2515x_adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bq2515x_adc);

struct bq2515x_adc_config {
	const struct device *mfd;
	struct k_sem *adc_ready_sem;
	uint8_t read_rate;
	uint8_t conversion_speed_ms;
};

struct bq2515x_adc_results {
	uint16_t vbat;
	uint16_t ts;
	uint16_t ichg;
	uint16_t adcin;
	uint16_t vin;
	uint16_t pmid;
	uint16_t iin;
} __packed;

struct bq2515x_adc_data {
	const struct device *dev;
	struct gpio_callback adc_ready_cb;
	uint8_t adcctrl0;
	struct bq2515x_adc_results results;
};

static void bq2515x_adc_ready_handler(const struct device *port, struct gpio_callback *cb,
				      gpio_port_pins_t pins)
{
	struct bq2515x_adc_data *data = CONTAINER_OF(cb, struct bq2515x_adc_data, adc_ready_cb);
	const struct bq2515x_adc_config *config = data->dev->config;

	k_sem_give(config->adc_ready_sem);
}

static void bq2515x_adc_convert(uint16_t reg, uint32_t num, uint32_t den, struct sensor_value *valp)
{
	uint32_t scaled = (uint32_t)sys_be16_to_cpu(reg) * num;
	valp->val1 = scaled / (UINT16_MAX * den);
	valp->val2 = (uint64_t)(scaled % (UINT16_MAX * den)) * 1000000 / (UINT16_MAX * den);
}

static int bq2515x_adc_channel_get(const struct device *dev, enum sensor_channel chan,
				   struct sensor_value *valp)
{
	struct bq2515x_adc_data *data = dev->data;

	switch ((int)chan) {
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		bq2515x_adc_convert(data->results.vbat, 6, 1, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_VIN:
		bq2515x_adc_convert(data->results.vin, 6, 1, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_PMID:
		bq2515x_adc_convert(data->results.pmid, 6, 1, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_IIN:
		bq2515x_adc_convert(data->results.iin, 375, 1000, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_TS:
		bq2515x_adc_convert(data->results.ts, 12, 10, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_ADCIN:
		bq2515x_adc_convert(data->results.adcin, 12, 10, valp);
		break;
	case SENSOR_CHAN_BQ2515X_ADC_ICHG:
		bq2515x_adc_convert(data->results.ichg, 125, 1, valp);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int bq2515x_adc_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct bq2515x_adc_config *config = dev->config;
	struct bq2515x_adc_data *data = dev->data;
	int ret;

	if (config->read_rate == BQ2515X_ADCCTRL0_ADC_READ_RATE_MANUAL) {
		uint8_t stat0;
		ret = mfd_bq2515x_reg_read(config->mfd, BQ2515X_STAT0_ADDR, &stat0);
		if (ret) {
			return ret;
		}
		/*
		 * If VIN is connected, the ADC always runs in continuous mode,
		 * so there is no point in requesting a manual conversion.
		 */
		if (!(stat0 & BQ2515X_STAT0_VIN_PGOOD_STAT)) {
			k_sem_reset(config->adc_ready_sem);

			ret = mfd_bq2515x_reg_write(config->mfd, BQ2515X_ADCCTRL0_ADDR,
						    data->adcctrl0 |
							    BQ2515X_ADCCTRL0_ADC_CONV_START);
			if (ret) {
				return ret;
			}
			/* Max conversion time for all channels should be 225 ms */
			ret = k_sem_take(config->adc_ready_sem, K_MSEC(300));
			if (ret == -ETIMEDOUT) {
				LOG_WRN("timed out waiting for ADC ready");
			}
		}
	}

	return mfd_bq2515x_reg_read_burst(config->mfd, BQ2515X_ADC_DATA_VBAT_M_ADDR, &data->results,
					  sizeof(data->results));
}

static int bq2515x_adc_init(const struct device *dev)
{
	const struct bq2515x_adc_config *config = dev->config;
	struct bq2515x_adc_data *data = dev->data;
	uint8_t conv_speed;
	uint8_t val;
	int ret;

	if (!device_is_ready(config->mfd)) {
		return -ENODEV;
	}

	data->dev = dev;

	if (config->read_rate == BQ2515X_ADCCTRL0_ADC_READ_RATE_MANUAL) {
		gpio_init_callback(&data->adc_ready_cb, bq2515x_adc_ready_handler,
				   BIT(BQ2515X_EVENT_ADC_READY));
		ret = mfd_bq2515x_add_callback(config->mfd, &data->adc_ready_cb);
		if (ret) {
			return ret;
		}
	}

	switch (config->conversion_speed_ms) {
	case 24:
		conv_speed = BQ2515X_ADCCTRL0_ADC_CONV_SPEED_24_MS;
		break;
	case 12:
		conv_speed = BQ2515X_ADCCTRL0_ADC_CONV_SPEED_12_MS;
		break;
	case 6:
		conv_speed = BQ2515X_ADCCTRL0_ADC_CONV_SPEED_6_MS;
		break;
	case 3:
		conv_speed = BQ2515X_ADCCTRL0_ADC_CONV_SPEED_3_MS;
		break;
	default:
		return -EINVAL;
	}

	data->adcctrl0 = FIELD_PREP(BQ2515X_ADCCTRL0_ADC_READ_RATE, config->read_rate) |
			 FIELD_PREP(BQ2515X_ADCCTRL0_ADC_CONV_SPEED, conv_speed);
	ret = mfd_bq2515x_reg_write(config->mfd, BQ2515X_ADCCTRL0_ADDR, data->adcctrl0);
	if (ret) {
		return ret;
	}

	val = BQ2515X_ADC_READ_EN_IIN | BQ2515X_ADC_READ_EN_PMID | BQ2515X_ADC_READ_EN_ICHG |
	      BQ2515X_ADC_READ_EN_VIN | BQ2515X_ADC_READ_EN_VBAT | BQ2515X_ADC_READ_EN_TS |
	      BQ2515X_ADC_READ_EN_ADCIN;
	ret = mfd_bq2515x_reg_write(config->mfd, BQ2515X_ADC_READ_EN_ADDR, val);
	if (ret) {
		return ret;
	}

	return 0;
}

static const struct sensor_driver_api bq2515x_adc_battery_driver_api = {
	.sample_fetch = bq2515x_adc_sample_fetch,
	.channel_get = bq2515x_adc_channel_get,
};

#define BQ2515X_ADC_INIT(n)                                                                        \
	static K_SEM_DEFINE(bq2515x_adc_ready_sem_##n, 0, 1);                                      \
                                                                                                   \
	static struct bq2515x_adc_data bq2515x_adc_data_##n;                                       \
                                                                                                   \
	static const struct bq2515x_adc_config bq2515x_adc_config_##n = {                          \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(n)),                                           \
		.adc_ready_sem = &bq2515x_adc_ready_sem_##n,                                       \
		.read_rate = DT_INST_ENUM_IDX(n, read_rate),                                       \
		.conversion_speed_ms = DT_INST_PROP(n, conversion_speed_ms),                       \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(                                                              \
		n, &bq2515x_adc_init, NULL, &bq2515x_adc_data_##n, &bq2515x_adc_config_##n,        \
		POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &bq2515x_adc_battery_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BQ2515X_ADC_INIT)
