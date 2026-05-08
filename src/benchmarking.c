#include <stdlib.h>
#include <controller/ll_sw/nordic/hal/nrf5/radio/radio_nrf5_ppi.h>
#include <zephyr/shell/shell.h>

uint32_t sum_delta;

static void cpu_tick_count_on(const char* no_dle, const char* enc_decr) {
    // depending on the usage of DLE or not or whether we want to benchmark
    // encryption or decryption, we register different PPI channels
    if (!strcmp(no_dle, "no_dle")) {
        hal_radio_ccm_en_de_crypt_start_time_no_dle_ppi_config();
    } else {
        if (!strcmp(enc_decr, "enc")) {
            hal_radio_ccm_encrypt_start_time_dle_ppi_config();
        } else {
            hal_radio_ccm_decrypt_start_time_dle_ppi_config();
        }
    }
    // capture of end times (KSGEN and ENCR/DECR) are the same for all measurements, therefor they are registered "globally"
    hal_radio_ccm_en_de_crypt_end_time_capture_ppi_config();
    hal_radio_nrf_ppi_channels_enable(BIT(HAL_EVENT_TIMER_CCM_END_KSGEN_CC_OFFSET) | BIT(HAL_EVENT_TIMER_CCM_START_ENDCRYPT_CC_OFFSET) | BIT(HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET));
}

static void cpu_tick_count_off() {
    hal_radio_nrf_ppi_channels_disable(BIT(HAL_EVENT_TIMER_CCM_END_KSGEN_CC_OFFSET) | BIT(HAL_EVENT_TIMER_CCM_START_ENDCRYPT_CC_OFFSET) | BIT(HAL_EVENT_TIMER_CCM_END_ENDCRYPT_CC_OFFSET));
}

static void benchmark_on(const char* no_dle, const char* enc_decr) {
    //is_benchmarking = true;
    sum_delta = 0;
    cpu_tick_count_on(no_dle, enc_decr);
}

static void benchmark_off() {
    //is_benchmarking = false;
    cpu_tick_count_off();
}

int cmd_benchmark(const struct shell *sh, size_t argc, char *argv[])
{
    // no_dle: the (not) use of DLE specifies when ENC/DECR measurement starts
    const char *no_dle = NULL;
    // some HW interrupts in ENC/DECR are different, therefor we need to specify
    // what we want to benchmark
    const char *enc_decr = NULL;
    // we can turn on and off the benchmarking
    const char *on_off = NULL;

    // We need all arguments for the benchmarking
    if (argc < 4) {
        shell_error(sh, "Wrong number of arguments.");
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }

    // extract each argument

    no_dle = argv[1];
    if (*no_dle == '\0') {
        shell_error(sh, "Argument must not be empty.");
        return -EINVAL;
    }
    if (strcmp(no_dle, "no_dle") != 0 && strcmp(no_dle, "dle") != 0) {
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }


    enc_decr = argv[2];
    if (*enc_decr == '\0') {
        shell_error(sh, "Argument must not be empty.");
        return -EINVAL;
    }
    if (strcmp(enc_decr, "enc") != 0 && strcmp(enc_decr, "decr") != 0) {
        shell_help(sh);
        return SHELL_CMD_HELP_PRINTED;
    }


    on_off = argv[3];
    if (*on_off == '\0') {
        shell_error(sh, "Argument must not be empty.");
        return -EINVAL;
    }
    if (!strcmp(on_off, "on")) {
        benchmark_on(no_dle, enc_decr);
    } else if (!strcmp(on_off, "off")) {
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