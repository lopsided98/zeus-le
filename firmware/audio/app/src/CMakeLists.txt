# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(drivers)

target_sources(app PRIVATE
    audio.c
    freq_ctlr.c
    freq_est.c
    ftp.c
    main.c
    net_audio.c
    sd_card.c
    sync_timer.c
    wav.c
)
target_sources_ifdef(CONFIG_USB_DEVICE_STACK_NEXT app PRIVATE
    usb.c
)