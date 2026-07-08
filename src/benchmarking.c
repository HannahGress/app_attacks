#include <stdlib.h>
#include <controller/ll_sw/nordic/hal/nrf5/radio/radio_nrf5_ppi.h>
#include <zephyr/shell/shell.h>
#include <controller/ll_sw/nordic/hal/nrf5/radio/radio.h>

uint32_t avg_ENCRYPT;

static void benchmark_on() {
    is_benchmarking = true;

    hal_radio_ccm_nRF52840_ppi_config();

    hal_radio_nrf_ppi_channels_enable( BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));
}

static void benchmark_off() {
    is_benchmarking = false;
    hal_radio_nrf_ppi_channels_disable( BIT(HAL_CRYPT_START_TIME_ENCRYPT_PPI) | BIT(HAL_CRYPT_START_TIME_DECRYPT_PPI) | BIT(HAL_CRYPT_END_TIME_ENDCRYPT_PPI));
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

    // TODO: for encryption & ENDCRYPT anpassen

    // get the length of value array (values_ENDCRYPT)
    int length_values_ENDCRYPT = sizeof(values_ENCRYPT) / sizeof(values_ENCRYPT[0]);

    // summarize the values to be able to calculate the average afterward
    int sum_ENDCRYPT = 0;

    for (int i = 0; i < length_values_ENDCRYPT; i++) {
        sum_ENDCRYPT += values_ENCRYPT[i];
    }

    avg_ENCRYPT = sum_ENDCRYPT / length_values_ENDCRYPT;

    shell_print(sh, "avg_ENDCRYPT: %i", avg_ENCRYPT);

    return 0;

}