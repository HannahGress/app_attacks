#include "zephyr/bluetooth/gatt.h"
#include "zephyr/random/random.h"
#include <zephyr/shell/shell.h>
#include "../include/main.h"
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include "../include/nRF52_54_ppi_dppi.h"
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
#include "../common_nRF5340/shared_varibles.h"
#endif

// #if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF53X)
//#include <controller/ll_sw/nordic/hal/nrf5/radio/radio.h>
// #endif


static bool notify_enabled;
#define MAX_PAYLOAD_SIZE 244

/* own UUIDs */
static struct bt_uuid_128 svc_uuid =
    BT_UUID_INIT_128(
        0x12, 0x34, 0x56, 0x78,
        0x12, 0x34,
        0x56, 0x78,
        0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);

static struct bt_uuid_128 chrc_uuid =
    BT_UUID_INIT_128(
        0xf0, 0xde, 0xbc, 0x9a,
        0x78, 0x56,
        0x34, 0x12,
        0x78, 0x56,
        0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

/*
 * GATT Server
 */

static void ccc_notification_config_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);

}

/* service + characteristic */
BT_GATT_SERVICE_DEFINE(notification_srv,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),

    BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL),

    BT_GATT_CCC(ccc_notification_config_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int send_notification(struct bt_conn *conn, const struct bt_gatt_attr *attr, int payload_size)
{
    // if client has not subscribed yet
    if (!notify_enabled) {
        return -EINVAL;
    }

    if (payload_size <= 0 || payload_size > MAX_PAYLOAD_SIZE) {
        return -EINVAL;
    }

    // create payload array
    uint8_t payload[payload_size];

    // fill with random bytes
    sys_rand_get(payload, payload_size);

    return bt_gatt_notify(conn, attr, payload, payload_size);
}

/*
 * GATT Client
 */

static uint8_t notification_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) {
        shell_print(shell, "Unsubscribed\n");
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    shell_print(shell, "Notification received (%u bytes)\n", length);
    #if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
    shell_print(shell, "Result t_start_ENDCRYPT: %u", t_start_DECRYPT);
    shell_print(shell, "Result t_end_ENDCRYPT: %u", t_end_ENDCRYPT);
    shell_print(shell, "Result Difference: %u", delta_DECRYPT);
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
    shell_print(shell, "Result t_start_ENDCRYPT: %u", BENCHMARK_SHARED_VARIABLES->t_start_DECRYPT);
    shell_print(shell, "Result t_end_ENDCRYPT: %u", BENCHMARK_SHARED_VARIABLES->t_end_ENDCRYPT);
    shell_print(shell, "Result Difference: %u", BENCHMARK_SHARED_VARIABLES->delta_DECRYPT);
    #endif

    return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
                         struct bt_gatt_subscribe_params *params)
{
    if(err==1) {
        shell_error(shell, "Subscribe failed");
    } else {
        shell_print(shell, "Subscribed\n");
    }
}

struct bt_gatt_subscribe_params subscribe_params = {
    .notify = notification_cb,
    .subscribe = subscribe_cb,
    .value = BT_GATT_CCC_NOTIFY,
};

static uint8_t service_and_characteristics_discovery(struct bt_conn *conn,
                 const struct bt_gatt_attr *attr,
                 struct bt_gatt_discover_params *params)
{
    if (!attr) {
        shell_print(shell, "Discovery done\n");
        return BT_GATT_ITER_STOP;
    }

    uint16_t value_handle = 0;

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        const struct bt_gatt_service_val *service_attribute_value = attr->user_data;

        // change params for characteristic discovery
        params->uuid = NULL;
        params->start_handle = attr->handle + 1; // value at attr->handle is the service declaration; attr->handle + 1 is the first attribute inside the service
        params->end_handle = service_attribute_value->end_handle;
        params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

        bt_gatt_discover(conn, params);
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        const struct bt_gatt_chrc *characteristic = attr->user_data;
        value_handle = characteristic->value_handle;
        subscribe_params.value_handle = value_handle;
        shell_print(shell, "Value handle: 0x%04x\n", characteristic->value_handle);
    }

    // change params for ccc discovery
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    params->start_handle = value_handle + 1;

    if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
        if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
            subscribe_params.ccc_handle = attr->handle;
            shell_print(shell, "CCC handle: 0x%04x\n", attr->handle);
        }
    }

    return BT_GATT_ITER_CONTINUE;
}

struct bt_gatt_discover_params discover_params = {
    .uuid = &svc_uuid.uuid, // only the already declared Service will be discovered
    .func = service_and_characteristics_discovery,
    .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
    .type = BT_GATT_DISCOVER_PRIMARY,
};