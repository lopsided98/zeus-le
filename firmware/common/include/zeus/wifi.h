// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#if DT_NODE_EXISTS(DT_NODELABEL(nrf70))
int wifi_power_off(void);

// Run right after the GPIO driver is loaded
// Can't use expressions in SYS_INIT(), so assert the expected hardcoded
// priority
#define WIFI_POWER_OFF_REGISTER()                  \
    BUILD_ASSERT(CONFIG_GPIO_INIT_PRIORITY == 40,  \
                 "GPIO init priority must be 40"); \
    SYS_INIT(wifi_power_off, PRE_KERNEL_1, 41)
#else
#define WIFI_POWER_OFF_REGISTER()
#endif
