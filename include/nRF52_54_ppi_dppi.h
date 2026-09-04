#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
//#include <nrf52840.h>
//#include <hal/nrf_ppi.h>
#include <zephyr/bluetooth/conn.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
/*
 * For benchmarking the nRF53840
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

/*
 * channel to capture start & end of KSGEN, encryption or decryption
 */

/*
 * Channel for the end of KSGEN
 * The start is captured in the code, not over Event/Task chain
 */
#define HAL_CRYPT_END_TIME_KSGEN_PPI 0

/* Different channel for encryption and decryption start, but the end event is the same, so we only need one channel for the end event */
#define HAL_CRYPT_START_TIME_ENCRYPT_PPI 1
#define HAL_CRYPT_START_TIME_DECRYPT_PPI 2
#define HAL_CRYPT_END_TIME_ENDCRYPT_PPI 3

void hal_radio_ccm_nRF52840_ppi_config();

/* We only have KSGEN in the AES-CCM peripheral of the nRF52840 (and nRF5340, but its values are defined elsewhere) */
/* The nRF54L15 spec does not mention KSGEN as part of the AES-CCm peripheral task */

extern volatile bool is_benchmarking;
extern volatile struct benchmark_measurement encryption_measurement;
extern volatile struct benchmark_measurement decryption_measurement;

struct benchmark_measurement {
 uint32_t t_start_KSGEN;
 uint32_t t_end_KSGEN;
 uint32_t delta_KSGEN;

 uint32_t t_start_ENDCRYPT;
 uint32_t t_end_ENDCRYPT;
 uint32_t delta_ENDCRYPT;
};

/* we allow a maximum of 100 packets transmitted per send_data() method */
#define SUM_ARRAY_MAX_SIZE 100
extern volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
extern volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
extern volatile uint32_t encryption_measurement_count;
extern volatile uint32_t decryption_measurement_count;
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
int send_signal_nRF5340_dppi_config();
int send_signal_nRF5340_dppi_disable();
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

/*
 * ENCRYPTION
 * we don't need a separate encryption DPPI channel
 * For encryption, we capture the appropriate function call;
 * Everything related to encryption is 0; Decryption is 1 and ENDCRYPT 2
 * For decryption. we can listen to the existing one of Zephyr, but need some additional channels and PPI Bridges
 * The same applies for ENDCRYPT
 */

/*
 * DECRYPTION
 * We must pass an Event from one PPI Domain to another (RADIO to PERI(PHERAL)), therefore, we need a PPI Bridge (PPIB)
 */
#define DECRYPT_START_PPIB_CHANNEL  1
#define DECRYPT_START_DPPI_CHANNEL  1

/*
 * ENDCRYPT
 * Measurement in the MCU domain
 */
#define ENDCRYPT_DPPI_CHANNEL 2

extern void hal_radio_ccm_nRF54L15_ppi_config(void);
extern void hal_radio_ccm_nRF54L15_ppi_disable(void);

/* Since our timer runs at 128 MHz, each tick is 7.8125 ns. Therefore, we must multiply our results with this number */
static const double TIMER_TICK_NS = 7.8125;

/* Semaphore to ensure that the readout on the shell is consistent */
extern struct k_sem encryption_measurement_sem;

/* We must declare the payload as an external variable, because we later filter for packages with this payload + 7 Bytes header data (+ other data) */
extern volatile int payload_size_i;
extern volatile bool is_benchmarking;

/* /!\ we only save ticks in the structs and multiply by TIMER_TICK_NS when printing the values (less time critical and more efficient) */
extern volatile struct benchmark_measurement encryption_measurement;
extern volatile struct benchmark_measurement decryption_measurement;

struct benchmark_measurement {
 uint32_t t_start_ENDCRYPT;
 uint32_t t_end_ENDCRYPT;
 uint32_t delta_ENDCRYPT;
};

/* we allow a maximum of 100 packets transmitted per send_data() method */
#define SUM_ARRAY_MAX_SIZE 100
/* /!\ we only save ticks in the arrazy and multiply by TIMER_TICK_NS when printing the values (less time critical and more efficient) */
extern volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
extern volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
extern volatile uint32_t encryption_measurement_count;
extern volatile uint32_t decryption_measurement_count;
#endif
