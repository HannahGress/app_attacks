#pragma once

#define BENCHMARK_ENDPOINT_NAME "benchmark_ipc"

enum benchmark_command {
    BENCHMARK_CMD_DPPI_CONFIG = 1,
    BENCHMARK_CMD_DPPI_DISABLE = 2,
};
