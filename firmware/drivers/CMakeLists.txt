# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_CHARGER charger)
add_subdirectory_ifdef(CONFIG_LED led)
add_subdirectory_ifdef(CONFIG_MFD mfd)
add_subdirectory_ifdef(CONFIG_REGULATOR regulator)