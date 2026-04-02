#pragma once

extern bool notify_enabled;
extern struct bt_gatt_service_static notification_srv;
extern int send_notification(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, int payload_size);
extern struct bt_gatt_subscribe_params subscribe_params;
extern struct bt_gatt_discover_params discover_params;