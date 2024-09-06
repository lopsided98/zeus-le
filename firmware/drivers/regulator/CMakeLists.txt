# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_REGULATOR_BQ2515X regulator_bq2515x.c)
zephyr_library_sources_ifdef(CONFIG_REGULATOR_RT6160 regulator_rt6160.c)