# SPDX-License-Identifier: GPL-3.0-or-later

target_sources(zeus_common PRIVATE
    led.c
    power.c
    shell.c
    wifi.c
)
target_sources_ifdef(CONFIG_USB_DEVICE_STACK_NEXT zeus_common PRIVATE usb.c)
target_link_libraries(zeus_common PRIVATE zeus_protocol)

zephyr_linker_sources(SECTIONS iterables.ld)