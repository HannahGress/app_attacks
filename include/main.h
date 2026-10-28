#pragma once

#include <stdbool.h>

struct bt_conn;  // forward declaration (sufficient for pointer)
extern struct bt_conn *default_conn;
extern const struct shell *shell;
int w_advertising_start(void);
int ble_init(void);
int send_packets(int n, int payload_size, bool quiet);