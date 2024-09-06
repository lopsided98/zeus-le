# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_LP58XX lp58xx.c)