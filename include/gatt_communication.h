#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

extern volatile bool notification_sent;
extern bool notify_enabled;
extern bool results_notify_enabled;
// suppresses the per-packet shell output of notify_complete_cb during a measurement
extern volatile bool benchmark_quiet;
extern const struct bt_gatt_service_static notification_srv;
extern int send_notification(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, int payload_size);
extern int send_results(struct bt_conn *conn, const uint8_t *data, uint16_t len);
extern int start_discovery(struct bt_conn *conn);
extern int subscribe_all(struct bt_conn *conn);
extern struct bt_gatt_subscribe_params subscribe_params;
extern struct bt_gatt_discover_params discover_params;

/*
 * Records on the results characteristic (DUT -> Device A), little endian.
 * The first byte of every notification is the record type.
 *   BEGIN:      type, payload_size, packets_sent (u16), count (u16), fields_per_record, timer_hz (u32)
 *   DATA:       type, payload_size, first_index (u16), n_records, n_records * fields_per_record * u32 (raw timer ticks)
 *   END:        type, payload_size
 *   BATCH_DONE: type
 *   READY:      type, payload_size   (decryption run: the DUT waits for Device A's packets now)
 * In a decryption run, packets_sent of BEGIN is the number of packets the DUT received.
 */
#define RESULTS_BEGIN        1
#define RESULTS_DATA         2
#define RESULTS_END          3
#define RESULTS_BATCH_DONE   4
#define RESULTS_READY        5

#define RESULTS_BEGIN_LEN       11
#define RESULTS_DATA_HEADER_LEN 5

/*
 * Record on the control characteristic (Device A -> DUT), little endian.
 *   RUN: type, payload_size, packets (u16), mode
 * mode ENC: the DUT sends <packets> notifications and measures their encryption.
 * mode DEC: the DUT sends READY, then Device A writes <packets> payloads to the
 *           DUT's data-in characteristic and the DUT measures their decryption.
 */
#define CONTROL_RUN     1
#define CONTROL_RUN_LEN 5

#define CONTROL_MODE_ENC 0
#define CONTROL_MODE_DEC 1

// Device A: write a RUN record to the DUT
extern int request_run(struct bt_conn *conn, uint8_t payload_size, uint16_t packets, uint8_t mode);
// Device A: wait (bounded) for the DUT's READY record of a decryption run
extern int wait_dut_ready(k_timeout_t timeout);
// Device A: write <n> random payloads to the DUT's data-in characteristic (decryption run)
extern int send_writes(struct bt_conn *conn, int n, int payload_size);

// DUT: the latest RUN request, given via run_request_sem
extern struct k_sem run_request_sem;
extern volatile uint8_t run_request_payload_size;
extern volatile uint16_t run_request_packets;
extern volatile uint8_t run_request_mode;

// DUT: payloads received on the data-in characteristic (decryption run), see expect_writes()
extern void expect_writes(uint8_t payload_size, uint16_t packets);
extern int wait_writes(k_timeout_t timeout);
extern uint16_t writes_received(void);
// DUT: ATT handle of the data-in value, the controller's Rx filter matches Write Commands to it
extern uint16_t data_in_handle(void);