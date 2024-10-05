// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <zephyr/sys/iterable_sections.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*power_shutdown_hook)(void);

#define POWER_SHUTDOWN_HOOK_DEFINE(hook, prio)              \
    const TYPE_SECTION_ITERABLE(power_shutdown_hook,        \
                                power_shutdown_hook_##prio, \
                                zeus_power_shutdown_hooks, prio) = hook;

int power_init(void);

#ifdef __cplusplus
}
#endif