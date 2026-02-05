/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <autoconf.h>
#include <host/smp.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>


struct bt_conn *default_conn;
uint8_t selected_id = BT_ID_DEFAULT;
const struct shell *shell;
static bool is_connected = false;
static bool authenticated_pairing = false;

static const char *security_err_str(enum bt_security_err err)
{
	switch (err) {
		case BT_SECURITY_ERR_SUCCESS:
			return "Success";
		case BT_SECURITY_ERR_AUTH_FAIL:
			return "Authentication failure";
		case BT_SECURITY_ERR_PIN_OR_KEY_MISSING:
			return "PIN or key missing";
		case BT_SECURITY_ERR_OOB_NOT_AVAILABLE:
			return "OOB not available";
		case BT_SECURITY_ERR_AUTH_REQUIREMENT:
			return "Authentication requirements";
		case BT_SECURITY_ERR_PAIR_NOT_SUPPORTED:
			return "Pairing not supported";
		case BT_SECURITY_ERR_PAIR_NOT_ALLOWED:
			return "Pairing not allowed";
		case BT_SECURITY_ERR_INVALID_PARAM:
			return "Invalid parameters";
		case BT_SECURITY_ERR_UNSPECIFIED:
			return "Unspecified";
		default:
			return "Unknown";
	}
}

// as soon as the callback function is called it listens to adv events until stop_advertising() is called
static void scan_started(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];
	//int err;

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
		type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	/* connect only to devices in close proximity */
	if (rssi > -50) {
		bt_addr_le_to_str(addr, dev, sizeof(dev));

		shell_print(shell, "[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i",
		   dev, type, ad->len, rssi);
	}

	//err = bt_le_scan_stop();
	//if (err) {
	//	shell_print(shell,"Stop LE scan failed (err %d)", err);
	//}
}

static int scan_start(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_started);
	if (err) {
		shell_error(shell,"Scanning failed to start (err %d)", err);
		return err;
	} else {
		shell_print(shell,"Scanning successfully started");
	}

	return 0;
}

static int scan_stop(void)
{
	int err;

	err = bt_le_scan_stop();

	if (err) {
		shell_error(shell, "Stopping scanning failed (err %d)", err);
		return err;
	} else {
		shell_print(shell, "Scan successfully stopped");
	}

	return 0;
}

static int cmd_scan(const struct shell *sh, size_t argc, char *argv[])
{
	const char *action;

	if (argc != 2) {
		shell_error(sh, "Wrong number of arguments.");
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	action = argv[1];
	if (!strcmp(action, "start")) {
		return scan_start();
	} else if (!strcmp(action, "stop")) {
		return scan_stop();
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	return 0;
}

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	bt_addr_le_t addr;
	struct bt_conn *conn = NULL;
	uint32_t options = 0;

	err = bt_addr_le_from_str(argv[1], argv[2], &addr);
	if (err) {
		shell_error(sh, "Invalid peer address (err %d)", err);
		return err;
	}


	struct bt_conn_le_create_param *create_params =
		BT_CONN_LE_CREATE_PARAM(options,
					BT_GAP_SCAN_FAST_INTERVAL,
					BT_GAP_SCAN_FAST_INTERVAL);

	err = bt_conn_le_create(&addr, create_params, BT_LE_CONN_PARAM_DEFAULT,
				&conn);
	if (err) {
		shell_error(sh, "Connection failed (%d)", err);
		return -ENOEXEC;
	} else {
		shell_print(sh, "Connection pending");

		/* unref connection obj in advance as app user */
		bt_conn_unref(conn);
	}

	return 0;
}

static int cmd_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
	struct bt_conn *conn;
	int err;

	if (default_conn && argc < 3) {
		conn = bt_conn_ref(default_conn);
	} else {
		bt_addr_le_t addr;

		if (argc < 3) {
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}

		err = bt_addr_le_from_str(argv[1], argv[2], &addr);
		if (err) {
			shell_error(sh, "Invalid peer address (err %d)",
					err);
			return err;
		}

		conn = bt_conn_lookup_addr_le(selected_id, &addr);
	}

	if (!conn) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		shell_error(sh, "Disconnection failed (err %d)", err);
		return err;
	}

	bt_conn_unref(conn);

	return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info conn_info;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		shell_error(shell, "Failed to connect to %s (%u)\n", addr,
			   err);
		bt_conn_unref(default_conn);
		default_conn = NULL;
		return;
	}

	shell_print(shell,"Connected: %s", addr);

	int info_err = bt_conn_get_info(conn, &conn_info);
	if (info_err) {
		shell_error(shell, "Failed to get connection info (%d).\n", info_err);
		return;
	}

	if (default_conn != NULL) {
		bt_conn_unref(default_conn);
	}
	default_conn = bt_conn_ref(conn);

	is_connected = true;

	/*shell_print(shell,"bt_conn_set_security: %s", addr);
	info_err = bt_conn_set_security(conn, BT_SECURITY_L2);
	shell_print(shell,"bt_conn_set_security done: %s", addr);
	if (info_err != 0) {
		shell_error(shell,"Failed to set security (%d).\n", info_err);
	}*/

}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn_info conn_info;
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	err = bt_conn_get_info(conn, &conn_info);
	if (err) {
		shell_error(shell, "Failed to get connection info (%d).\n", err);
		return;
	}

	shell_print(shell, "%s: %s role %u, reason %u %s\n", __func__, addr, conn_info.role,
		   reason, bt_hci_err_to_str(reason));

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	shell_print(shell,"Disconnected (reason %u)\n", reason);

}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		shell_print(shell, "Security changed: %s level %u\n", addr, level);
	} else {
		shell_print(shell, "Security failed: %s level %u err %d\n", addr, level, err);
	}
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	if (!authenticated_pairing) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Passkey for %s: %06u\n", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	if (!authenticated_pairing) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Confirm passkey for %s: %d", addr, passkey);
}

static void auth_passkey_entry(struct bt_conn *conn)
{
	if (!authenticated_pairing) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Enter passkey for %s", addr);
}

static void auth_pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Confirm pairing for %s", addr);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Pairing cancelled: %s\n", addr);
}

enum bt_security_err pairing_accept(
	struct bt_conn *conn, const struct bt_conn_pairing_feat *const feat)
{
	shell_print(shell, "Remote pairing features: "
				   "IO: 0x%02x, OOB: %d, AUTH: 0x%02x, Key: %d, "
				   "Init Kdist: 0x%02x, Resp Kdist: 0x%02x",
				   feat->io_capability, feat->oob_data_flag,
				   feat->auth_req, feat->max_enc_key_size,
				   feat->init_key_dist, feat->resp_key_dist);

	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	switch(err) {
		case BT_SECURITY_ERR_AUTH_FAIL:
			shell_print(shell, "Pairing failed due to authentication reasons (0x03). Unauthenticated pairing (Just Works) with No Input No Output is not supported, "
							 "use Passkey Entry or Numeric Comparison");
			break;
		case BT_SECURITY_ERR_UNSPECIFIED:
			shell_print(shell, "Pairing failed du to unspecified reasons (0x08). Maybe try Passkey Entry or Numeric Comparison as association model.");
			break;
		default:
			shell_print(shell, "Pairing failed with %s reason: %s (%d)", addr,
			security_err_str(err), err);
	}
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Pairing complete: %s with %s", bonded ? "Bonded" : "Paired",
			addr);
}

static void bond_info(const struct bt_bond_info *info, void *user_data)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int *bond_count = user_data;

	bt_addr_le_to_str(&info->addr, addr, sizeof(addr));
	shell_print(shell, "Remote Identity: %s", addr);
	(*bond_count)++;
}

void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	shell_print(shell, "Bond deleted for %s, id %u", addr, id);
}

static int cmd_bonds(const struct shell *sh)
{
	int bond_count = 0;

	shell_print(sh, "Bonded devices:");
	bt_foreach_bond(selected_id, bond_info, &bond_count);
	shell_print(sh, "Total %d", bond_count);

	return 0;
}

static int cmd_pairing_delete(const struct shell *sh, size_t argc, char *argv[])
{
	bt_addr_le_t addr;
	int err;

	if (strcmp(argv[1], "all") == 0) {
		err = bt_unpair(selected_id, NULL);
		if (err) {
			shell_error(sh, "Failed to clear pairings (err %d)",
				  err);
			return err;
		} else {
			shell_print(sh, "Pairings successfully cleared");
		}

		return 0;
	}

	if (argc < 3) {
		shell_print(sh, "Both address and address type needed");
		return -ENOEXEC;
	} else {
		err = bt_addr_le_from_str(argv[1], argv[2], &addr);
	}

	if (err) {
		shell_print(sh, "Invalid address");
		return err;
	}

	err = bt_unpair(selected_id, &addr);
	if (err) {
		shell_error(sh, "Failed to clear pairing (err %d)", err);
	} else {
		shell_print(sh, "Pairing successfully cleared");
	}

	return err;
}

static int cmd_security(const struct shell *sh, int sec_level)
{
	int err;
	int sec;

	switch(sec_level) {
		case 3:
			sec = BT_SECURITY_L2;
		break;
		case 4:
			sec = BT_SECURITY_L4;
		break;
		default:
			sec = BT_SECURITY_L2;
	}

	struct bt_conn_info info;

	if (!default_conn || (bt_conn_get_info(default_conn, &info) < 0)) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	err = bt_conn_set_security(default_conn, sec);
	if (err) {
		shell_error(sh, "Setting security failed (err %d)", err);
	}

	return err;
}

static int cmd_pair(const struct shell *sh, size_t argc, char *argv[]){
	cmd_connect(sh, argc, argv);
	k_sleep(K_SECONDS(2));
	// check if security level is given. If not, set to 2 (default)
	// 1 No encryption and no authentication;
	// 2 Encryption and no authentication;
	// 3 Encryption and authentication;
	// 4 Bluetooth LE Secure Connection.

	int level;
	if (argc < 3) {
		level = 2;
		shell_print(sh, "argc < 3: Setting security level to 2 -> No Input No Output");
	} else {
		level = atoi(argv[3]);
		// if level has no valid value (2, 3 or 4), then set to default (2)
		if(level != 3 && level != 4) {
			level = 2;
			shell_print(sh, "level != 3 || level != 4: Setting security level to 2 -> No Input No Output");
		}
	}

	cmd_security(sh, level);
	return 0;
}

static int cmd_auth_cancel(const struct shell *shell,
			   size_t argc, char *argv[])
{
	struct bt_conn *conn;

	if (default_conn) {
		conn = default_conn;
	} else {
		conn = NULL;
	}

	if (!conn) {
		shell_print(shell, "Not connected");
		return -ENOEXEC;
	}

	bt_conn_auth_cancel(conn);

	return 0;
}

static int cmd_auth_passkey(const struct shell *shell,
				size_t argc, char *argv[])
{
	unsigned int passkey;
	int err;

	if (!default_conn) {
		shell_print(shell, "Not connected");
		return -ENOEXEC;
	}

	passkey = atoi(argv[1]);
	if (passkey > 999999) {
		shell_print(shell, "Passkey should be between 0-999999");
		return -EINVAL;
	}

	err = bt_conn_auth_passkey_entry(default_conn, passkey);
	if (err) {
		shell_error(shell, "Failed to set passkey (%d)", err);
		return err;
	}

	return 0;
}

static int cmd_auth_passkey_confirm(const struct shell *shell,
					size_t argc, char *argv[])
{
	if (!default_conn) {
		shell_print(shell, "Not connected");
		return -ENOEXEC;
	}

	bt_conn_auth_passkey_confirm(default_conn);
	return 0;
}

static int cmd_auth_pairing_confirm(const struct shell *shell,
					size_t argc, char *argv[])
{
	if (!default_conn) {
		shell_print(shell, "Not connected");
		return -ENOEXEC;
	}

	bt_conn_auth_pairing_confirm(default_conn);
	return 0;
}

static struct bt_conn_auth_cb auth_cb = {
	// for Nino
	.pairing_confirm = auth_pairing_confirm,
	.pairing_accept = pairing_accept,
	.cancel = auth_cancel,
	// for authenticated pairing
	.passkey_display = auth_passkey_display,
	.passkey_entry = auth_passkey_entry,
	.passkey_confirm = auth_passkey_confirm,
};

static struct bt_conn_cb connection_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

/*
static struct bt_conn_auth_cb conn_auth_callbacks = {
	.pairing_accept = pairing_accept,
	.pairing_confirm = auth_pairing_confirm,
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.passkey_entry = auth_passkey_entry,
	.cancel = auth_cancel,
};*/


static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_failed = pairing_failed,
	.pairing_complete = pairing_complete,
	.bond_deleted = bond_deleted,
};

static int cmd_init(const struct shell *sh)
{
	int err;
	shell = sh;

	err = bt_enable(NULL);
	if (err) {
		printf("Bluetooth init failed (err %d)\n", err);
		return -1;
	}
	printf("Bluetooth initialized\n");

	settings_load();
	printf("Settings loaded\n");

	default_conn = NULL;


	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		printf("Failed to register authorization info callbacks.\n");
		return -1;
	}
	printf("Bluetooth authentication info callbacks registered.\n");

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err) {
		printf("Failed to register nino pairing callbacks. (err %d)\n", err);
		return -1;
	}

	printf("Bluetooth authentication callbacks registered.\n");

	err = bt_conn_cb_register(&connection_callbacks);
	if (err) {
		printf("Failed to register connection callbacks. (err %d)\n", err);
		return -1;
	}
	printf("Bluetooth connection callbacks registered.\n");

	return 0;
}

static int cmd_authenticated_pairing(const struct shell *sh, size_t argc, char *argv[]) {

	if (argc != 2) {
		shell_error(sh, "Usage: authenticated_pairing <true/false>");
		return -EINVAL;
	}

	if (!strcmp(argv[1], "true")) {
		authenticated_pairing = true;
		shell_print(sh, "Authentication enabled");
	} else {
		authenticated_pairing = false;
		shell_print(sh, "Authentication disabled");
	}

	return 0;
}

static int cmd_knob(const struct shell *sh, size_t argc, char *argv[])
{
	if (argc != 2) {
		shell_error(sh, "Usage: knob <true/false> or knob <key_size>");
		return -EINVAL;
	}

	uint8_t key_size;

	if (!strcmp(argv[1], "true")) {
		key_size = 7;
	} else if (!strcmp(argv[1], "false")) {
		key_size = 16;
	} else {
		// Try to parse as a number
		char *endptr;
		long value = strtol(argv[1], &endptr, 10);

		// Check if conversion was successful and within valid range
		if (*endptr != '\0' || value < 7 || value > 16) {
			shell_error(sh, "Invalid input. Use 'true', 'false', or a number between 7-16");
			return -EINVAL;
		}

		key_size = (uint8_t)value;
	}

	bt_smp_set_enc_key_size(key_size);
	shell_print(sh, "LTK entropy set to %u bytes", key_size);

	return 0;
}

static int cmd_scda(const struct shell *sh, size_t argc, char *argv[])
{
	if (argc != 2) {
		shell_error(sh, "Usage: scda <true/false>");
		return -EINVAL;
	}

	bool downgrade = !strcmp(argv[1], "true");

	bt_smp_secure_connections_downgrade(downgrade);
	shell_print(sh, "Secure Connections Downgrade Attack set to: %s", downgrade ? "true" : "false");

	return 0;
}

static void cmd_automatic_testing(const struct shell *sh, size_t argc, char *argv[]) {
	if (argc != 2) {
		shell_error(sh, "Usage: automatic_testing <true/false>");
	} else {
		shell_print(sh, "Start with Nino");
		cmd_pair(sh, argc, argv);
	}
}

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;

}

static int cmd_default_handler(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

#define HELP_NONE "[none]"
#define HELP_ADDR_LE "<address: XX:XX:XX:XX:XX:XX> <type: (public|random)>"

SHELL_STATIC_SUBCMD_SET_CREATE(cmds,
	SHELL_CMD(init, NULL, HELP_NONE, cmd_init),
	SHELL_CMD_ARG(scan, NULL, "<value: start, stop>", cmd_scan, 2, 0),
	SHELL_CMD_ARG(connect, NULL, HELP_NONE, cmd_connect, 3, 0),
	SHELL_CMD_ARG(disconnect, NULL, HELP_NONE, cmd_disconnect, 3, 0),
	SHELL_CMD(security, NULL, "security level 2 for Nino attack", cmd_security),
	SHELL_CMD_ARG(pair, NULL, "["HELP_ADDR_LE"], optional security level (2-4)", cmd_pair, 3, 1),
	SHELL_CMD(bonds, NULL, HELP_NONE, cmd_bonds),
	SHELL_CMD_ARG(unpair, NULL, "[all] ["HELP_ADDR_LE"]", cmd_pairing_delete, 3, 0),
	SHELL_CMD_ARG(auth-cancel, NULL, HELP_NONE, cmd_auth_cancel, 1, 0),
	SHELL_CMD_ARG(auth-passkey, NULL, "<passkey>", cmd_auth_passkey, 2, 0),
	SHELL_CMD_ARG(auth-passkey-confirm, NULL, HELP_NONE,
			  cmd_auth_passkey_confirm, 1, 0),
	SHELL_CMD_ARG(auth-pairing-confirm, NULL, HELP_NONE,
			  cmd_auth_pairing_confirm, 1, 0),
	SHELL_CMD_ARG(authenticated_pairing, NULL, "true/false", cmd_authenticated_pairing, 2, 0),
	SHELL_CMD_ARG(knob, NULL, "<true/false> (reduce LTK entropy)", cmd_knob, 2, 0),
	SHELL_CMD_ARG(scda, NULL, "<true/false> (enable/disable Secure Connections Downgrade Attack)", cmd_scda, 2, 0),
	SHELL_CMD_ARG(automatic_testing, NULL, "automatic_testing ["HELP_ADDR_LE"]", cmd_automatic_testing, 3, 0));


SHELL_CMD_REGISTER(bleframework, &cmds, "Bluetooth shell commands", cmd_default_handler);

// big help for (authenticated) pairing method implementation: https://elixir.bootlin.com/zephyr/v2.6.1-rc2/source/subsys/bluetooth/shell/bt.c