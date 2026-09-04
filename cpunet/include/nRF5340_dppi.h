#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
#include <nrf5340_network.h>

#define ENCRYPT_START_DPPI_CHANNEL 0
#define DECRYPT_START_DPPI_CHANNEL 1
#define ENDCRYPT_END_DPPI_CHANNEL 2

void hal_radio_ccm_nRF5340_dppi_config();
void hal_radio_ccm_nRF5340_ppi_disable();
#endif
