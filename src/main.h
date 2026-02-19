#pragma once

struct bt_conn;  // forward declaration (sufficient for pointer)
extern struct bt_conn *default_conn;
extern const struct shell *shell;

int w_advertising_start(void);
