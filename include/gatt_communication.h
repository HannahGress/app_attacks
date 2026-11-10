#pragma once

#include <stdbool.h>
#include <stdint.h>

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
 */
#define RESULTS_BEGIN        1
#define RESULTS_DATA         2
#define RESULTS_END          3
#define RESULTS_BATCH_DONE   4

#define RESULTS_BEGIN_LEN       11
#define RESULTS_DATA_HEADER_LEN 5

/*
 * Record on the control characteristic (Device A -> DUT), little endian.
 *   RUN: type, payload_size, packets (u16)
 */
#define CONTROL_RUN     1
#define CONTROL_RUN_LEN 4

// Device A: write a RUN record to the DUT
extern int request_run(struct bt_conn *conn, uint8_t payload_size, uint16_t packets);

// DUT: the latest RUN request, given via run_request_sem
extern struct k_sem run_request_sem;
extern volatile uint8_t run_request_payload_size;
extern volatile uint16_t run_request_packets;