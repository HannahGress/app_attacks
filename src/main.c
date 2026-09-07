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
#include <common/bt_str.h>
#include <host/conn_internal.h>
#include <host/keys.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <host/smp.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <ifa.h>
#include <gatt_communication.h>
#include <benchmarking.h>
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include "../include/nRF52_54_ppi_dppi.h"
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <ipc_service.h>
#include <shared_variables.h>
#endif
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#include <nrf54l15_global.h>
#endif


struct bt_conn *default_conn;
uint8_t selected_id = BT_ID_DEFAULT;
const struct shell *shell;
static bool is_connected = false;
#define NAME_LEN 30

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

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
			  BT_UUID_16_ENCODE(BT_UUID_CSC_VAL),
			  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL))
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int advertising_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		shell_error(shell, "Advertising failed to start, reason: %d (%s)\n", err, bt_hci_err_to_str(err));
		return err;
	}
	return 0;
}

// wrapper for calling from outside
int w_advertising_start(void)
{
	advertising_start();
	return 0;
}

static int advertising_stop(void)
{
	int err = bt_le_adv_stop();
	if (err) {
		shell_error(shell, "Advertising failed to stop, reason: %d (%s)\n", err, bt_hci_err_to_str(err));
		return err;
	}

	shell_print(shell, "Advertising successfully stoped\n");

	return 0;
}

static int cmd_advertise(const struct shell *sh, size_t argc, char *argv[])
{
	const char *action = argv[1];

	if (argc != 2) {
		shell_error(sh, "Wrong number of arguments.");
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (!strcmp(action, "start")) {
		return advertising_start();
	} else if (!strcmp(action, "stop")) {
		return advertising_stop();
	} else {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
		case BT_DATA_NAME_SHORTENED:
		case BT_DATA_NAME_COMPLETE:
			len = MIN(data->data_len, NAME_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
		default:
			return true;
	}
}

// as soon as the callback function is called it listens to adv events until stop_advertising() is called
static void scan_started(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];
	char name[NAME_LEN];

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
		type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
		type != BT_GAP_ADV_TYPE_SCAN_RSP) {
		return;
		}

	/* connect only to devices in close proximity */
	if (rssi > -50) {
		bt_addr_le_to_str(addr, dev, sizeof(dev));
		(void)memset(name, 0, sizeof(name));

		uint16_t ad_len = ad->len;

		bt_data_parse(ad, data_cb, name);

		shell_print(shell,
		"[DEVICE]: %s, Name: %s, AD evt type %u, AD data len %u, RSSI %i",
		dev, name[0] ? name : "<none>", type, ad_len, rssi);
	}
}

static int scan_start(void)
{
	struct bt_le_scan_param param = {
		.type       = BT_LE_SCAN_TYPE_ACTIVE,
		.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
		.timeout    = 0, };

	int err = bt_le_scan_start(&param, scan_started);
	if (err) {
		shell_error(shell,"Scanning failed to start, reason: %d (%s)", err, bt_hci_err_to_str(err));
		return err;
	} else {
		shell_print(shell,"Scanning successfully started");
	}

	return 0;
}

static int scan_stop(void)
{
	int err = bt_le_scan_stop();

	if (err) {
		shell_error(shell, "Stopping scanning failed, reason: %d (%s)", err, bt_hci_err_to_str(err));
		return err;
	} else {
		shell_print(shell, "Scan successfully stopped");
	}

	return 0;
}

static int cmd_scan(const struct shell *sh, size_t argc, char *argv[])
{
	const char *action = argv[1];

	if (argc != 2) {
		shell_error(sh, "Wrong number of arguments.");
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (!strcmp(action, "start")) {
		return scan_start();
	}

	if (!strcmp(action, "stop")) {
		return scan_stop();
	}

	shell_help(sh);
	return SHELL_CMD_HELP_PRINTED;
}



static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_exchange_params *params)
{
	printk("%s: MTU exchange %s (%u)\n", __func__,
		   err == 0U ? "successful" : "failed",
		   bt_gatt_get_mtu(conn));
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
	.func = mtu_exchange_cb
};

static int mtu_exchange(struct bt_conn *conn)
{
	printk("%s: Current MTU = %u\n", __func__, bt_gatt_get_mtu(conn));

	printk("%s: Exchange MTU...\n", __func__);
	int err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (err) {
		printk("%s: MTU exchange failed (err %d)", __func__, err);
	}

	return err;
}

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	bt_addr_le_t addr;
	struct bt_conn *conn = NULL;
	uint32_t options = 0;

	err = bt_addr_le_from_str(argv[1], argv[2], &addr);
	if (err) {
		shell_error(sh, "Invalid peer address, reason: %d (%s)", err, bt_hci_err_to_str(err));
		return err;
	}

	struct bt_conn_le_create_param *create_params =
		BT_CONN_LE_CREATE_PARAM(options,
					BT_GAP_SCAN_FAST_INTERVAL,
					BT_GAP_SCAN_FAST_INTERVAL);

	err = bt_conn_le_create(&addr, create_params, BT_LE_CONN_PARAM_DEFAULT, &conn);

	if (err) {
		shell_error(sh, "Connection failed (%s)", bt_hci_err_to_str(err));
		return -ENOEXEC;
	}

	shell_print(sh, "Connection pending");

	// unref connection obj in advance as app user
	bt_conn_unref(conn);

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
			shell_error(sh, "Invalid peer address, reason: %d (%s)", err,
					bt_hci_err_to_str(err));
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
		shell_error(sh, "Disconnection failed, reason: %d (%s)", err, bt_hci_err_to_str(err));
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
		shell_error(shell, "connected(): Failed to connect to %s, reason: %d (%s)\n", addr, err,
			   bt_hci_err_to_str(err));
		bt_conn_unref(default_conn);
		default_conn = NULL;
		conn = NULL;
		return;
	}

	shell_print(shell,"Connected: %s", addr);

	int info_err = bt_conn_get_info(conn, &conn_info);
	if (info_err) {
		shell_error(shell, "Failed to get connection info, reason: %d (%s).\n", info_err, bt_hci_err_to_str(info_err));
		return;
	}

	if (default_conn) {
		bt_conn_unref(default_conn);
	}
	default_conn = bt_conn_ref(conn);
	is_connected = true;
	k_sem_give(&conn_sem); 	// Signal that connection is complete

	// send ATT MTU request/response depending on role (Central/Peripheral)
	if(conn_info.role == BT_CONN_ROLE_CENTRAL) {
		(void)mtu_exchange(conn);
	} else if (conn_info.role == BT_CONN_ROLE_PERIPHERAL) {
		printk("Connected, current ATT MTU: %u\n", bt_gatt_get_mtu(conn));
	}

	advertising_stop();

}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn_info conn_info;
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	err = bt_conn_get_info(conn, &conn_info);
	if (err) {
		shell_error(shell, "Failed to get connection info, reason: %d (%s).\n", err, bt_hci_err_to_str(err));
		return;
	}

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	k_sem_give(&disconn_sem); // Signal that disconnection is complete

	shell_print(shell,"Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		shell_print(shell, "Security with %s changed to level %u", addr, level);
	} else {
		shell_error(shell, "Security failed: %s level %u reason %d (%s)", addr, level, err, bt_hci_err_to_str(err));
	}
	k_sleep(K_MSEC(500));
	k_sem_give(&bond_sem); // Signal that bonding is complete
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

	shell_print(shell, "Pairing failed with %s, reason: %d (%s)", addr, err,
			security_err_str(err));
}

void show_bt_keys_info(struct bt_keys *keys, void *data)
{
	uint8_t ltk[16];

	if (keys->keys & BT_KEYS_LTK_P256) {
		sys_memcpy_swap(ltk, keys->ltk.val, keys->enc_size);
		shell_print(shell, "SC LTK: 0x%s", bt_hex(ltk, keys->enc_size));
	}

#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
	if (keys->keys & BT_KEYS_PERIPH_LTK) {
		sys_memcpy_swap(ltk, keys->periph_ltk.val, keys->enc_size);
		shell_print(shell, "Legacy LTK: 0x%s (peripheral)", bt_hex(ltk, keys->enc_size));
	}
#endif /* !CONFIG_BT_SMP_SC_PAIR_ONLY */

	if (keys->keys & BT_KEYS_LTK) {
		sys_memcpy_swap(ltk, keys->ltk.val, keys->enc_size);
		shell_print(shell, "Legacy LTK: 0x%s (central)", bt_hex(ltk, keys->enc_size));
	}
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info conn_info;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	shell_print(shell, "Pairing complete: %s with %s", bonded ? "Bonded" : "Paired",
			addr);

	int err = bt_conn_get_info(conn, &conn_info);
	if (err) {
		shell_error(shell, "Failed to get connection info, reason: %d (%s).\n", err, bt_hci_err_to_str(err));
	}

	// print it so you can verify if it's an SC/Legacy or 7/16 Bytes key
	show_bt_keys_info(conn->le.keys, NULL);
}

static void bond_info(const struct bt_bond_info *info, void *user_data)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int *bond_count = user_data;

	bt_addr_le_to_str(&info->addr, addr, sizeof(addr));
	shell_print(shell, "Remote Identity: %s", addr);
	(*bond_count)++;
}

static int cmd_bonds(const struct shell *sh)
{
	int bond_count = 0;

	shell_print(sh, "Bonded devices:");
	bt_foreach_bond(selected_id, bond_info, &bond_count);
	shell_print(sh, "Total %d", bond_count);

	return 0;
}

void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	shell_print(shell, "Bond deleted for %s, id %u", addr, id);
}

static int cmd_pairing_delete(const struct shell *sh, size_t argc, char *argv[])
{
	bt_addr_le_t addr;
	int err;

	if (strcmp(argv[1], "all") == 0) {
		err = bt_unpair(selected_id, NULL);
		if (err) {
			shell_error(sh, "Failed to clear pairings, reason: %d %s", err,
				  bt_hci_err_to_str(err));
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
		shell_error(sh, "Failed to clear pairing, reason: %d (%s)", err, bt_hci_err_to_str(err));
	} else {
		shell_print(sh, "Pairing successfully cleared");
	}

	return err;
}

static int cmd_security(const struct shell *sh)
{
	int err;
	int sec = BT_SECURITY_L2;
	struct bt_conn_info info;

	if (!default_conn || (bt_conn_get_info(default_conn, &info) < 0)) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	err = bt_conn_set_security(default_conn, sec);
	if (err) {
		shell_error(sh, "Setting security failed, reason: %d (%s)", err, bt_hci_err_to_str(err));
	}

	return err;
}

static int cmd_pair(const struct shell *sh, size_t argc, char *argv[]){
	cmd_connect(sh, argc, argv);
	k_sleep(K_SECONDS(2));
	cmd_security(sh);
	return 0;
}

struct bt_conn_cb connection_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.pairing_accept = pairing_accept,
};


static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_failed = pairing_failed,
	.pairing_complete = pairing_complete,
	.bond_deleted = bond_deleted,
};


static void init_timer() {
	#if defined(CONFIG_SOC_COMPATIBLE_NRF52X)

	/* TIMER 3*/
	NRF_TIMER3->TASKS_STOP = 1;
	NRF_TIMER3->MODE = TIMER_MODE_MODE_Timer;
	NRF_TIMER3->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
	NRF_TIMER3->PRESCALER = 1;   // 1 / 16 MHz = 62.5 ns pro Tick

	NRF_TIMER3->TASKS_CLEAR = 1;
	NRF_TIMER3->TASKS_START = 1;

	#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP) || defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUNET)
	NRF_TIMER1->TASKS_STOP = 1;
	NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;
	NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
	NRF_TIMER1->PRESCALER = 4;   // 1 MHz → 1 tick = 1 µs

	NRF_TIMER1->TASKS_CLEAR = 1;
	NRF_TIMER1->TASKS_START = 1;

	#elif defined(CONFIG_SOC_COMPATIBLE_NRF54LX)

	NRF_TIMER00->TASKS_STOP = 1;

	NRF_TIMER00->MODE =
		TIMER_MODE_MODE_Timer;

	NRF_TIMER00->BITMODE =
		TIMER_BITMODE_BITMODE_32Bit;

	NRF_TIMER00->PRESCALER = 0;  // 128 MHz / 7,8125 ns per tick

	NRF_TIMER00->TASKS_CLEAR = 1;
	NRF_TIMER00->TASKS_START = 1;
	#endif
}

/**
* Define IPC for the two cores
*/

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
/*
static struct ipc_ept benchmark_ept;

static K_SEM_DEFINE(benchmark_bound_sem, 0, 1);

static void benchmark_ept_bound(void *priv)
{
	k_sem_give(&benchmark_bound_sem);
}

static struct ipc_ept_cfg benchmark_ept_cfg = {
	.name = BENCHMARK_ENDPOINT_NAME,
	.cb = {
		.bound = benchmark_ept_bound,
	},
};


int benchmark_ipc_init(void)
{
	const struct device *ipc_instance =
		DEVICE_DT_GET(DT_NODELABEL(ipc0));

	int err;

	err = ipc_service_open_instance(ipc_instance);

	if (err < 0 && err != -EALREADY) {
		return err;
	}

	err = ipc_service_register_endpoint(
		ipc_instance,
		&benchmark_ept,
		&benchmark_ept_cfg
	);

	if (err < 0) {
		return err;
	}

	err = k_sem_take(&benchmark_bound_sem, K_SECONDS(2));

	if (err) {
		return -ETIMEDOUT;
	}

	return 0;
}*/
#endif


static int cmd_init(const struct shell *sh)
{
	int err;
	shell = sh;

	err = bt_enable(NULL);
	if (err) {
		printf("Bluetooth init failed, reason: %d (%s)\n", err, strerror(-err));
		return -1;
	}
	printf("Bluetooth initialized\n");

	//#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
	//err = benchmark_ipc_init();
	//if (err) {
	//	printf("Benchmark IPC init failed: %d\n", err);
	//	return err;
	//}
	//#endif

	err = settings_load();
	if(err < 0){
		printf("Loading settings failed, reason: %d (%s)\n", err, strerror(-err));
		printf("continuing anyways\n");
	} else {
		printf("Settings loaded\n");
	}

	default_conn = NULL;

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		printf("Failed to register authorization info callbacks, reason: %d (%s).\n", err, strerror(-err));
		return -1;
	}
	printf("Bluetooth authentication info callbacks registered.\n");

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		printf("Failed to register authorization callbacks, reason: %d ( %s)\n", err, strerror(-err));
		return -1;
	}

	printf("Bluetooth authentication callbacks registered.\n");

	err = bt_conn_cb_register(&connection_callbacks);
	if (err) {
		printf("Failed to register connection callbacks, reason: %d (%s)\n", err, strerror(-err));
		return -1;
	}
	printf("Bluetooth connection callbacks registered.\n");

	init_timer();

	return 0;
}

static int cmd_send_data(const struct shell *sh, size_t argc, char *argv[])
{

	const char *count = NULL;
	const char *payload_size = NULL;
	int n = 1; // number of loops. default 1 (if no second argument is specified)
	payload_size_i = 1; // payload size
	char *endptr;

	// I need at least one argument send_data
	if (argc < 2) {
		shell_error(sh, "Wrong number of arguments.");
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	// if I have more than one, I want to adjust packet count, wait time and payload size

	if (argc > 1) {
		count = argv[1];
		// Convert value in *number into integer
		n = (int)strtol(count, &endptr, 10);
		if (*endptr != '\0') {
			shell_error(sh, "Argument must be numeric.");
			return -EINVAL;
		}
		if(n > 100) {
			shell_error(sh, "Maximum amount of iterations is 100.");
		}
	}

	if (argc > 2) {
		payload_size = argv[2];
		// Convert value in *number into integer
		payload_size_i = (int)strtol(payload_size, &endptr, 10);
		if (*endptr != '\0') {
			shell_error(sh, "Argument must be numeric.");
			return -EINVAL;
		}
	}

	// If *number contains a value <= 0 -> no packets can be sent, so we throw an error
	if (n <= 0) {
		shell_error(sh, "Number of sent packets must be at least 1.");
		return -EINVAL;
	}

	// if n is > 0 -> packets can be sent
	if (default_conn) {
		for(int i=0; i<n; i++) {

			while(notification_sent == false) {
				k_sleep(K_MSEC(500));
			}

			/* Set the measurement variables to zero */
			//reset_values();

			const int err = send_notification(default_conn, &notification_srv.attrs[2], payload_size_i);
			if(err) {
				shell_error(sh, "Failed to send data. Reason: err=%d", err);
			}
		}
	} else {
		shell_print(sh, "Not connected");
	}

	notification_sent = false;

	/* Set the measurement array(s) to zero */
	// reset_measurements();
	return 0;
}

static int cmd_service_and_characteristic_discovery(const struct shell *sh, size_t argc, char *argv[]) {
	int err = bt_gatt_discover(default_conn, &discover_params);
	if (err) {
		shell_error(sh,"Service and Characteristic discovery failed (err %d)\n", err);
	}
	return 0;
}

static int cmd_subscribe(const struct shell *sh, size_t argc, char *argv[])
{
	int err = bt_gatt_subscribe(default_conn, &subscribe_params);
	if (err) {
		shell_error(sh,"Subscribe failed");
	} else {
		shell_print(sh,"Subscribing...\n");
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

int main(void)
{
	printf("BLE Testing Framework %s\n", CONFIG_BOARD_TARGET);

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
	SHELL_CMD_ARG(advertise, NULL, "<value: start, stop>", cmd_advertise, 2, 0),
	SHELL_CMD_ARG(scan, NULL, "<value: start, stop>", cmd_scan, 2, 0),
	SHELL_CMD_ARG(connect, NULL, HELP_NONE, cmd_connect, 3, 0),
	SHELL_CMD_ARG(disconnect, NULL, HELP_NONE, cmd_disconnect, 3, 0),
	SHELL_CMD(security, NULL, "security level 2 for Nino attack", cmd_security),
	SHELL_CMD_ARG(pair, NULL, NULL, cmd_pair, 3, 0),
	SHELL_CMD(bonds, NULL, HELP_NONE, cmd_bonds),
	SHELL_CMD_ARG(unpair, NULL, "[all] ["HELP_ADDR_LE"]", cmd_pairing_delete, 2, 1),
	SHELL_CMD_ARG(knob, NULL, "<true/false> (reduce LTK entropy)", cmd_knob, 2, 0),
	SHELL_CMD_ARG(scda, NULL, "<true/false> (enable/disable Secure Connections Downgrade Attack)", cmd_scda, 2, 0),
	SHELL_CMD_ARG(id_reset, NULL, "Enter an id which should be reset", cmd_reset, 2, 0),
	SHELL_CMD_ARG(id_save, NULL, "", cmd_ifa_id_save, 1, 0),
	SHELL_CMD_ARG(id_restore, NULL, "", cmd_ifa_id_restore, 1, 0),

	SHELL_CMD_ARG(snapshot, NULL, "", cmd_ifa_snapshot_take, 3, 0),
	SHELL_CMD_ARG(restore, NULL, "", cmd_ifa_snapshot_restore, 1, 0),

	SHELL_CMD_ARG(ifa1, NULL, "", cmd_ifa_stage1, 3, 0),
	SHELL_CMD(ifa1_p, NULL, HELP_NONE, cmd_ifa_stage1_periph),
	SHELL_CMD_ARG(ifa2, NULL, "", cmd_ifa_stage2, 4, 0),
	SHELL_CMD(ifa2_1_p, NULL, HELP_NONE, cmd_ifa_stage2_1_periph),
	SHELL_CMD(ifa2_2_p, NULL, HELP_NONE, cmd_ifa_stage2_2_periph),
	SHELL_CMD_ARG(ifa3, NULL, "", cmd_ifa_stage3, 1, 0),
	SHELL_CMD_ARG(ifa4, NULL, "", cmd_ifa_stage4, 3, 0),
	SHELL_CMD_ARG(ifa, NULL, "ifa addr addr_type n \n addr is target address formatted as "HELP_ADDR_LE" \n n is number of bondings\n", cmd_ifa, 4, 0),
	SHELL_CMD_ARG(send_data, NULL, HELP_NONE, cmd_send_data, 2, 2),
	SHELL_CMD_ARG(benchmark, NULL, "<value: on, off>", cmd_benchmark, 2, 0),
	SHELL_CMD(discover, NULL, "Discovery of Services and Characteristics", cmd_service_and_characteristic_discovery),
	SHELL_CMD(subscribe, NULL, "Subscription to a predefinec characteristic", cmd_subscribe),
	SHELL_CMD(time_results, NULL, "Get the average time of keystream generation, encryption or decryption", cmd_get_time_results),
	SHELL_CMD(reset_values, NULL, "Reset benchmarking values, counters and arrays.", reset_values),
	);

SHELL_CMD_REGISTER(bleframework, &cmds, "Bluetooth shell commands", cmd_default_handler);