/*
 * Device B (DUT) runs the benchmark on its own, so it can be powered by the PPK II
 * without USB/serial terminal (build with -DEXTRA_CONF_FILE=auto_dut.conf).
 * Same steps as the "under the hood" table in the README:
 *   init -> advertise start -> (Device A pairs, discovers, subscribes)
 *   per "bleframework run <payload_size> [n] [enc|dec]" on Device A (control characteristic):
 *   enc: benchmark on -> idle gap -> n notifications to A -> idle gap -> benchmark off -> results to A
 *   dec: benchmark on -> idle gap -> READY -> n writes from A -> idle gap -> benchmark off -> results to A
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/shell/shell.h>
#include <main.h>
#include <gatt_communication.h>
#include <benchmarking.h>
#include <auto_benchmark.h>
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX) || defined(CONFIG_SOC_COMPATIBLE_NRF52X)
#include <nRF52_54_ppi_dppi.h>
#elif defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#include <shared_variables.h>
#include <nRF52_54_ppi_dppi.h>
#endif

#if defined(CONFIG_BENCHMARK_AUTO_DUT)

#define BOOT_DELAY_MS 2000
/* quiet time before and after each block, gives every PPK II recording a clean baseline */
#define IDLE_GAP_MS 500

/* raw timer ticks per record; the nRF54L CCM has no separate KSGEN step */
#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
#define FIELDS_PER_RECORD 3
#else
#define FIELDS_PER_RECORD 6
#endif

#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
#define BENCHMARK_TIMER_HZ 16000000UL
#else
#define BENCHMARK_TIMER_HZ (BENCHMARK_TIMER_BASE_FREQ_HZ >> BENCHMARK_TIMER_PRESCALER)
#endif

/* decryption run: max. time for Device A's packets after READY */
#define WRITES_TIMEOUT_S 10

// number of measurements of this run: encryptions (mode ENC) or decryptions (mode DEC)
static uint32_t meas_count(uint8_t mode)
{
	#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
	uint32_t count = (mode == CONTROL_MODE_DEC)
		? NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurement_count
		: NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurement_count;
	#else
	uint32_t count = (mode == CONTROL_MODE_DEC) ? decryption_measurement_count
						    : encryption_measurement_count;
	#endif

	return MIN(count, SUM_ARRAY_MAX_SIZE);
}

// same record layout for both modes, so the PC side reads encryption and decryption alike
static void meas_get(uint8_t mode, uint32_t i, uint32_t fields[FIELDS_PER_RECORD])
{
	#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
	struct benchmark_measurement m = (mode == CONTROL_MODE_DEC)
		? NRF5340_SHARED_VARIABLES->benchmark_results.decryption_measurements[i]
		: NRF5340_SHARED_VARIABLES->benchmark_results.encryption_measurements[i];
	#else
	struct benchmark_measurement m = (mode == CONTROL_MODE_DEC) ? decryption_measurements[i]
								    : encryption_measurements[i];
	#endif

	#if defined(CONFIG_SOC_COMPATIBLE_NRF54LX)
	fields[0] = m.t_start_ENDCRYPT;
	fields[1] = m.t_end_ENDCRYPT;
	fields[2] = m.delta_ENDCRYPT;
	#else
	fields[0] = m.t_start_KSGEN;
	fields[1] = m.t_end_KSGEN;
	fields[2] = m.delta_KSGEN;
	fields[3] = m.t_start_ENDCRYPT;
	fields[4] = m.t_end_ENDCRYPT;
	fields[5] = m.delta_ENDCRYPT;
	#endif
}

// 0 never matches a notification (len == payload_size + 7), so the results are not measured and P0.04 stays quiet.
// Also switches the Rx filter of the decryption run off.
static void clear_payload_filter(void)
{
	#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
	NRF5340_SHARED_VARIABLES->benchmark_control.payload_size = 0;
	#else
	payload_size_i = 0;
	rx_payload_size_i = 0;
	rx_handle_i = 0;
	#endif
}

// decryption run: the controller measures A's Write Commands to data-in with this payload size
static void set_rx_filter(uint8_t payload_size)
{
	#if defined(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
	// not implemented on the nRF5340 (no Rx filter variables in the shared memory yet)
	ARG_UNUSED(payload_size);
	#else
	rx_handle_i = data_in_handle();
	rx_payload_size_i = payload_size;
	#endif
}

static bool link_ready(void)
{
	return default_conn &&
	       bt_conn_get_security(default_conn) >= BT_SECURITY_L2 &&
	       notify_enabled && results_notify_enabled;
}

// packets: sent (mode ENC) or received (mode DEC) by the DUT
static int dump_results(uint8_t payload_size, uint16_t packets, uint8_t mode)
{
	uint8_t buf[RESULTS_DATA_HEADER_LEN + 244];
	uint32_t fields[FIELDS_PER_RECORD];
	uint16_t count = meas_count(mode);
	int err;

	buf[0] = RESULTS_BEGIN;
	buf[1] = payload_size;
	sys_put_le16(packets, &buf[2]);
	sys_put_le16(count, &buf[4]);
	buf[6] = FIELDS_PER_RECORD;
	sys_put_le32(BENCHMARK_TIMER_HZ, &buf[7]);

	err = send_results(default_conn, buf, RESULTS_BEGIN_LEN);
	if (err) {
		return err;
	}

	// as many records as fit into one notification (ATT MTU - 3, max. 244)
	uint16_t max_len = MIN(bt_gatt_get_mtu(default_conn) - 3, 244);
	uint8_t records_per_packet = (max_len - RESULTS_DATA_HEADER_LEN) / (FIELDS_PER_RECORD * 4);

	for (uint16_t first = 0; first < count; first += records_per_packet) {
		uint8_t n_records = MIN(records_per_packet, count - first);
		uint8_t *value = &buf[RESULTS_DATA_HEADER_LEN];

		buf[0] = RESULTS_DATA;
		buf[1] = payload_size;
		sys_put_le16(first, &buf[2]);
		buf[4] = n_records;

		for (int r = 0; r < n_records; r++) {
			meas_get(mode, first + r, fields);
			for (int f = 0; f < FIELDS_PER_RECORD; f++) {
				sys_put_le32(fields[f], value);
				value += 4;
			}
		}

		err = send_results(default_conn, buf, value - buf);
		if (err) {
			return err;
		}
	}

	buf[0] = RESULTS_END;
	buf[1] = payload_size;

	return send_results(default_conn, buf, 2);
}

void auto_benchmark_run(void)
{
	int err;

	k_sleep(K_MSEC(BOOT_DELAY_MS));

	err = ble_init();
	if (err) {
		shell_error(shell, "[AUTO] Bluetooth init failed (err %d)", err);
		return;
	}

	// pair from scratch every run, so the pairing configuration of Device A (knob/scda) applies
	(void)bt_unpair(BT_ID_DEFAULT, NULL);

	w_advertising_start();
	shell_print(shell, "[AUTO] Waiting for Device A to pair and subscribe...");

	while (!link_ready()) {
		k_sleep(K_MSEC(100));
	}

	shell_print(shell, "[AUTO] Device A ready, waiting for run requests");

	while (true) {
		k_sem_take(&run_request_sem, K_FOREVER);

		uint8_t payload_size = run_request_payload_size;
		uint16_t packets = run_request_packets;
		uint8_t mode = run_request_mode;

		if (mode == CONTROL_MODE_DEC) {
			// count A's payloads from now on; the Tx filter stays off (payload_size_i = 0),
			// so no notification of B (e.g. READY) is measured as an encryption
			expect_writes(payload_size, packets);
			set_rx_filter(payload_size);
		}

		// also resets the arrays and counters
		benchmark_on();
		k_sleep(K_MSEC(IDLE_GAP_MS));

		if (mode == CONTROL_MODE_DEC) {
			uint8_t ready[2] = { RESULTS_READY, payload_size };

			err = send_results(default_conn, ready, sizeof(ready));
			if (!err) {
				err = wait_writes(K_SECONDS(WRITES_TIMEOUT_S));
			}
			if (err) {
				shell_error(shell, "[AUTO] Receiving payload %u failed (err %d, %u/%u received)",
					    payload_size, err, writes_received(), packets);
			}
			packets = writes_received();
		} else {
			err = send_packets(packets, payload_size, true);
			if (err) {
				shell_error(shell, "[AUTO] Sending payload %u failed (err %d)", payload_size, err);
			}
		}

		k_sleep(K_MSEC(IDLE_GAP_MS));
		benchmark_off();
		clear_payload_filter();

		// sent even after a send/receive error, A waits for RES_END and sees count < packets
		err = dump_results(payload_size, packets, mode);
		if (err) {
			shell_error(shell, "[AUTO] Sending the results of payload %u failed (err %d)", payload_size, err);
		}
	}
}

#endif /* CONFIG_BENCHMARK_AUTO_DUT */