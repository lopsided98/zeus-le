# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(("${BOARD}" STREQUAL "raytac_mdbt53_db_40_nrf5340_cpuapp") OR
	("${BOARD}" STREQUAL "nrf5340bsim_nrf5340_cpuapp"))
	message(INFO " ${BOARD} used for Application Core")
else()
	message(FATAL_ERROR "${BOARD} is not supported")
endif()

project(zeus_le_central LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ../common/src)
