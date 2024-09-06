# SPDX-License-Identifier: GPL-3.0-or-later

add_library(zeus_common STATIC)
target_include_directories(zeus_common PUBLIC include)
target_link_libraries(zeus_common PRIVATE zephyr_interface kernel)

add_subdirectory(src)