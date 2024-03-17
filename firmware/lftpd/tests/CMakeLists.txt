# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lftpd_tests)

add_subdirectory(.. lftpd)

target_sources(app PRIVATE 
    test_lftpd_io.c
)
target_include_directories(app PRIVATE ../private)
target_link_libraries(app PRIVATE lftpd)