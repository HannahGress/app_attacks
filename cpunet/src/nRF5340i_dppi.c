#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_ccm.h>
#include <nRF5340_dppi.h>
// load cmake with set(ENV{BOARD} nrf5340dk/nrf5340/cpunet) so that the following two functions does not throw any errors

void hal_radio_ccm_nRF5340_dppi_config() {

    /* ENCRYPTION */

    /* CCM EVENT_ENDKSGEN -> publish on DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_ccm_publish_set(
        NRF_CCM,
        NRF_CCM_EVENT_ENDKSGEN,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* TIMER1 CAPTURE[0] subscribes to DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER1,
        NRF_TIMER_TASK_CAPTURE0,
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

    /* TIMER1 CAPTURE[1] subscribes to DPPI channel DECRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER1,
        NRF_TIMER_TASK_CAPTURE1,
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

    /* TIMER1 CAPTURE[2] subscribes to DPPI channel ENDCRYPT_END_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER1,
        NRF_TIMER_TASK_CAPTURE2,
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