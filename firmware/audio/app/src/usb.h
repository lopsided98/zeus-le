// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
int usb_init(void);
#else
static inline int usb_init(void) { return 0; }
#endif