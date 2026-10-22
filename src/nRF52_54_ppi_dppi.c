#include <nRF52_54_ppi_dppi.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <hal/nrf_ppi.h>
#include "zephyr/bluetooth/conn.h"
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_ccm.h>
#include <hal/nrf_ppib.h>
#include <zephyr/kernel.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
/* We only have KSGEN in the AES-CCM peripheral of the nRF52840 (and nRF5340, but its values are defined elsewhere) */
/* The nRF54L15 spec does not mention KSGEN as part of the AES-CCm peripheral task */
K_SEM_DEFINE(encryption_measurement_sem, 0, 1);
volatile int payload_size_i = 0;
volatile struct benchmark_measurement encryption_measurement;
volatile struct benchmark_measurement decryption_measurement;
volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile uint32_t encryption_measurement_count = 0;
volatile uint32_t decryption_measurement_count = 0;
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

K_SEM_DEFINE(encryption_measurement_sem, 0, 1);
volatile int payload_size_i = 0;
volatile struct benchmark_measurement encryption_measurement;
volatile struct benchmark_measurement decryption_measurement;
volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile uint32_t encryption_measurement_count = 0;
volatile uint32_t decryption_measurement_count = 0;
#endif

/*
 * For benchmarking the nRF53840
 * check documentation for HW trigger: https://docs.nordicsemi.com/bundle/ps_nrf52840/page/ccm.html
 */
#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

void hal_radio_ccm_nRF52840_ppi_config() {

    /*
     * KSGEN
     */

    /*
     * ENCRYPTION:
     * start of KSGEN is triggered in SW -> radio.c, line 2532
     * in the function static void *radio_ccm_ext_tx_pkt_set(struct ccm *cnf, uint8_t pdu_type, void *pkt)
     * We do register this event/this task in a register of the TIMER3 instance.
     * We don't have a PPI channel for that anymore
     */

    /*
     * DECRYPTION:
     * start of KSGEN is triggered in SW -> radio.c, line 2342
     * static void *radio_ccm_ext_rx_pkt_set(struct ccm *cnf, uint8_t phy, uint8_t pdu_type, void *pkt)
     * We do register this event/this task in a register of the TIMER3 instance.
     * We don't have a PPI channel for that anymore
     */

    // ENDCR: when KSGEN ends
    nrf_ppi_channel_endpoint_setup(
    NRF_PPI,
    HAL_CRYPT_END_TIME_KSGEN_PPI,
    (uint32_t)&NRF_CCM->EVENTS_ENDKSGEN,
    (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_KSGEN_END_CC_OFFSET]);


    /*
     * ENCRYPTION & DECRYPTION
     */

    // when encryption starts -> take actual time
    nrf_ppi_channel_endpoint_setup(
    NRF_PPI,
    HAL_CRYPT_START_TIME_ENCRYPT_PPI,
    (uint32_t)&NRF_CCM->EVENTS_ENDKSGEN,
    (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_START_ENC_CC_OFFSET]);

    // when decryption starts (closer to it)
    nrf_ppi_channel_endpoint_setup(
    NRF_PPI,
    HAL_CRYPT_START_TIME_DECRYPT_PPI,
    (uint32_t)&NRF_RADIO->EVENTS_ADDRESS,
    (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_START_DECR_CC_OFFSET]);

    // when encryption and decryption ends
    nrf_ppi_channel_endpoint_setup(
        NRF_PPI,
        HAL_CRYPT_END_TIME_ENDCRYPT_PPI,
        (uint32_t)&(NRF_CCM->EVENTS_ENDCRYPT),
        (uint32_t)&(NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET]));
}
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

void hal_radio_ccm_nRF54L15_ppi_config(){

    /* ENCRYPTION */

    /*
     * Encryption start is captured in radio.c, line 2521 in the function
     * static void *radio_ccm_ext_tx_pkt_set(struct ccm *cnf, uint8_t pdu_type, void *pkt)
     */


    /* DECRYPTION */
    /*
    * The Zephyr BLE controller already publishes RADIO EVENTS_PAYLOAD on DPPI channel 7
    *  and bridges it to the MCU domain. Therefore, we can capture the channel in the MCU domain
    */

    nrf_timer_subscribe_set(
        NRF_TIMER00,
        NRF_TIMER_TASK_CAPTURE1,
        HAL_TRIGGER_CRYPT_PPI
    );


    /* ENDCRYPTION */

    /* NRF_CCM EVENTS_ENDCRYPT -> publish on DPPI channel ENDCRYPT_DPPI_CHANNEL */
    nrf_ccm_publish_set(
        NRF_CCM00,
        NRF_CCM_EVENT_END,
    ENDCRYPT_DPPI_CHANNEL
    );

    nrf_timer_subscribe_set(
        NRF_TIMER00,
        NRF_TIMER_TASK_CAPTURE2,
        ENDCRYPT_DPPI_CHANNEL
    );

    nrf_dppi_channels_enable(
        NRF_DPPIC00,
        BIT(ENDCRYPT_DPPI_CHANNEL)
    );
}

void hal_radio_ccm_nRF54L15_ppi_disable(){

    /*
     * ENCRYPTION
     * We haven't defined any DPPI channels, so we don't need to disable any
     */

    /* ENDCRYPTION */

    /* Disable DPPI channel ENDCRYPT_DPPI_CHANNEL in DPPIC00 */
    nrf_dppi_channels_disable(
        NRF_DPPIC00,
        BIT(ENDCRYPT_DPPI_CHANNEL)
    );

}
#endif