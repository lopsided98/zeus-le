add_library(hci_ipc STATIC src/hci_ipc.c)
target_link_libraries(hci_ipc zephyr_interface)
target_include_directories(hci_ipc INTERFACE include)