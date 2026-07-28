#include <hal/nrf_ccm.h>
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include "nRF52_54_ppi_dppi.h"

void hal_radio_ccm_nRF4L15_ppi_config(){

    /* ENCRYPTION */

    /* RADIO EVENTS_READY -> publish on DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_radio_publish_set(
        NRF_RADIO,
        NRF_RADIO_EVENT_READY,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* TIMER10 CAPTURE[0] subscribes to DPPI channel ENCRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER10,
        NRF_TIMER_TASK_CAPTURE0,
        ENCRYPT_START_DPPI_CHANNEL
    );

    /* Enable DPPI channel ENCRYPT_START_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_enable(
        NRF_DPPIC10,
        BIT(ENCRYPT_START_DPPI_CHANNEL)
    );


    /* DECRYPTION */
    /* RADIO EVENTS_PAYLOAD -> publish on DPPI channel DECRYPT_START_DPPI_CHANNEL */
    nrf_radio_publish_set(
        NRF_RADIO,
        NRF_RADIO_EVENT_PAYLOAD,
        DECRYPT_START_DPPI_CHANNEL
    );

    /* TIMER10 CAPTURE[1] subscribes to DPPI channel DECRYPT_START_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER10,
        NRF_TIMER_TASK_CAPTURE1,
        DECRYPT_START_DPPI_CHANNEL
    );

    /* Enable DPPI channel DECRYPT_START_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_enable(
        NRF_DPPIC10,
        BIT(DECRYPT_START_DPPI_CHANNEL)
    );

    /* ENDCRYPTION */

    /* NRF_CCM EVENTS_ENDCRYPT -> publish on DPPI channel ENDCRYPT_END_DPPI_CHANNEL */
    nrf_radio_publish_set(
        NRF_RADIO,
        NRF_CCM_EVENT_END,
        ENDCRYPT_END_DPPI_CHANNEL
    );

    /* TIMER10 CAPTURE[2] subscribes to DPPI channel ENDCRYPT_END_DPPI_CHANNEL */
    nrf_timer_subscribe_set(
        NRF_TIMER10,
        NRF_TIMER_TASK_CAPTURE2,
        ENDCRYPT_END_DPPI_CHANNEL
    );

    /* Enable DPPI channel ENDCRYPT_END_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_enable(
        NRF_DPPIC10,
        BIT(ENDCRYPT_END_DPPI_CHANNEL)
    );

}

void hal_radio_ccm_nRF4L15_ppi_disable(){

    /* ENCRYPTION */

    /* Disable DPPI channel ENCRYPT_START_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_disable(
        NRF_DPPIC10,
        BIT(ENCRYPT_START_DPPI_CHANNEL)
    );


    /* DECRYPTION */

    /* Disable DPPI channel DECRYPT_START_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_disable(
        NRF_DPPIC10,
        BIT(DECRYPT_START_DPPI_CHANNEL)
    );

    /* ENDCRYPTION */

    /* Disable DPPI channel ENDCRYPT_END_DPPI_CHANNEL in DPPIC10 */
    nrf_dppi_channels_disable(
        NRF_DPPIC10,
        BIT(ENDCRYPT_END_DPPI_CHANNEL)
    );

}
#endif


/*
 * For benchmarking the nRF53840
 * check documentation for HW trigger: https://docs.nordicsemi.com/bundle/ps_nrf52840/page/ccm.html
 */
#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

static void hal_radio_ccm_nRF52840_ppi_config(void) {

    // when encryption starts -> take actual time
    nrf_ppi_channel_endpoint_setup(
    NRF_PPI,
    HAL_CRYPT_START_TIME_ENCRYPT_PPI,
    (uint32_t)&NRF_CCM->EVENTS_ENDKSGEN,
    //(uint32_t)&NRF_RADIO->EVENTS_ADDRESS,
    (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_START_ENCRYPT_CC_OFFSET]);

    // when decryption starts (closer to it)
    nrf_ppi_channel_endpoint_setup(
    NRF_PPI,
    HAL_CRYPT_START_TIME_DECRYPT_PPI,
    (uint32_t)&NRF_RADIO->EVENTS_ADDRESS,
    //(uint32_t)&NRF_RADIO->EVENTS_PAYLOAD,
    (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_START_DECRYPT_CC_OFFSET]);


    // when encryption and decryption ends
    nrf_ppi_channel_endpoint_setup(
        NRF_PPI,
        HAL_CRYPT_END_TIME_ENDCRYPT_PPI,
        (uint32_t)&(NRF_CCM->EVENTS_ENDCRYPT),
        (uint32_t)&(NRF_TIMER3->TASKS_CAPTURE[HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET]));
}
#endif