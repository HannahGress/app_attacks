#include <zephyr/ipc/ipc_service.h>
#include <ipc_service.h>
#include <nRF5340_dppi.h>

//static struct ipc_ept benchmark_ept;
/*
static void benchmark_command_received(
    const void *data,
    size_t len,
    void *priv
)
{
    if (len != sizeof(uint8_t)) {
        return;
    }

    uint8_t command = *(const uint8_t *)data;

    __DMB();

    switch (command) {
        case BENCHMARK_CMD_DPPI_CONFIG:
            hal_radio_ccm_nRF5340_dppi_config();
        break;

        case BENCHMARK_CMD_DPPI_DISABLE:
            hal_radio_ccm_nRF5340_ppi_disable();
        break;

        default:
            break;
    }
}
*/