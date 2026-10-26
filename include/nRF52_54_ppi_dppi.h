#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <zephyr/bluetooth/conn.h>

/*
 * For benchmarking the nRF52840
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

/*
 * GPIO current-measurement marker (nRF52840) -- CROSS-CHECK ONLY
 *
 * /!\ This pin (P0.03, PPK II D0) is NOT the measurement we are interested in.
 * /!\ The relevant measurement (only the send_data notification encryption)
 * /!\ is the DATA-PACKET MARKER on P0.04 (PACKET_MARKER_*, PPK II D1) below.
 *
 * P0.03 marks EVERY AES-CCM operation, i.e. all encryptions AND all
 * decryptions (on the nRF52 the CCM ENDKSGEN/ENDCRYPT events are shared by
 * both). We keep it only as a reference/sanity check: every P0.04 pulse must
 * line up with exactly one P0.03 pulse, which shows that the P0.04 filter
 * picks the right CCM operation.
 *
 * Drives the pin LOW for the duration of each AES-CCM operation (active-low),
 * fired straight from the CCM hardware events over PPI + GPIOTE, so there is
 * no CPU involvement and no software jitter:
 *   EVENTS_ENDKSGEN -> pin LOW  (crypt starts)
 *   EVENTS_ENDCRYPT -> pin HIGH (crypt ends)
 *
 * To measure a different pin, change PORT/PIN and rebuild + reflash.
 */
#define CURRENT_MARKER_PORT          0   /* GPIO port: P0 -> 0, P1 -> 1         */
#define CURRENT_MARKER_PIN           3   /* P0.03: free on the DK (no LED/button,
                                          * draws no current of its own)        */
#define CURRENT_MARKER_GPIOTE_CH     0   /* free GPIOTE channel (0..7)          */
#define CURRENT_MARKER_ENC_START_PPI 4   /* free PPI channel: pin -> LOW       */
#define CURRENT_MARKER_ENC_END_PPI   5   /* free PPI channel: pin -> HIGH      */

/*
 * DATA-PACKET MARKER (active low) on P0.<PACKET_MARKER_PIN>  -> wire to PPK II D1
 *
 * >>> THIS IS THE RELEVANT MEASUREMENT PIN. <<<
 * It goes LOW ONLY for the encryption of a real send_data notification, so it
 * can be used on its own; P0.03 (D0) above is just the cross-check.
 *
 * Wired to the same CCM events as the current marker (ENDKSGEN -> LOW,
 * ENDCRYPT -> HIGH) over PPI + GPIOTE, so the pin edges are hardware-crisp with
 * no software jitter. The difference: these two PPI channels are kept DISABLED
 * by default and only enabled, per packet, by the `benchmarked_tx_packet`
 * filter in the controller (lll_conn.c) for a real send_data TX encryption,
 * then disabled again in lll_conn_isr_tx. That gates the hardware marker with
 * the software decision without ever lagging the pin edges.
 */
#define PACKET_MARKER_PORT           0   /* GPIO port: P0 -> 0, P1 -> 1         */
#define PACKET_MARKER_PIN            4   /* P0.04 (AIN2): free on the DK        */
#define PACKET_MARKER_GPIOTE_CH      1   /* free GPIOTE channel (0..7)          */
/* NOTE: PPI 6 and 7 are NOT free, the BLE controller owns them
 * (HAL_EVENT_TIMER_START_PPI / HAL_RADIO_READY_TIME_CAPTURE_PPI) and touching
 * them causes supervision timeouts (0x08). 17/18 are only used by the
 * controller with Coded PHY or direction finding enabled, recheck
 * radio_nrf5_ppi_resources.h if you turn those on. */
#define PACKET_MARKER_ENC_START_PPI  17  /* free PPI channel: pin -> LOW       */
#define PACKET_MARKER_ENC_END_PPI    18  /* free PPI channel: pin -> HIGH      */

void hal_radio_ccm_nRF52840_ppi_config();

/* We only have KSGEN in the AES-CCM peripheral of the nRF52840 (and nRF5340, but its values are defined elsewhere) */
/* The nRF54L15 spec does not mention KSGEN as part of the AES-CCm peripheral task */

/*
 * Benchmark timer (TIMER3): f_timer = BENCHMARK_TIMER_BASE_FREQ_HZ / 2^BENCHMARK_TIMER_PRESCALER
 * init_timer() in main.c programs BENCHMARK_TIMER_PRESCALER, and TIMER_TICK_NS is
 * derived from the same two values, so the conversion to ns can't get out of sync.
 * 16 MHz, prescaler 0 -> 62.5 ns per tick (32 bit wraps after ~268 s).
 */
#define BENCHMARK_TIMER_BASE_FREQ_HZ 16000000UL
#define BENCHMARK_TIMER_PRESCALER    0
static const double TIMER_TICK_NS =
    1e9 / (double)(BENCHMARK_TIMER_BASE_FREQ_HZ >> BENCHMARK_TIMER_PRESCALER);

// given by radio ISR (lll_conn_isr_tx) when a new encryption measurement is stored
extern struct k_sem encryption_measurement_sem;


/* We must declare the payload as an external variable, because we later filter for packages with this payload + 7 Bytes header data (+ other data) */
extern volatile int payload_size_i;
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
// variables etc. are in shared_variables.h
/* Since our timer runs at 16 MHz, each tick is 62.5 ns. Therefore, we must multiply our results with this number */
static const double TIMER_TICK_NS = 62.5;
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
 * Since we work in different domains and bridges, the channel numbers can be the same, because the channels are different
 * in/for each domain/bridge
 */
//#define DECRYPT_START_RADIO_DPPI_CHANNEL  1
//#define DECRYPT_START_PPIB_CHANNEL  1
//#define DECRYPT_START_MCU_DPPI_CHANNEL  1

/*
 * ENDCRYPT
 * Measurement in the MCU domain
 */
#define ENDCRYPT_DPPI_CHANNEL 3

extern void hal_radio_ccm_nRF54L15_ppi_config(void);
extern void hal_radio_ccm_nRF54L15_ppi_disable(void);

/*
 * Benchmark timer (TIMER00): f_timer = BENCHMARK_TIMER_BASE_FREQ_HZ / 2^BENCHMARK_TIMER_PRESCALER
 * init_timer() in main.c programs BENCHMARK_TIMER_PRESCALER, and TIMER_TICK_NS is
 * derived from the same two values, so the conversion to ns can't get out of sync.
 * TIMER00 is clocked at 128 MHz, prescaler 0 -> 7.8125 ns per tick (32 bit wraps after ~33 s).
 */
#define BENCHMARK_TIMER_BASE_FREQ_HZ 128000000UL
#define BENCHMARK_TIMER_PRESCALER    0
static const double TIMER_TICK_NS =
    1e9 / (double)(BENCHMARK_TIMER_BASE_FREQ_HZ >> BENCHMARK_TIMER_PRESCALER);

// Given by the radio ISR (lll_conn_isr_tx) when a new encryption measurement is stored
extern struct k_sem encryption_measurement_sem;

/* We must declare the payload as an external variable, because we later filter for packages with this payload + 7 Bytes header data (+ other data) */
extern volatile int payload_size_i;

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
