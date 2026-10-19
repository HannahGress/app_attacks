#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_ccm.h>
#include "../include/nRF5340_dppi.h"
// load cmake with set(ENV{BOARD} nrf5340dk/nrf5340/cpunet) so that the following two functions does not throw any errors

/*
* Everything related to KSGEN ENC start -> 0
* Everything related to KSGEN DECR start -> 1
* Everything related to KSGEN end -> 2
* Everything related to ENCR start -> 3
* Everything related to DECR start -> 4
* Everything related to ENDCRYPT -> 5
*/

void hal_radio_ccm_nRF5340_dppi_config() {

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

    /* CCM EVENT_ENDKSGEN -> publish on DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_ccm_publish_set(
        NRF_CCM,
        NRF_CCM_EVENT_ENDKSGEN,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* TIMER1 CAPTURE[3] subscribes to DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER2,
        NRF_TIMER_TASK_CAPTURE2,
        KSGEN_END_DPPI_CHANNEL
    );

    /* Enable DPPI channel ENCRYPT_START_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_enable(
        NRF_DPPIC,
        BIT(KSGEN_END_DPPI_CHANNEL)
    );



    /* ENCRYPTION */

    /* CCM EVENT_ENDKSGEN -> publish on DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_ccm_publish_set(
        NRF_CCM,
        NRF_CCM_EVENT_ENDKSGEN,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* TIMER1 CAPTURE[3] subscribes to DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER2,
        NRF_TIMER_TASK_CAPTURE3,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* Enable DPPI channel ENCRYPT_START_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_enable(
        NRF_DPPIC,
        BIT(ENCRYPT_START_DPPI_CHANNEL)
    );


    /* DECRYPTION */
    /* RADIO EVENTS_PAYLOAD -> publish on DPPI channel DECRYPT_START_DPPI_CHANNEL */
    nrf_radio_publish_set(
        NRF_RADIO,
        NRF_RADIO_EVENT_ADDRESS,
        DECRYPT_START_DPPI_CHANNEL
    );

    /* TIMER1 CAPTURE[4] subscribes to DPPI channel DECRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER2,
        NRF_TIMER_TASK_CAPTURE4,
        DECRYPT_START_DPPI_CHANNEL
    );

    /* Enable DPPI channel DECRYPT_START_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_enable(
        NRF_DPPIC,
        BIT(DECRYPT_START_DPPI_CHANNEL)
    );


    /* ENDCRYPTION */

    /* NRF_CCM EVENTS_ENDCRYPT -> publish on DPPI channel ENDCRYPT_END_DPPI_CHANNEL */
    nrf_ccm_publish_set(
        NRF_CCM,
        NRF_CCM_EVENT_END,
        ENDCRYPT_END_DPPI_CHANNEL
    );

    /* TIMER1 CAPTURE[5] subscribes to DPPI channel ENDCRYPT_END_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER2,
        NRF_TIMER_TASK_CAPTURE5,
        ENDCRYPT_END_DPPI_CHANNEL
    );

    /* Enable DPPI channel ENDCRYPT_END_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_enable(
        NRF_DPPIC,
        BIT(ENDCRYPT_END_DPPI_CHANNEL)
    );
}

void hal_radio_ccm_nRF5340_ppi_disable() {
    /* ENCRYPTION */

    /* Disable DPPI channel ENCRYPT_START_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_disable(
        NRF_DPPIC,
        BIT(ENCRYPT_START_DPPI_CHANNEL)
    );


    /* DECRYPTION */

    /* Disable DPPI channel DECRYPT_START_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_disable(
        NRF_DPPIC,
        BIT(DECRYPT_START_DPPI_CHANNEL)
    );


    /* ENDCRYPTION */

    /* Disable DPPI channel ENDCRYPT_END_DPPI_CHANNEL in DPPIC */
    nrf_dppi_channels_disable(
        NRF_DPPIC,
        BIT(ENDCRYPT_END_DPPI_CHANNEL)
    );
}
#endif