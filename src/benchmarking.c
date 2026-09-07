#include <stdlib.h>
#include <zephyr/shell/shell.h>

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nRF52_54_ppi_dppi.h>
#include <controller/ll_sw/nordic/hal/nrf5/radio/radio_nrf5_ppi.h>
#include "zephyr/bluetooth/conn.h"
#include "zephyr/bluetooth/hci.h"
#include <main.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#include <nRF52_54_ppi_dppi.h>
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <shared_variables.h>
#endif

static void benchmark_on() {
    #if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    is_benchmarking = true;
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    //BENCHMARK_SHARED_VARIABLES->is_benchmarking = true;
    #endif

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    hal_radio_ccm_nRF52840_ppi_config();

    /* enable the PPi channels */
    hal_radio_nrf_ppi_channels_enable(
        BIT(HAL_CRYPT_END_TIME_KSGEN_PPI) |
        BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    send_signal_nRF5340_dppi_config();
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    hal_radio_ccm_nRF54L15_ppi_config();
    #endif
}

static void benchmark_off() {
    #if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    is_benchmarking = false;
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    //BENCHMARK_SHARED_VARIABLES->is_benchmarking = false;
    #endif

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    hal_radio_nrf_ppi_channels_disable(
        BIT(HAL_CRYPT_END_TIME_KSGEN_PPI) |
        BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP )
    send_signal_nRF5340_dppi_disable();
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    hal_radio_ccm_nRF54L15_ppi_disable();
    #endif
}

int cmd_benchmark(const struct shell *sh, size_t argc, char *argv[])
{
    // we can turn on and off the benchmarking
    const char *on_off = NULL;

    // We need all arguments for the benchmarking
    if (argc < 2) {
        shell_error(sh, "Wrong number of arguments.");
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }

    on_off = argv[1];
    if (*on_off == '\0') {
        shell_error(sh, "Argument must not be empty.");
        return -EINVAL;
    }
    if (!strcmp(on_off, "on")) {
        benchmark_on();
    } else if (!strcmp(on_off, "off")) {
        benchmark_off();
    } else {
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }
    return 0;
}


int cmd_get_time_results(const struct shell *sh, size_t argc, char *argv[]) {

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(encryption_measurements) / sizeof(encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(decryption_measurements) / sizeof(decryption_measurements[0]);

    double sum_KSGEN_ENCRYPT = 0;
    double sum_ENCRYPT = 0;
    double sum_KSGEN_DECRYPT = 0;
    double sum_DECRYPT = 0;

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && encryption_measurements[i].delta_ENDCRYPT != 0) {
        sum_KSGEN_ENCRYPT += encryption_measurements[i].delta_KSGEN * TIMER_TICK_NS;
        sum_ENCRYPT += encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS;
        i++;
    }

    while(j < length_values_DECRYPT && decryption_measurements[j].delta_ENDCRYPT != 0) {
        sum_KSGEN_DECRYPT += decryption_measurements[j].delta_KSGEN * TIMER_TICK_NS;
        sum_DECRYPT += decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS;
        j++;
    }

    double avg_KSGEN_ENCRYPT = (i > 0) ? sum_ENCRYPT / i : 0.0;
    double avg_ENCRYPT = (i > 0) ? sum_ENCRYPT / i : 0.0;
    double avg_DECRYPT = (j > 0) ? sum_DECRYPT / j : 0.0;
    double avg_KSGEN_DECRYPT = (j > 0) ? sum_DECRYPT / j : 0.0;

    shell_print(sh, "avg_KSGEN_ENCRYPT: %f ns", avg_KSGEN_ENCRYPT);
    shell_print(sh, "avg_ENCRYPT: %f ns", avg_ENCRYPT);
    shell_print(sh, "avg_KSGEN_DECRYPT: %f ns", avg_KSGEN_DECRYPT);
    shell_print(sh, "avg_DECRYPT: %f ns", avg_DECRYPT);

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(encryption_measurements) / sizeof(encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(decryption_measurements) / sizeof(decryption_measurements[0]);

    double sum_ENCRYPT = 0;
    double sum_DECRYPT = 0;

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && encryption_measurements[i].delta_ENDCRYPT != 0) {
        sum_ENCRYPT += encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS;
        i++;
    }

    while(j < length_values_DECRYPT && decryption_measurements[j].delta_ENDCRYPT != 0) {
        sum_DECRYPT += decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS;
        j++;
    }

    double avg_ENCRYPT = (i > 0) ? sum_ENCRYPT / i : 0.0;
    double avg_DECRYPT = (j > 0) ? sum_DECRYPT / j : 0.0;

    shell_print(sh, "avg_ENCRYPT: %f ns", avg_ENCRYPT);
    shell_print(sh, "avg_DECRYPT: %f ns", avg_DECRYPT);

    #endif

    // TODO: for encryption & ENDCRYPT anpassen

    // get the length of value array (values_ENDCRYPT)
    //int length_values_ENDCRYPT = sizeof(values_ENCRYPT) / sizeof(values_ENCRYPT[0]);

    // summarize the values to be able to calculate the average afterward
    //int sum_ENDCRYPT = 0;

    //for (int i = 0; i < length_values_ENDCRYPT; i++) {
    //    sum_ENDCRYPT += values_ENCRYPT[i];
    //}

    //avg_ENCRYPT = sum_ENDCRYPT / length_values_ENDCRYPT;

    //shell_print(sh, "avg_ENDCRYPT: %i", avg_ENCRYPT);

    return 0;
}

void reset_values() {
#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    encryption_measurement = (struct benchmark_measurement){0};
    decryption_measurement = (struct benchmark_measurement){0};
    encryption_measurement_count = 0;
    decryption_measurement_count = 0;
    memset((void *)encryption_measurements, 0, sizeof(encryption_measurements));
    memset((void *)decryption_measurements, 0, sizeof(decryption_measurements));
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    //BENCHMARK_SHARED_VARIABLES->enc_count = 0;
#elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    encryption_measurement = (struct benchmark_measurement){0};
    decryption_measurement = (struct benchmark_measurement){0};
    encryption_measurement_count = 0;
    decryption_measurement_count = 0;
    memset((void *)encryption_measurements, 0, sizeof(encryption_measurements));
    memset((void *)decryption_measurements, 0, sizeof(decryption_measurements));
#endif
}