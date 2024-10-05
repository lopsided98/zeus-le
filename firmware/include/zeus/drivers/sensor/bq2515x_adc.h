/*
 * Copyright (c) 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_BQ2515X_ADC_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_BQ2515X_ADC_H_

#include <zephyr/drivers/sensor.h>

/* BQ2515x charger ADC specific channels */
enum sensor_channel_bq2515x_adc {
	/** VIN voltage, in volts **/
	SENSOR_CHAN_BQ2515X_ADC_VIN = SENSOR_CHAN_PRIV_START,
	/** PMID voltage, in volts **/
	SENSOR_CHAN_BQ2515X_ADC_PMID,
	/** Input supply current, in amps **/
	SENSOR_CHAN_BQ2515X_ADC_IIN,
	SENSOR_CHAN_BQ2515X_ADC_TS,
	/** ADCIN pin voltage, in volts **/
	SENSOR_CHAN_BQ2515X_ADC_ADCIN,
    /** Charge current, in percent of the maximum **/
	SENSOR_CHAN_BQ2515X_ADC_ICHG,
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_BQ2515X_ADC_H_ */
