# SPDX-License-Identifier: Apache-2.0

if((CONFIG_BOARD_ZEUS_LE_NRF5340_CPUAPP OR CONFIG_BOARD_ZEUS_LE_NRF5340_CPUAPP_NS)
   AND CONFIG_BOARD_ENABLE_CPUNET)
  zephyr_library()
  zephyr_library_sources(zeus_le_nrf5340_cpunet_reset.c)
endif()
