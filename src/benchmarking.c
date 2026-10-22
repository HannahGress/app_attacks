#include <main.h>
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
#include <ipc_service.h>
#include <nRF52_54_ppi_dppi.h>
#endif

static void benchmark_on() {

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    hal_radio_ccm_nRF52840_ppi_config();

    /* enable the PPi channels */
    hal_radio_nrf_ppi_channels_enable(
        BIT(HAL_CRYPT_END_TIME_KSGEN_PPI) |
        BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));

    /* reset all values to not disturb the measurements */
    reset_values()

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

    send_signal_nRF5340_dppi_config();

    /* reset all values to not disturb the measurements */
    reset_values()

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

    hal_radio_ccm_nRF54L15_ppi_config();

    /* reset all values to not disturb the measurements */
    reset_values()

    #endif

    shell_print(shell, "Benchmarking successfully enabled.");
}

static void benchmark_off() {

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    hal_radio_nrf_ppi_channels_disable(
        BIT(HAL_CRYPT_END_TIME_KSGEN_PPI) |
        BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP )
    send_signal_nRF5340_dppi_disable();
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    hal_radio_ccm_nRF54L15_ppi_disable();
    #endif

    shell_print(shell, "Benchmarking successfully disabled.");
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

    double avg_KSGEN_ENCRYPT = (i > 0) ? sum_KSGEN_ENCRYPT / i : 0.0;
    double avg_ENCRYPT = (i > 0) ? sum_ENCRYPT / i : 0.0;
    double avg_KSGEN_DECRYPT = (j > 0) ? sum_KSGEN_DECRYPT / j : 0.0;
    double avg_DECRYPT = (j > 0) ? sum_DECRYPT / j : 0.0;

    if(avg_ENCRYPT != 0) {
        shell_print(sh, "avg_KSGEN_ENCRYPT: %f ns", avg_KSGEN_ENCRYPT);
        shell_print(sh, "avg_ENCRYPT: %f ns", avg_ENCRYPT);
    }
    else if(avg_DECRYPT != 0) {
        shell_print(sh, "avg_KSGEN_DECRYPT: %f ns", avg_KSGEN_DECRYPT);
        shell_print(sh, "avg_DECRYPT: %f ns", avg_DECRYPT);
    }
    else {
        shell_print(sh, "No results for key stream generation, encryption and decryption (yet). You must first run send_data <n> <payload> to create some.");
    }

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements) / sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements) / sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[0]);

    double sum_KSGEN_ENCRYPT = 0;
    double sum_ENCRYPT = 0;
    double sum_KSGEN_DECRYPT = 0;
    double sum_DECRYPT = 0;

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_ENDCRYPT != 0) {
        sum_KSGEN_ENCRYPT += NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_KSGEN * TIMER_TICK_NS;
        sum_ENCRYPT += NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS;
        i++;
    }

    while(j < length_values_DECRYPT && NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_ENDCRYPT != 0) {
        sum_KSGEN_DECRYPT += NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_KSGEN * TIMER_TICK_NS;
        sum_DECRYPT += NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS;
        j++;
    }

    double avg_KSGEN_ENCRYPT = (i > 0) ? sum_KSGEN_ENCRYPT / i : 0.0;
    double avg_ENCRYPT = (i > 0) ? sum_ENCRYPT / i : 0.0;
    double avg_KSGEN_DECRYPT = (j > 0) ? sum_KSGEN_DECRYPT / j : 0.0;
    double avg_DECRYPT = (j > 0) ? sum_DECRYPT / j : 0.0;

    if(avg_ENCRYPT != 0) {
        shell_print(sh, "avg_KSGEN_ENCRYPT: %f ns", avg_KSGEN_ENCRYPT);
        shell_print(sh, "avg_ENCRYPT: %f ns", avg_ENCRYPT);
    }
    else if(avg_DECRYPT != 0) {
        shell_print(sh, "avg_KSGEN_DECRYPT: %f ns", avg_KSGEN_DECRYPT);
        shell_print(sh, "avg_DECRYPT: %f ns", avg_DECRYPT);
    }
    else {
        shell_print(sh, "No results for key stream generation, encryption and decryption (yet). You must first run send_data <n> <payload> to create some.");
    }

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

    if(avg_ENCRYPT != 0) {
        shell_print(sh, "avg_ENCRYPT: %f ns", avg_ENCRYPT);
    }
    else if(avg_DECRYPT != 0) {
        shell_print(sh, "avg_DECRYPT: %f ns", avg_DECRYPT);
    }
    else {
        shell_print(sh, "No results for encryption and decryption (yet). You must first run send_data <n> <payload> to create some.");
    }

    #endif

    return 0;
}

int cmd_get_time_results_extended(const struct shell *sh, size_t argc, char *argv[]) {

#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(encryption_measurements) / sizeof(encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(decryption_measurements) / sizeof(decryption_measurements[0]);

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && encryption_measurements[i].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", i);
        shell_print(sh, "start_KSGEN %f: ns\n", encryption_measurements[i].t_start_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "end_KSGEN %f: ns\n", encryption_measurements[i].t_end_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "delta_KSGEN %f: ns\n", encryption_measurements[i].delta_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "start_ENCRYPT %f: ns\n", encryption_measurements[i].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_ENCRYPT %f: ns\n", encryption_measurements[i].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_ENCRYPT %f: ns\n", encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS);
        i ++;
    }

    while(j < length_values_DECRYPT && decryption_measurements[j].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", j);
        shell_print(sh, "start_KSGEN %f: ns\n", decryption_measurements[j].t_start_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "end_KSGEN %f: ns\n", decryption_measurements[j].t_end_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "delta_KSGEN %f: ns\n", decryption_measurements[j].delta_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "start_DECRYPT %f: ns\n", decryption_measurements[j].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_DECRYPT %f: ns\n", decryption_measurements[j].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_DECRYPT %f: ns\n", decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS);
        j++;
    }

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements) / sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements) / sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[0]);

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", i);
        shell_print(sh, "start_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].t_start_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "end_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].t_end_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "delta_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "start_ENCRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_ENCRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_ENCRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS);
        i ++;
    }

    while(j < length_values_DECRYPT && NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", j);
        shell_print(sh, "start_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].t_start_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "end_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].t_end_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "delta_KSGEN %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_KSGEN * TIMER_TICK_NS);
        shell_print(sh, "start_DECRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_DECRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_DECRYPT %f: ns\n", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS);
        j++;
    }

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

    /* get the length of value array encryption_measurements and decryption_measurements */
    int length_values_ENCRYPT = sizeof(encryption_measurements) / sizeof(encryption_measurements[0]);
    int length_values_DECRYPT = sizeof(decryption_measurements) / sizeof(decryption_measurements[0]);

    int i = 0;
    int j = 0;

    while(i < length_values_ENCRYPT && encryption_measurements[i].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", i);
        shell_print(sh, "start_ENCRYPT %f: ns\n", encryption_measurements[i].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_ENCRYPT %f: ns\n", encryption_measurements[i].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_ENCRYPT %f: ns\n", encryption_measurements[i].delta_ENDCRYPT * TIMER_TICK_NS);
        i ++;
    }

    while(j < length_values_DECRYPT && decryption_measurements[j].delta_ENDCRYPT != 0) {
        shell_print(sh, "Iteration %u: \n", j);
        shell_print(sh, "start_DECRYPT %f: ns\n", decryption_measurements[j].t_start_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "end_DECRYPT %f: ns\n", decryption_measurements[j].t_end_ENDCRYPT * TIMER_TICK_NS);
        shell_print(sh, "delta_DECRYPT %f: ns\n", decryption_measurements[j].delta_ENDCRYPT * TIMER_TICK_NS);
        j++;
    }

#endif

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
    NRF5340_SHARED_VARIABLES->encryption_measurement = (struct benchmark_measurement){0};
    NRF5340_SHARED_VARIABLES->decryption_measurement = (struct benchmark_measurement){0};
    NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurement_count = 0;
    NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurement_count = 0;
    memset((void *)NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements, 0, sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements));
    memset((void *)NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements, 0, sizeof(NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements));
#elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    encryption_measurement = (struct benchmark_measurement){0};
    decryption_measurement = (struct benchmark_measurement){0};
    encryption_measurement_count = 0;
    decryption_measurement_count = 0;
    memset((void *)encryption_measurements, 0, sizeof(encryption_measurements));
    memset((void *)decryption_measurements, 0, sizeof(decryption_measurements));
#endif
    shell_print(shell, "Values successfully reset.");
}