# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(("${BOARD}" STREQUAL "zeus_le") OR
    ("${BOARD}" STREQUAL "raytac_mdbt53_db_40") OR
    ("${BOARD}" STREQUAL "nrf5340bsim"))
    message(INFO " ${BOARD} used for Network Core")
else()
    message(FATAL_ERROR "${BOARD} is not supported")
endif()

project(zeus_le_audio_net LANGUAGES C)

add_subdirectory(../../hci_ipc hci_ipc)
add_subdirectory(../common audio_common)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE
    hci_ipc
    zeus_audio_common
)
target_include_directories(app PRIVATE
    ${ZEPHYR_BASE}/subsys # Need private Bluetooth headers
)