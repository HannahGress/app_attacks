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
 * Channel for the end of KSGEN (CC[2]); its FORK also captures the encryption start (CC[3]),
 * because the encryption starts on that same ENDKSGEN event (ENDKSGEN_CRYPT short)
 */
#define HAL_CRYPT_END_TIME_KSGEN_PPI 0

/* Different channel for decryption start, but the end event is the same, so we only need one channel for the end event */
#define HAL_CRYPT_START_TIME_DECRYPT_PPI 2
#define HAL_CRYPT_END_TIME_ENDCRYPT_PPI 3

/*
 * KSGEN START IN HARDWARE (TX only)
 *
 * The CCM has no "KSGEN started" event. So while benchmarking, radio.c triggers
 * EGU0 instead of CCM TASKS_KSGEN, and this PPI channel starts KSGEN (TEP) and
 * captures t_start_KSGEN in TIMER3 CC[0] (FORK) on the same clock edge, no CPU
 * cycles in between. It is only enabled in benchmark_on(); otherwise radio.c
 * starts KSGEN directly, so encryption never depends on this channel.
 * EGU0/SWI0 is unused here (the controller only uses the SWI4/SWI5 IRQs) and
 * its interrupt stays disabled.
 *
 * /!\ Must be a channel no driver allocates at runtime: Zephyr's nrfx allocator
 * hands out the HIGHEST free channel first (e.g. the UART takes 19 for
 * ENDTX->STOPTX on boards with a serial console). Channel 1 is handed out last.
 */
#define KSGEN_START_EGU_EVENT        0   /* NRF_EGU0 TASKS_TRIGGER/EVENTS_TRIGGERED[0] */
#define KSGEN_START_ENC_PPI          1

/*
 * KSGEN MARKER (active low) on P0.<KSGEN_MARKER_PIN> -> wire to PPK II D0
 *
 * Goes LOW for the keystream generation of a benchmarked notification only:
 *   EGU0 (= KSGEN start)  -> pin LOW
 *   CCM EVENTS_ENDKSGEN   -> pin HIGH
 * The DATA-PACKET MARKER on P0.04 (D1) below goes LOW on that same ENDKSGEN
 * edge, so per packet the PPK II sees a D0 pulse directly followed by a D1 pulse
 * (KSGEN, then encryption). Gated per packet exactly like P0.04 (enabled by the
 * filter in lll_conn.c, disabled in lll_conn_isr_tx).
 */
#define KSGEN_MARKER_PORT            0   /* GPIO port: P0 -> 0, P1 -> 1         */
#define KSGEN_MARKER_PIN             3   /* P0.03: free on the DK (no LED/button,
                                          * draws no current of its own)        */
#define KSGEN_MARKER_GPIOTE_CH       0   /* free GPIOTE channel (0..7)          */
#define KSGEN_MARKER_START_PPI       4   /* free PPI channel: pin -> LOW       */
#define KSGEN_MARKER_END_PPI         5   /* free PPI channel: pin -> HIGH      */

/*
 * DATA-PACKET MARKER (active low) on P0.<PACKET_MARKER_PIN>  -> wire to PPK II D1
 *
 * It goes LOW ONLY for the encryption of a real send_data notification
 * (KSGEN excluded, that one is on P0.03 / D0 above).
 *
 * Wired to the CCM events (ENDKSGEN -> LOW, ENDCRYPT -> HIGH) over PPI +
 * GPIOTE, so the pin edges are hardware-crisp with
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
 * them causes supervision timeouts (0x08). 17/18/19 are only used by the
 * controller with Coded PHY or direction finding enabled, recheck
 * radio_nrf5_ppi_resources.h if you turn those on. 19 is taken by the UART
 * driver at runtime (see KSGEN_START_ENC_PPI above), so 17/18 are the next
 * ones it would hand out if another driver allocates a PPI channel. */
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
