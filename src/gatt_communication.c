#include "zephyr/bluetooth/gatt.h"
#include "zephyr/random/random.h"
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
 #include <zephyr/sys/byteorder.h>
#include <stdio.h>
#include <main.h>
#include <gatt_communication.h>
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nRF52_54_ppi_dppi.h>
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <shared_variables.h>
#include <nRF52_54_ppi_dppi.h>
#endif

volatile bool notification_sent = true;
volatile bool benchmark_quiet = false;
static volatile bool results_sent = true;
bool notify_enabled;
bool results_notify_enabled;
#define MAX_PAYLOAD_SIZE 244

/* Device A: data notifications received since the last block summary (RES_END) */
static uint16_t received_in_block;
static struct bt_gatt_subscribe_params results_subscribe_params;

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

/* results characteristic: the DUT sends its measurement arrays to Device A over it */
static struct bt_uuid_128 results_chrc_uuid =
    BT_UUID_INIT_128(
        0xf1, 0xde, 0xbc, 0x9a,
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

static void ccc_results_config_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    results_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* service + characteristics
 * attrs[2] is the value of the data characteristic (send_data)
 * attrs[5] is the value of the results characteristic
 * Each CCC directly follows its value, Device A relies on that (ccc_handle = value_handle + 1)
 */
BT_GATT_SERVICE_DEFINE(notification_srv,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),

    BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL),

    BT_GATT_CCC(ccc_notification_config_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(&results_chrc_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL),

    BT_GATT_CCC(ccc_results_config_changed,
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

    // no shell output during a measurement (DUT auto run); the values stay in the arrays
    if (benchmark_quiet) {
        #if defined(CONFIG_SOC_COMPATIBLE_NRF52X) || defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
        (void)k_sem_take(&encryption_measurement_sem, K_NO_WAIT);
        #endif
        notification_sent = true;
        return;
    }

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

static void results_complete_cb(struct bt_conn *conn, void *user_data)
{
    results_sent = true;
}

// sends one record over the results characteristic and waits (bounded) until it is on air
int send_results(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    if (!results_notify_enabled) {
        shell_error(shell, "Results notifications not enabled");
        return -EINVAL;
    }

    struct bt_gatt_notify_params params = {
        .attr = &notification_srv.attrs[5],
        .data = data,
        .len = len,
        .func = results_complete_cb,
    };

    results_sent = false;

    int err = bt_gatt_notify_cb(conn, &params);
    if (err) {
        results_sent = true;
        return err;
    }

    int waited_ms = 0;
    while (results_sent == false && waited_ms < 1000) {
        k_sleep(K_MSEC(1));
        waited_ms++;
    }

    return 0;
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

    received_in_block++;

    // autonomous DUT run (results characteristic subscribed): only count, no output per packet.
    // Printing ~7 lines per packet blocks A's BT RX thread, A stops acknowledging and B has to retransmit.
    if (results_subscribe_params.value_handle) {
        return BT_GATT_ITER_CONTINUE;
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

/*
 * Device A receives the DUT's measurement arrays and prints them as text lines,
 * serial_controller.py turns them into a CSV:
 *   RES_BEGIN,<payload_size>,<packets_sent>,<count>,<fields_per_record>,<timer_hz>
 *   RES,<payload_size>,<index>,<tick>,...
 *   RES_END,<payload_size>
 *   RES_BATCH_DONE
 */
static uint8_t results_fields_per_record;
static uint16_t block_sent, block_measured;  // from the DUT's RES_BEGIN, for the block summary

static uint8_t results_notification_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) {
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    const uint8_t *rec = data;

    if (length < 1) {
        return BT_GATT_ITER_CONTINUE;
    }

    switch (rec[0]) {
    case RESULTS_BEGIN:
        if (length < RESULTS_BEGIN_LEN) {
            break;
        }
        results_fields_per_record = rec[6];
        block_sent = sys_get_le16(&rec[2]);
        block_measured = sys_get_le16(&rec[4]);
        shell_print(shell, "RES_BEGIN,%u,%u,%u,%u,%u", rec[1], sys_get_le16(&rec[2]),
                    sys_get_le16(&rec[4]), rec[6], sys_get_le32(&rec[7]));
        break;

    case RESULTS_DATA: {
        if (length < RESULTS_DATA_HEADER_LEN || results_fields_per_record == 0) {
            break;
        }
        uint16_t first_index = sys_get_le16(&rec[2]);
        uint8_t n_records = rec[4];

        if (length < RESULTS_DATA_HEADER_LEN + n_records * results_fields_per_record * 4) {
            shell_error(shell, "RES_ERROR,truncated record");
            break;
        }

        const uint8_t *value = &rec[RESULTS_DATA_HEADER_LEN];
        for (int r = 0; r < n_records; r++) {
            char line[128];
            int pos = snprintf(line, sizeof(line), "RES,%u,%u", rec[1], first_index + r);

            for (int f = 0; f < results_fields_per_record; f++) {
                pos += snprintf(&line[pos], sizeof(line) - pos, ",%u", sys_get_le32(value));
                value += 4;
            }
            shell_print(shell, "%s", line);
        }
        break;
    }

    case RESULTS_END:
        shell_print(shell, "RES_END,%u", length > 1 ? rec[1] : 0);
        shell_print(shell, "Summary: %u x %u-byte packets received (B sent %u, measured %u)",
                    received_in_block, length > 1 ? rec[1] : 0, block_sent, block_measured);
        received_in_block = 0;
        break;

    case RESULTS_BATCH_DONE:
        shell_print(shell, "RES_BATCH_DONE");
        break;

    default:
        break;
    }

    return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
                         struct bt_gatt_subscribe_params *params)
{
    if (err != 0) {
        shell_error(shell, "Subscribe failed");
    } else if (results_subscribe_params.value_handle) {
        // the results subscription follows, it prints "Subscribed" when both are done
        shell_print(shell, "Data notifications subscribed");
    } else {
        shell_print(shell, "Subscribed\n");
    }
}

static void results_subscribe_cb(struct bt_conn *conn, uint8_t err,
                         struct bt_gatt_subscribe_params *params)
{
    if (err != 0) {
        shell_error(shell, "Subscribe failed (results)");
    } else {
        shell_print(shell, "Subscribed\n");
    }
}

struct bt_gatt_subscribe_params subscribe_params = {
    .notify = notification_cb,
    .subscribe = subscribe_cb,
    .value = BT_GATT_CCC_NOTIFY,
};

static struct bt_gatt_subscribe_params results_subscribe_params = {
    .notify = results_notification_cb,
    .subscribe = results_subscribe_cb,
    .value = BT_GATT_CCC_NOTIFY,
};

// subscribes to the data characteristic and, if the peer has one, to the results characteristic
int subscribe_all(struct bt_conn *conn)
{
    // a new run starts with an empty block count
    received_in_block = 0;

    int err = bt_gatt_subscribe(conn, &subscribe_params);
    if (err) {
        return err;
    }

    if (results_subscribe_params.value_handle) {
        err = bt_gatt_subscribe(conn, &results_subscribe_params);
    }

    return err;
}

static uint8_t service_and_characteristics_discovery(struct bt_conn *conn,
                 const struct bt_gatt_attr *attr,
                 struct bt_gatt_discover_params *params)
{
    if (!attr) {
        // printed only now, so the script does not subscribe before both characteristics are known
        if (subscribe_params.ccc_handle) {
            shell_print(shell, "CCC handle: 0x%04x", subscribe_params.ccc_handle);
        }
        if (results_subscribe_params.ccc_handle) {
            shell_print(shell, "Results CCC handle: 0x%04x", results_subscribe_params.ccc_handle);
        }
        shell_print(shell, "Discovery done");
        return BT_GATT_ITER_STOP;
    }

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

        // our service layout is fixed (BT_GATT_SERVICE_DEFINE above): each CCC directly follows its value
        if (!bt_uuid_cmp(characteristic->uuid, &chrc_uuid.uuid)) {
            subscribe_params.value_handle = characteristic->value_handle;
            subscribe_params.ccc_handle = characteristic->value_handle + 1;
            shell_print(shell, "Value handle: 0x%04x", characteristic->value_handle);
        } else if (!bt_uuid_cmp(characteristic->uuid, &results_chrc_uuid.uuid)) {
            results_subscribe_params.value_handle = characteristic->value_handle;
            results_subscribe_params.ccc_handle = characteristic->value_handle + 1;
            shell_print(shell, "Results value handle: 0x%04x", characteristic->value_handle);
        }
    }

    return BT_GATT_ITER_CONTINUE;
}

struct bt_gatt_discover_params discover_params = {
    .func = service_and_characteristics_discovery,
};

int start_discovery(struct bt_conn *conn)
{
    // discover_params is modified during the discovery, so start from scratch every time
    discover_params.uuid = &svc_uuid.uuid; // only the already declared Service will be discovered
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    subscribe_params.value_handle = 0;
    subscribe_params.ccc_handle = 0;
    results_subscribe_params.value_handle = 0;
    results_subscribe_params.ccc_handle = 0;

    return bt_gatt_discover(conn, &discover_params);
}