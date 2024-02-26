target_sources_ifdef(CONFIG_AUDIO_DUMMY_CODEC app PRIVATE
    dummy_codec.c
)
target_sources_ifdef(CONFIG_AUDIO_SGTL5000 app PRIVATE
    sgtl5000.c
)

if(${CONFIG_I2S_ALSA})
    find_package(ALSA REQUIRED)
    target_link_libraries(app PRIVATE ALSA::ALSA)
    target_sources(app PRIVATE i2s_alsa.c)
endif()