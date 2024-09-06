/*
 * Copyright (c) 2024 Ben Wolsieffer
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_RT6160_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_RT6160_H_

/**
 * @defgroup regulator_rt6160 RT6160 Devicetree helpers.
 * @ingroup regulator_interface
 * @{
 */

/**
 * @name RT6160 Regulator modes
 * @{
 */
#define RT6160_MODE_AUTO_PFM   0
#define RT6160_MODE_ULTRASONIC 1
#define RT6160_MODE_FORCED_PWM  2

/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_REGULATOR_RT6160_H_ */
