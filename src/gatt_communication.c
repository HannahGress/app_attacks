#include "zephyr/bluetooth/gatt.h"
#include "zephyr/random/random.h"
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <main.h>
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nRF52_54_ppi_dppi.h>
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <shared_variables.h>
#include <nRF52_54_ppi_dppi.h>
#endif

volatile bool notification_sent = true;
bool notify_enabled;
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

/*
 * This function is called when the data has been transmitted over the air.
 * (https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/api/gatt.html)
 */

// zepyhr calls this after sending
// it takes the last entry, calculates ticks in nanoseconds and returns it to shell
static void notify_complete_cb(struct bt_conn *conn, void *user_data) {

    // This runs in the Bluetooth TX context and MUST NOT block, otherwise it
    // stalls the whole BT stack (a blocking K_FOREVER is what froze send_data
    // after one packet). Skip the timing block (non-blocking) unless a
    // measurement is really ready!

    // get payload size from command line rather than a pointer to a stack variable
    // this way we can report which packet this is for
    int payload_size = POINTER_TO_INT(user_data);

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    if (encryption_measurement_count == 0 ||
        k_sem_take(&encryption_measurement_sem, K_NO_WAIT) != 0) {
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    // no semaphore across the cores, the net core only increments the shared count
    if (NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurement_count == 0) {
    #endif
        shell_print(shell, "packet sent (payload_size: %d bytes)", payload_size);
        notification_sent = true;
        return;
    }

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    int index = encryption_measurement_count-1;
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    int index = NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurement_count-1;
    #endif

    shell_print(shell, "payload_size: %d bytes", payload_size);

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    /*
     * KSGEN
     */

    shell_print(shell, "t_start_KSGEN: %f ns", encryption_measurements[index].t_start_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "t_end_KSGEN: %f ns", encryption_measurements[index].t_end_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "delta_KSGEN: %f ns", encryption_measurements[index].delta_KSGEN * TIMER_TICK_NS);


    /*
     * ENCRYPT
     */

    shell_print(shell, "t_start_ENCRYPT: %f ns", encryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_ENCRYPT: %f ns", encryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_ENCRYPT: %f ns", encryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);


    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    /*
     * KSGEN
     */

    shell_print(shell, "t_start_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].t_start_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "t_end_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].t_end_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "delta_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].delta_KSGEN * TIMER_TICK_NS);

    /*
     * ENCRYPT
     */

    shell_print(shell, "t_start_ENCRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_ENCRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_ENCRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    /*
     * ENCRYPT
     */
    shell_print(shell, "t_start_ENCRYPT: %f ns", encryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_ENCRYPT: %f ns", encryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_ENCRYPT: %f ns", encryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);
    #endif

    notification_sent = true;
}

int send_notification(struct bt_conn *conn, const struct bt_gatt_attr *attr, int payload_size)
{
    // if client has not subscribed yet
    if (!notify_enabled) {
        shell_error(shell, "Notifications not enabled");
        return -EINVAL;
    }

    if (payload_size <= 0 || payload_size > MAX_PAYLOAD_SIZE) {
        shell_error(shell, "Invalid payload size: %d", payload_size);
        return -EINVAL;
    }

    // create payload array
    uint8_t payload[payload_size];

    // fill with random bytes
    sys_rand_get(payload, payload_size);

    struct bt_gatt_notify_params params = {
        .attr = attr,
        .data = payload,
        .len = payload_size,
        .user_data = INT_TO_POINTER(payload_size),
        .func = notify_complete_cb,
    };

    return bt_gatt_notify_cb(conn, &params);
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

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    int index = decryption_measurement_count-1;
    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
    int index = NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurement_count-1;
    #endif

    #if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

    /*
     * KSGEN
     */

    shell_print(shell, "t_start_KSGEN: %f ns", decryption_measurements[index].t_start_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "t_end_KSGEN: %f ns", decryption_measurements[index].t_end_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "delta_KSGEN: %f ns", decryption_measurements[index].delta_KSGEN * TIMER_TICK_NS);

    /*
     * DECRYPT
     */

    shell_print(shell, "t_start_DECRYPT: %f ns", decryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_DECRYPT: %f ns", decryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_DECRYPT: %f ns", decryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

    /*
    * KSGEN
    */

    shell_print(shell, "t_start_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].t_start_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "t_end_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].t_end_KSGEN * TIMER_TICK_NS);
    shell_print(shell, "delta_KSGEN: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].delta_KSGEN * TIMER_TICK_NS);

    /*
     * DECRYPT
     */

    shell_print(shell, "t_start_DECRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_DECRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_DECRYPT: %f ns", NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);

    #elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
    /*
     * DECRYPT
     */

    shell_print(shell, "t_start_DECRYPT: %f ns", decryption_measurements[index].t_start_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "t_end_DECRYPT: %f ns", decryption_measurements[index].t_end_ENDCRYPT * TIMER_TICK_NS);
    shell_print(shell, "delta_DECRYPT: %f ns", decryption_measurements[index].delta_ENDCRYPT * TIMER_TICK_NS);

    #endif

    shell_print(shell, "Notification received (%u bytes)\n", length);

    return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
                         struct bt_gatt_subscribe_params *params)
{
    if (err != 0) {
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
        shell_print(shell, "Discovery done");
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
        shell_print(shell, "Value handle: 0x%04x", characteristic->value_handle);
    }

    // change params for ccc discovery
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    params->start_handle = value_handle + 1;

    if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
        if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
            subscribe_params.ccc_handle = attr->handle;
            shell_print(shell, "CCC handle: 0x%04x", attr->handle);
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