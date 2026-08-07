#include "../include/nRF52_54_ppi_dppi.h"

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <hal/nrf_ppi.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <ipc_service.h>
#include "zephyr/ipc/ipc_service.h"
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_ccm.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52LX) || defined(CONFIG_SOC_COMPATIBLE_NRF54L)
volatile bool is_benchmarking;
// variables to store the time for each time point in the encr/decr chain
volatile uint32_t t_start_ENCRYPT;
volatile uint32_t t_start_DECRYPT;
volatile uint32_t t_end_ENDCRYPT;
volatile uint32_t enc_count;
//we must ensure that enc_count is only used while benchmarking
volatile uint32_t delta_ENCRYPT;
volatile uint32_t delta_DECRYPT;
// we allow a maximum of 100 packets transmitted per send_data() method
#define SUM_ARRAY_MAX_SIZE 100
volatile uint32_t values_ENCRYPT[SUM_ARRAY_MAX_SIZE];
volatile uint32_t values_DECRYPT[SUM_ARRAY_MAX_SIZE];
#endif

/*
 * For benchmarking the nRF53840
 * check documentation for HW trigger: https://docs.nordicsemi.com/bundle/ps_nrf52840/page/ccm.html
 */
#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

void hal_radio_ccm_nRF52840_ppi_config(void) {

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


#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

static struct ipc_ept benchmark_ept;

static int signal_cpunet(uint8_t command)
{
    int ret;

    ret = ipc_service_send(
        &benchmark_ept,
        &command,
        sizeof(command)
    );

    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(command)) {
        return -EIO;
    }

    return 0;
}


int send_signal_nRF5340_dppi_config() {
    return signal_cpunet(BENCHMARK_CMD_DPPI_CONFIG);
}

int send_signal_nRF5340_dppi_disable() {
    return signal_cpunet(BENCHMARK_CMD_DPPI_DISABLE);
}
#endif



#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

void hal_radio_ccm_nRF54L15_ppi_config(){

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
    nrf_ccm_publish_set(
        NRF_CCM,
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

void hal_radio_ccm_nRF45L15_ppi_disable(){

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