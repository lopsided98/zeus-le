# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(drivers)

target_sources(app PRIVATE
    audio.c
    freq_ctlr.c
    freq_est.c
    ftp.c
    main.c
    mgr.cpp
    net_audio.c
    power.c
    record.c
    sd_card.c
    shell.c
    sync_timer.c
    wav.c
)
