#include "zephyr/bluetooth/gatt.h"
#include "zephyr/random/random.h"

#define PAYLOAD_LEN 20
static uint8_t payload[PAYLOAD_LEN];
static bool notify_enabled;

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

    printk("Notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

/* service + characteristic */
BT_GATT_SERVICE_DEFINE(notification_srv,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid),

    BT_GATT_CHARACTERISTIC(&chrc_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, payload),

    BT_GATT_CCC(ccc_notification_config_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int send_notification(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
    // if client has not subscribed yet
    if (!notify_enabled) {
        return -EINVAL;
    }

    /* fill with random bytes */
    sys_rand_get(payload, PAYLOAD_LEN);

    return bt_gatt_notify(conn, attr, payload, PAYLOAD_LEN);
}

/*
 * GATT Client
 */

static uint8_t notification_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) {
        printk("Unsubscribed\n");
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    printk("Notification received (%u bytes)\n", length);
    return BT_GATT_ITER_CONTINUE;
}

static void subscribe_cb(struct bt_conn *conn, uint8_t err,
                         struct bt_gatt_subscribe_params *params)
{
    printk("subscribe_cb err=%u\n", err);
}

struct bt_gatt_subscribe_params subscribe_params = {
    .notify = notification_cb,
    .subscribe = subscribe_cb,
    .value = BT_GATT_CCC_NOTIFY,
    //.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE,
};

static uint8_t service_and_characteristics_discovery(struct bt_conn *conn,
                 const struct bt_gatt_attr *attr,
                 struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("Discovery done\n");
        return BT_GATT_ITER_STOP;
    }

    uint16_t value_handle;

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
        printk("decl_handle=0x%04x value_handle=0x%04x\n",
           attr->handle, characteristic->value_handle);
    }

    // change params for ccc discovery
    params->type = BT_GATT_DISCOVER_DESCRIPTOR;
    params->start_handle = value_handle + 1;

    if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
        if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
            subscribe_params.ccc_handle = attr->handle;
            printk("decl_handle=0x%04x",
            attr->handle);
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