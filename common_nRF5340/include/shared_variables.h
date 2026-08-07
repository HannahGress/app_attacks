#pragma once

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET) || defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define SUM_ARRAY_MAX_SIZE 100U

struct benchmark_shared_variables {

    volatile uint32_t is_benchmarking;
    // variables to store the time for each time point in the encr/decr chain
    volatile uint32_t t_start_ENCRYPT;
    volatile uint32_t t_start_DECRYPT;
    volatile uint32_t t_end_ENDCRYPT;

    volatile uint32_t delta_ENCRYPT;
    volatile uint32_t delta_DECRYPT;

    volatile uint32_t enc_count;

    volatile uint32_t values_ENCRYPT[SUM_ARRAY_MAX_SIZE];
    volatile uint32_t values_DECRYPT[SUM_ARRAY_MAX_SIZE];
};

#define BENCHMARK_SHARED_MEMORY_NODE DT_NODELABEL(benchmark_shared_memory)

#define BENCHMARK_SHARED_VARIABLES \
((volatile struct benchmark_shared_variables *) \
DT_REG_ADDR(BENCHMARK_SHARED_MEMORY_NODE))

BUILD_ASSERT(
    sizeof(struct benchmark_shared_variables) <=
    DT_REG_SIZE(BENCHMARK_SHARED_MEMORY_NODE),
    "Shared-memory region is too small"
);

#endif
