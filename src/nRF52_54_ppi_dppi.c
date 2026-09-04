#include <nRF52_54_ppi_dppi.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <hal/nrf_ppi.h>
#include "zephyr/bluetooth/conn.h"
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
#include <hal/nrf_ppib.h>
#include <zephyr/kernel.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
/* We only have KSGEN in the AES-CCM peripheral of the nRF52840 (and nRF5340, but its values are defined elsewhere) */
/* The nRF54L15 spec does not mention KSGEN as part of the AES-CCm peripheral task */
volatile bool is_benchmarking;
volatile struct benchmark_measurement encryption_measurement;
volatile struct benchmark_measurement decryption_measurement;
volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
volatile uint32_t encryption_measurement_count;
volatile uint32_t decryption_measurement_count;
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

K_SEM_DEFINE(encryption_measurement_sem, 0, 1);
volatile int payload_size_i = 0;
volatile bool is_benchmarking = false;
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

    /*
     * Encryption start is captured in radio.c, line 2521 in the function
     * static void *radio_ccm_ext_tx_pkt_set(struct ccm *cnf, uint8_t pdu_type, void *pkt)
     */


    /* DECRYPTION */
    /*
     * RADIO EVENTS_PAYLOAD -> publish on DPPI channel HAL_TRIGGER_CRYPT_PPI
     * Therefore, we can listen to this channel and don't need to configure our own publisher anc channel
     * I addition, the RADIO and MCU reside in different DPPI domains, therefore, we need a PPI Bridge (PPIB)
     * A PPIB channel can transfer messages between different domains by subscribing and publishing
     * PPIB10 in RADIO PD is hardwired to PPIB00 in MCU PD (https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/ppi.html)
     */

    /* Our PPIB channel subscribes to the HAL_TRIGGER_CRYPT_PPI */
    nrf_ppib_subscribe_set(
        NRF_PPIB10,
        nrf_ppib_send_task_get(DECRYPT_START_PPIB_CHANNEL),
    HAL_TRIGGER_CRYPT_PPI
    );

    /*
     * MCU domain:
     * The PPIB channel receives the data in the MCU domain and publishes it onto the DECRYPT_START_DPPI_CHANNEL
     */
    nrf_ppib_publish_set(
        NRF_PPIB00,
        nrf_ppib_receive_event_get(DECRYPT_START_PPIB_CHANNEL),
        DECRYPT_START_DPPI_CHANNEL
    );

    /* The TIMER00 instance subscribes to the DECRYPT_START_DPPI_CHANNEL and saves the value into one of its register*/
    nrf_timer_subscribe_set(
        NRF_TIMER00,
        NRF_TIMER_TASK_CAPTURE1,
        DECRYPT_START_DPPI_CHANNEL
    );

    /* Finally, we must activate our DECRYPT_START_DPPI_CHANNEL */
    nrf_dppi_channels_enable(
        NRF_DPPIC00,
    BIT(DECRYPT_START_DPPI_CHANNEL)
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

    /* DECRYPTION */

    /* Disable DPPI channel DECRYPT_START_DPPI_CHANNEL in DPPIC00 */
    nrf_dppi_channels_disable(
        NRF_DPPIC00,
        BIT(DECRYPT_START_DPPI_CHANNEL)
    );

    /* ENDCRYPTION */

    /* Disable DPPI channel ENDCRYPT_DPPI_CHANNEL in DPPIC00 */
    nrf_dppi_channels_disable(
        NRF_DPPIC00,
        BIT(ENDCRYPT_DPPI_CHANNEL)
    );

}
#endif