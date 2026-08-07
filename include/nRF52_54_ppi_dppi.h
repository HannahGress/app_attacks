#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nrf52840.h>
#include <hal/nrf_ppi.h>
#endif



#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
/*
 * For benchmarking the nRF53840 & nRF5340
 * Timer offsets /variables for capturing start and end of KSGEN and de-/encryption
 */
#define HAL_EVENT_TIMER_CCM_START_ENCRYPT_CC_OFFSET 0
#define HAL_EVENT_TIMER_CCM_START_DECRYPT_CC_OFFSET 1
#define HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET 2

/*
 * channel to capture start & end of encryption or decryption
 */
#define HAL_CRYPT_START_TIME_ENCRYPT_PPI 0
#define HAL_CRYPT_START_TIME_DECRYPT_PPI 1
#define HAL_CRYPT_END_TIME_ENDCRYPT_PPI 2

void hal_radio_ccm_nRF52840_ppi_config(void);
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
int send_signal_nRF5340_dppi_config();
int send_signal_nRF5340_dppi_disable();
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#define ENCRYPT_START_DPPI_CHANNEL 0
#define DECRYPT_START_DPPI_CHANNEL 1
#define ENDCRYPT_END_DPPI_CHANNEL 2
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
void hal_radio_ccm_nRF4L15_ppi_config(void);
void hal_radio_ccm_nRF4L15_ppi_disable(void);
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
extern volatile bool is_benchmarking;
// variables to store the time for each time point in the encr/decr chain
extern volatile uint32_t t_start_ENCRYPT;
extern volatile uint32_t t_start_DECRYPT;
extern volatile uint32_t t_end_ENDCRYPT;
extern volatile uint32_t enc_count;
//we must ensure that enc_count is only used while benchmarking
extern volatile uint32_t delta_ENCRYPT;
extern volatile uint32_t delta_DECRYPT;
// we allow a maximum of 100 packets transmitted per send_data() method
#define SUM_ARRAY_MAX_SIZE 100
extern volatile uint32_t values_ENCRYPT[SUM_ARRAY_MAX_SIZE];
extern volatile uint32_t values_DECRYPT[SUM_ARRAY_MAX_SIZE];
#endif
