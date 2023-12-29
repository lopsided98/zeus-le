# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

set(BOARD raytac_mdbt53_db_40_nrf5340_cpuapp)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zeus_le_central LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ../common/src)
