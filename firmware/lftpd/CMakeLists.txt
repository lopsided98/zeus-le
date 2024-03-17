add_library(lftpd STATIC
    lftpd_inet.c
    lftpd_io.c
    lftpd_string.c
    lftpd.c
)
target_link_libraries(lftpd PRIVATE zephyr_interface kernel)
target_include_directories(lftpd PRIVATE private)
target_include_directories(lftpd PUBLIC include)