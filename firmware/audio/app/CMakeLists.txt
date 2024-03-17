# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

# app.overlay is not used automatically if a board-specific overlay is present
list(APPEND EXTRA_DTC_OVERLAY_FILE app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(("${BOARD}" STREQUAL "raytac_mdbt53_db_40") OR
    ("${BOARD}" STREQUAL "nrf5340bsim_nrf5340_cpuapp"))
    message(INFO " ${BOARD} used for Application Core")
else()
    message(FATAL_ERROR "${BOARD} is not supported")
endif()

project(zeus_le_audio LANGUAGES C)

add_subdirectory(../../common common)
add_subdirectory(../common audio_common)
add_subdirectory(src)

target_link_libraries(app PRIVATE zeus_common zeus_audio_common)
