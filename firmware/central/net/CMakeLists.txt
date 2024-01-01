# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(("${BOARD}" STREQUAL "raytac_mdbt53_db_40_nrf5340_cpunet") OR
	("${BOARD}" STREQUAL "nrf5340bsim_nrf5340_cpunet"))
	message(INFO " ${BOARD} used for Network Core")
else()
	message(FATAL_ERROR "${BOARD} is not supported for this sample")
endif()

project(zeus_le_central_net LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/hci_ipc.c
)
target_include_directories(app PRIVATE
    ../common/src
    ${ZEPHYR_BASE}/subsys # Need private Bluetooth headers
)