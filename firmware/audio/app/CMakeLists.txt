# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

# app.overlay is not used automatically if a board-specific overlay is present
list(APPEND EXTRA_DTC_OVERLAY_FILE app.overlay)
# FIXME: this doesn't work, have to manually specify on command line
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../common)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(("${BOARD}" STREQUAL "zeus_le") OR
    ("${BOARD}" STREQUAL "raytac_mdbt53_db_40") OR
    ("${BOARD}" STREQUAL "nrf5340bsim"))
    message(INFO " ${BOARD} used for Application Core")
else()
    message(FATAL_ERROR "${BOARD} is not supported")
endif()

project(zeus_le_audio LANGUAGES C)

add_subdirectory(../../common common)
add_subdirectory(../common audio_common)
add_subdirectory(../../lftpd lftpd)
add_subdirectory(src)

target_link_libraries(app PRIVATE
    zeus_common
    zeus_audio_common
    lftpd
)
