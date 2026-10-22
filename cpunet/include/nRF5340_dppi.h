#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
#include <nrf5340_network.h>

/*
* Everything related to KSGEN ENC start -> 0
* Everything related to KSGEN DECR start -> 1
* Everything related to KSGEN end -> 2
* Everything related to ENCR start -> 3
* Everything related to DECR start -> 4
* Everything related to ENDCRYPT -> 5
*/

/*
 * For benchmarking the nRF5340
 * Timer offsets /variables for capturing start and end of KSGEN and de-/encryption
 */

/* KSGEN */
#define HAL_EVENT_TIMER_KSGEN_START_ENC_CC_OFFSET 0
#define HAL_EVENT_TIMER_KSGEN_START_DECR_CC_OFFSET 1
/* Timer for the end of KSGEN*/
#define HAL_EVENT_TIMER_KSGEN_END_CC_OFFSET 2

/*
 * Different timer for encryption and decryption start, but the end event is the same, so we only need one timer for the end event.
 */
#define HAL_EVENT_TIMER_CCM_START_ENC_CC_OFFSET 3
#define HAL_EVENT_TIMER_CCM_START_DECR_CC_OFFSET 4
#define HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET 5

/* DPPI Channels */
#define KSGEN_END_DPPI_CHANNEL 2
#define ENDCRYPT_END_DPPI_CHANNEL 1 // this channel is exceptionally set to 1, because 5 is already in use

void hal_radio_ccm_nRF5340_dppi_config();
void hal_radio_ccm_nRF5340_ppi_disable();
#endif
