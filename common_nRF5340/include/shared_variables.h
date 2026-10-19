#pragma once

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET) || defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

/* we allow a maximum of 100 packets transmitted per send_data() method */
#define SUM_ARRAY_MAX_SIZE 100

struct benchmark_measurement {
    uint32_t t_start_KSGEN;
    uint32_t t_end_KSGEN;
    uint32_t delta_KSGEN;

    uint32_t t_start_ENDCRYPT;
    uint32_t t_end_ENDCRYPT;
    uint32_t delta_ENDCRYPT;
};

struct benchmark_results {
    volatile struct benchmark_measurement encryption_measurements[SUM_ARRAY_MAX_SIZE];
    volatile struct benchmark_measurement decryption_measurements[SUM_ARRAY_MAX_SIZE];
    volatile uint32_t encryption_measurement_count;
    volatile uint32_t decryption_measurement_count;
};

struct benchmark_control {
    /* We must declare the payload as an external variable, because we later filter for packages with this payload + 7 Bytes header data (+ other data) */
    int32_t payload_size;
    uint32_t is_benchmarking; // TODO bool?
};

struct benchmark_shared_variables {

    /* Semaphore to ensure that the readout on the shell is consistent */
    // struct k_sem encryption_measurement_sem;

    struct benchmark_control benchmark_control;
    struct benchmark_results benchmark_results;

    volatile struct benchmark_measurement encryption_measurement;
    volatile struct benchmark_measurement decryption_measurement;

};


#define BENCHMARK_SHARED_MEMORY_NODE DT_NODELABEL(benchmark_shm)
/*

BUILD_ASSERT(
    sizeof(struct benchmark_shared_variables) <=
    DT_REG_SIZE(BENCHMARK_SHARED_MEMORY_NODE),
    "Shared-memory region is too small"
);
*/

#define NRF5340_SHARED_VARIABLES \
((volatile struct benchmark_shared_variables *) \
DT_REG_ADDR(BENCHMARK_SHARED_MEMORY_NODE))


#endif
