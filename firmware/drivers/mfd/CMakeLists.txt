# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_MFD_BQ2515X mfd_bq2515x.c)