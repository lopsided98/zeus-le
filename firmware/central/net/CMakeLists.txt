# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zeus_le_central_net LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/hci_ipc.c
)