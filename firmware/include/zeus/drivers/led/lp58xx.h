/*
 * Copyright (c) 2024 Ben Wolsieffer
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <zephyr/device.h>

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_LP58XX_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_LP58XX_H_

#define LP58XX_AE_REPEAT_INFINITE  0xf
#define LP58XX_AEU_REPEAT_INFINITE 0x3

struct lp58xx_aeu_config {
	uint8_t pwm[5];
	uint16_t time_msec[4];
	uint8_t repeat: 2;
};

struct lp58xx_ae_config {
	uint16_t pause_start_msec;
	uint16_t pause_end_msec;
	uint8_t num_aeu: 2;
	uint8_t repeat: 4;
	struct lp58xx_aeu_config aeu[3];
};

int lp58xx_ae_configure(const struct device *dev, uint8_t channel,
			const struct lp58xx_ae_config *ae_cfg);

int lp58xx_get_auto_pwm(const struct device *dev, uint8_t start_channel, uint8_t num_channels,
			uint8_t *buf);

int lp58xx_start(const struct device *dev);

int lp58xx_stop(const struct device *dev);

int lp58xx_pause(const struct device *dev);

int lp58xx_continue(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_LED_LP58XX_H_ */
