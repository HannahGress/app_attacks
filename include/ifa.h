#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
//#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <autoconf.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

extern struct k_sem conn_sem;
extern struct k_sem disconn_sem;
extern struct k_sem bond_sem;

void ifa_init(const struct shell *sh);

int cmd_reset(const struct shell *sh, size_t argc, char *argv[]);

int cmd_ifa_id_save();
int cmd_ifa_id_restore();

int cmd_ifa_snapshot_take(const struct shell *sh, size_t argc, char *argv[]);
int cmd_ifa_snapshot_restore();

int cmd_ifa_stage1(const struct shell *sh, size_t argc, char *argv[]);
int cmd_ifa_stage1_periph(const struct shell *sh);
int cmd_ifa_stage2(const struct shell *sh, size_t argc, char *argv[]);
int cmd_ifa_stage2_1_periph(const struct shell *sh);
int cmd_ifa_stage2_2_periph(const struct shell *sh);
int cmd_ifa_stage3(const struct shell *sh, size_t argc, char *argv[]);
int cmd_ifa_stage4(const struct shell *sh, size_t argc, char *argv[]);

int cmd_ifa(const struct shell *sh, size_t argc, char *argv[]);