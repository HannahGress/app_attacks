#pragma once

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nrf52840.h>
#include <hal/nrf_ppi.h>
#endif

/*
 * For benchmarking the nRF53840
 * Timer offsets /variables for capturing start and end of KSGEN and de-/encryption
 */
#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#define HAL_EVENT_TIMER_CCM_START_ENCRYPT_CC_OFFSET 0
#define HAL_EVENT_TIMER_CCM_START_DECRYPT_CC_OFFSET 1
#define HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET 2
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
void hal_radio_ccm_nRF4L15_ppi_config(void);
void hal_radio_ccm_nRF4L15_ppi_disable(void);

#define ENCRYPT_START_DPPI_CHANNEL 0
#define DECRYPT_START_DPPI_CHANNEL 1
#define ENDCRYPT_END_DPPI_CHANNEL 2
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
extern bool is_benchmarking;
// variables to store the time for each time point in the encr/decr chain
extern uint32_t t_start_ENCRYPT;
extern uint32_t t_start_DECRYPT;
extern uint32_t t_end_ENDCRYPT;
extern uint32_t enc_count;
//we must ensure that enc_count is only used while benchmarking
extern uint32_t delta_ENCRYPT;
extern uint32_t delta_DECRYPT;
// we allow a maximum of 100 packets transmitted per send_data() method
#define SUM_ARRAY_MAX_SIZE 100
extern uint32_t values_ENCRYPT[SUM_ARRAY_MAX_SIZE];
extern uint32_t values_DECRYPT[SUM_ARRAY_MAX_SIZE];
#endif