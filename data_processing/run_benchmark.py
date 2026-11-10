"""Measure every payload size in its own run: PPK II recording + timer ticks per payload.

The PPK II (source meter) powers Device B (DUT, auto_dut build, no USB). Device A (normal build, USB)
pairs with B and sends "bleframework run <payload> <n>" for each payload size. B measures that one size and
sends its timer ticks back to A, which prints them as RES lines. Per payload this writes
    measurements/enc/raw/<Setup>/ppk_<NNN>.csv     current + D0/D1 of the PPK II
    measurements/enc/raw/<Setup>/timer_<NNN>.csv   timer ticks of every encryption, converted to ns
and evaluates the setup at the end (evaluate.py).

B measures at most 100 packets per "run" (its timer array), so more packets are sent as blocks of 100
within one PPK recording. --n packets are sent for every payload size.

Usage:
    python run_benchmark.py --setup SecCon_16Bytes --n 1000 [--payloads 1-244]
"""

import argparse
import re
import sys
import threading
import time
from pathlib import Path

import numpy as np
import pandas as pd
from ppk2_api.ppk2_api import PPK2_API

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "serial_interface"))
from serial_controller import SerialController, find_nordic_boards  # noqa: E402  (Hannah's script, unchanged)

import evaluate  # noqa: E402

# setup -> pairing configuration on A (knob = 7-byte key, scda = Legacy pairing)
SETUP_COMMANDS = {
    "SecCon_16Bytes": ("knob false", "scda false"),
    "SecCon_7Bytes": ("knob true", "scda false"),
    "Legacy_16Bytes": ("knob false", "scda true"),
    "Legacy_7Bytes": ("knob true", "scda true"),
}
TIMER_FIELDS = ("t_start_KSGEN", "t_end_KSGEN", "delta_KSGEN", "t_start_ENCRYPT", "t_end_ENCRYPT", "delta_ENCRYPT")
DEVICE_ADDRESS = re.compile(r"\[DEVICE\]:\s+([0-9A-Fa-f:]{17}\s+\([^)]+\)),\s+Name:\s+BLE Framework,")
LTK = re.compile(r"^(SC|Legacy) LTK: 0x([0-9a-fA-F]+)")
RECORD = re.compile(r"RES(_BEGIN|_END)?,\d+(,\d+)*")

BOOT_WAIT_S = 4       # B: 2 s boot delay + BLE init + advertising
PRE_RECORD_S = 0.2    # PPK runs a bit before "run", B adds another 500 ms idle gap
RUN_TIMEOUT_S = 30    # per block
BLOCK = 100           # B's timer array holds 100 records (SUM_ARRAY_MAX_SIZE), more packets = more blocks


class Recorder:
    """Collects the raw PPK II byte stream in a thread, decoding happens after the recording."""

    def __init__(self, ppk):
        self.ppk = ppk
        self.chunks = []
        self.running = False
        self.thread = None

    def start(self):
        self.chunks = []
        self.running = True
        self.ppk.start_measuring()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        while self.running:
            data = self.ppk.get_data()
            if data:
                self.chunks.append(data)
            time.sleep(0.005)

    def stop(self):
        self.running = False
        self.thread.join()
        self.ppk.stop_measuring()
        time.sleep(0.05)
        self.chunks.append(self.ppk.get_data())

        self.ppk.remainder = {"sequence": b"", "len": 0}
        current, bits = self.ppk.get_samples(b"".join(self.chunks))
        bits = np.asarray(bits, dtype=np.uint8)
        return pd.DataFrame({
            "Timestamp(ms)": np.round(np.arange(len(current)) * evaluate.SAMPLE_US / 1000, 2),
            "Current(uA)": np.round(current, 3),
            "D0": bits & 1,
            "D1": (bits >> 1) & 1,
        })


def read_lines(a, until, timeout):
    """Reads A's serial output until until(line) is true; returns all lines.

    The shell redraws its prompt between asynchronous prints ("uart:~$ " + "\\r" + escape codes), so "\\r" also
    ends a line and a RES record is cut out of wherever it sits in the line.
    """
    lines, buffer, end = [], "", time.monotonic() + timeout
    while time.monotonic() < end:
        waiting = a.serial.in_waiting
        if not waiting:
            time.sleep(0.01)
            continue
        buffer += a._strip_ansi(a.serial.read(waiting).decode("utf-8", errors="replace"))
        *complete, buffer = re.split(r"[\r\n]", buffer)
        for line in complete:
            record = RECORD.search(line)
            line = record.group(0) if record else line.strip()
            if not line:
                continue
            lines.append(line)
            if until(line):
                return lines
    raise TimeoutError(f"no answer from Device A within {timeout} s")


def command(a, cmd, timeout=5.0):
    ok, response = a.write_with_response(f"bleframework {cmd}", "central", timeout)
    if not ok:
        sys.exit(f"'{cmd}' failed on Device A")
    return response or ""


def connect(a, setup):
    # init first, unpair needs Bluetooth enabled; -120 (EALREADY) = A is still initialized from a previous run
    ok, response = a.write_with_response("bleframework init", "central", 5.0)
    if not ok:
        if "-120" not in (response or ""):
            sys.exit("'init' failed on Device A")
        print("[Info] Device A was already initialized, continuing")
    command(a, "unpair all")
    for cmd in SETUP_COMMANDS[setup]:
        command(a, cmd)

    while True:
        matches = set(DEVICE_ADDRESS.findall(command(a, "scan start", 3.0)))
        command(a, "scan stop")
        if len(matches) == 1:
            address = matches.pop()
            break
        print(f"[Info] {len(matches)} BLE Framework devices found (need exactly 1, RSSI > -70), scanning again")

    # check that the negotiated pairing really is the requested setup
    pairing = command(a, f"pair {address}", 15.0)
    ltk = next((m for m in map(LTK.match, pairing.splitlines()) if m), None)
    got = f"{'SecCon' if ltk and ltk[1] == 'SC' else 'Legacy'}_{len(ltk[2]) // 2 if ltk else 0}Bytes"
    if got != setup:
        sys.exit(f"Pairing gave {got}, expected {setup}")

    command(a, "discover")
    command(a, "subscribe")
    time.sleep(0.5)  # B polls link_ready() every 100 ms


def run_block(a, payload, n):
    """One 'run' on B (n <= BLOCK packets); returns A's lines and the timer records as ns."""
    a.send_command(f"bleframework run {payload} {n}")
    lines = read_lines(a, lambda line: line == f"RES_END,{payload}", RUN_TIMEOUT_S)

    begin = next(line for line in lines if line.startswith("RES_BEGIN,")).split(",")
    count, fields, timer_hz = int(begin[3]), int(begin[4]), int(begin[5])
    names = TIMER_FIELDS[-fields:]  # nRF54L: ENCRYPT only

    records = [line.split(",")[3:] for line in lines if line.startswith(f"RES,{payload},")]
    records = [r for r in records if len(r) == fields]  # ticks only, cut lines are dropped
    ticks = np.array(records, dtype=np.float64).reshape(-1, fields)
    timer = pd.DataFrame(ticks * 1e9 / timer_hz, columns=[f"{name}_ns" for name in names])
    return lines, count, timer


def run_payload(a, recorder, payload, n, out_dir):
    """n packets as blocks of BLOCK, all in one PPK recording: one ppk + one timer file per payload."""
    blocks = [BLOCK] * (n // BLOCK) + ([n % BLOCK] if n % BLOCK else [])
    timers, log_lines, problems = [], [], []

    recorder.start()
    time.sleep(PRE_RECORD_S)
    try:
        for i, size in enumerate(blocks):
            # one block takes a few seconds (2 x 500 ms idle on B + packets + results), show that it is alive
            print(f"Payload {payload:3d}: sending block {i + 1}/{len(blocks)} ({size} packets)...", end="\r", flush=True)
            lines, count, timer = run_block(a, payload, size)
            timers.append(timer)
            log_lines += lines
            if count != size or len(timer) != size:
                problems.append(f"block {i}: B measured {count}, A received {len(timer)}, expected {size}")
    except TimeoutError:
        sys.exit(f"Payload {payload}: no RES_END from Device A within {RUN_TIMEOUT_S} s (block {len(timers)})")
    finally:
        ppk = recorder.stop()

    timer = pd.concat(timers, ignore_index=True)
    timer.insert(0, "index", timer.index)  # 0..n-1 in the order of the D1 pulses

    if problems:
        log = out_dir / f"a_{payload:03d}.log"
        log.write_text("\n".join(log_lines))
        print(f"Warning: payload {payload}: " + "; ".join(problems) + f" (A's output: {log})")

    ppk.to_csv(out_dir / f"ppk_{payload:03d}.csv", index=False)
    timer.round(2).to_csv(out_dir / f"timer_{payload:03d}.csv", index=False)
    print(f"Payload {payload:3d}: {len(timer)} timer records in {len(blocks)} block(s), {len(ppk)} PPK samples")


def pick_board(port):
    if port:
        return port
    boards = find_nordic_boards()
    if not boards:
        sys.exit("No board found, is Device A connected via USB? (or pass --port-a COMx)")
    if len(boards) == 1:
        return boards[0]["port"]

    for i, board in enumerate(boards, start=1):
        print(f"{i}: {board['board']}  serial {board['serial_number']}  {board['port']}")
    while True:
        choice = input("Device A (list position or serial number): ").strip()
        for i, board in enumerate(boards, start=1):
            if choice in (str(i), str(board["serial_number"])):
                return board["port"]
        print("Not in the list, try again")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--setup", required=True, choices=evaluate.SETUPS)
    parser.add_argument("--payloads", default=evaluate.DEFAULT_PAYLOADS, help="e.g. 1,20,50 or 1-244")
    parser.add_argument("--n", type=int, required=True, help="packets per payload size")
    parser.add_argument("--voltage", type=float, default=3.0, help="PPK II source voltage in V")
    parser.add_argument("--port-a", help="serial port of Device A (default: ask nrfutil)")
    parser.add_argument("--port-ppk", help="serial port of the PPK II (default: first one found)")
    parser.add_argument("--root", type=Path, default=Path("measurements/enc"))
    args = parser.parse_args()
    if args.n < 1:
        parser.error("--n must be at least 1")
    payloads = evaluate.parse_payloads(args.payloads)

    out_dir = args.root / "raw" / args.setup
    out_dir.mkdir(parents=True, exist_ok=True)

    ppk_port = args.port_ppk or next(iter(PPK2_API.list_devices()), None)
    if not ppk_port:
        sys.exit("No PPK II found (close the Power Profiler app, it blocks the port)")
    ppk = PPK2_API(ppk_port, timeout=1, write_timeout=1, exclusive=True)
    ppk.get_modifiers()
    ppk.use_source_meter()
    ppk.set_source_voltage(int(args.voltage * 1000))

    a = SerialController()
    a.serial.port = pick_board(args.port_a)
    if not a.connect():
        sys.exit(1)

    ppk.toggle_DUT_power("ON")
    print(f"Device B powered with {args.voltage} V, waiting {BOOT_WAIT_S} s for it to boot")
    time.sleep(BOOT_WAIT_S)

    try:
        connect(a, args.setup)
        print(f"Subscribed. Benchmarking {len(payloads)} payload size(s) x {args.n} packets, please wait...")
        recorder = Recorder(ppk)
        for payload in payloads:
            run_payload(a, recorder, payload, args.n, out_dir)
    finally:
        ppk.toggle_DUT_power("OFF")
        a.disconnect()

    # only this run's payloads, their rows replace the older ones in summary.csv
    print("Evaluating the recordings, please wait...")
    evaluate.evaluate_setup(args.root, args.setup, args.voltage, payloads)


if __name__ == "__main__":
    main()