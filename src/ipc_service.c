#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include "zephyr/ipc/ipc_service.h"
#include <../common_nRF5340/include/ipc_service.h>

static struct ipc_ept benchmark_ept;

static K_SEM_DEFINE(benchmark_bound_sem, 0, 1);

static void benchmark_ept_bound(void *priv)
{
    k_sem_give(&benchmark_bound_sem);
}

static struct ipc_ept_cfg benchmark_ept_cfg = {
    .name = BENCHMARK_ENDPOINT_NAME,
    .cb = {
        .bound = benchmark_ept_bound,
    },
};


int benchmark_ipc_init(void)
{
    const struct device *ipc_instance =
        DEVICE_DT_GET(DT_NODELABEL(ipc0));

    int err;

    err = ipc_service_open_instance(ipc_instance);

    if (err < 0 && err != -EALREADY) {
        return err;
    }

    err = ipc_service_register_endpoint(
        ipc_instance,
        &benchmark_ept,
        &benchmark_ept_cfg
    );

    if (err < 0) {
        return err;
    }

    err = k_sem_take(&benchmark_bound_sem, K_SECONDS(2));

    if (err) {
        return -ETIMEDOUT;
    }

    return 0;
}

int send_signal_nRF5340_dppi_config(void)
{
    uint8_t command = BENCHMARK_CMD_DPPI_CONFIG;

    return ipc_service_send(
        &benchmark_ept,
        &command,
        sizeof(command)
    );
}

int send_signal_nRF5340_dppi_disable(void)
{
    uint8_t command = BENCHMARK_CMD_DPPI_DISABLE;

    return ipc_service_send(
        &benchmark_ept,
        &command,
        sizeof(command)
    );
}
#endif