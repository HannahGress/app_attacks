#pragma once

#include <stdbool.h>

struct bt_conn;  // forward declaration (sufficient for pointer)
extern struct bt_conn *default_conn;
extern const struct shell *shell;
int w_advertising_start(void);
int ble_init(void);
void idle_test_run(void);  // src/idle_test.c, CONFIG_BENCHMARK_IDLE_TEST
int send_packets(int n, int payload_size, bool quiet);