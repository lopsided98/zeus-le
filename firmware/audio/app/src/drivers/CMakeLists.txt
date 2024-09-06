# Copyright (c) 2024 Ben Wolsieffer
# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory_ifdef(CONFIG_DISK_DRIVERS disk)

target_sources_ifdef(CONFIG_AUDIO_DUMMY_CODEC app PRIVATE
    dummy_codec.c
)
target_sources_ifdef(CONFIG_AUDIO_SGTL5000 app PRIVATE
    sgtl5000.c
)
target_sources_ifdef(CONFIG_AUDIO_TLV320ADCX120 app PRIVATE
    tlv320adcx120.c
)

if(${CONFIG_I2S_ALSA})
    find_package(ALSA REQUIRED)
    target_link_libraries(app PRIVATE ALSA::ALSA)
    target_sources(app PRIVATE i2s_alsa.c)
endif()

target_sources_ifdef(CONFIG_I2S_TONE app PRIVATE
    i2s_tone.c
)
