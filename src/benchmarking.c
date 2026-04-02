//#include <controller/ll_sw/nordic/hal/nrf5/radio/radio.h>
#include <controller/ll_sw/nordic/hal/nrf5/radio/radio_nrf5_ppi.h>
#include <zephyr/shell/shell.h>
//#include "main.h"

uint32_t sum_delta;

static void cpu_tick_count_on() {
    hal_radio_ccm_endcrypt_time_capture_ppi_config();
    hal_radio_nrf_ppi_channels_enable(BIT(HAL_CRYPT_END_TIME_CAPTURE_PPI) | BIT(HAL_CRYPT_START_TIME_CAPTURE_PPI));
}

static void cpu_tick_count_off() {
    hal_radio_nrf_ppi_channels_disable(BIT(HAL_CRYPT_END_TIME_CAPTURE_PPI | BIT(HAL_CRYPT_START_TIME_CAPTURE_PPI)));
}

static void benchmark_on() {
    //is_benchmarking = true;
    sum_delta = 0;
    cpu_tick_count_on();
}

static void benchmark_off() {
    //is_benchmarking = false;
    cpu_tick_count_off();
}

int cmd_benchmark(const struct shell *sh, size_t argc, char *argv[])
{
    const char *action = argv[1];

    if (argc != 2) {
        shell_error(sh, "Wrong number of arguments.");
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }

    if (!strcmp(action, "on")) {
        benchmark_on();
    } else if (!strcmp(action, "off")) {
        benchmark_off();
    } else {
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }
    return 0;
}

int cmd_reset_sum_delta()
{
    sum_delta = 0;
    return 0;
}

int cmd_get_delta_encryption_time(const struct shell *sh, size_t argc, char *argv[]) {
    //shell_print(sh, "Start: %u, End: %u", t_start, t_end);
    //shell_print(sh, "Result Locally: %u",  t_end - t_start);
    //shell_print(sh, "Result Cumulated: %u",  sum_delta += delta_encryption_time);
    //shell_print(sh, "Result radio_is_done(): %u", delta_encryption_time);
    //shell_print(sh, "Enc Count: %u", enc_count);
}