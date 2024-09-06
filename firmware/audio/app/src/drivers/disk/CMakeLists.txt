# Copyright (c) 2021 Nordic Semiconductor ASA
# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_DISK_DRIVER_CACHE app PRIVATE disk_cache.c)