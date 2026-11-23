"""Idle current test: what do the benchmark timer and Bluetooth add to the idle current of Device B?

Device B runs the idle_test build (-DEXTRA_CONF_FILE=idle_test.conf) and is powered by the PPK II,
D1 wired to P0.04 like for the benchmark. Device A is not needed (keep it off, B advertises in phase 4/5).
B marks the start of every phase with a short pulse on D1 (see src/idle_test.c).

Prints the mean current per phase and writes the whole recording to measurements/idle/ppk_idle.csv.

Usage:
    python idle_test.py [--voltage 3.0] [--port-ppk COMx]
"""

import argparse
import sys
import time
from pathlib import Path

import numpy as np

import evaluate
from run_benchmark import Recorder, open_ppk

PHASES = ("sleep", "timer", "sleep again", "ble adv", "ble adv + timer")
RECORD_S = 1 + 5 * len(PHASES) + 2  # settle time on B + 5 s per phase + margin
SETTLE_S = 0.5                       # skip the start of each phase (e.g. bt_enable)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--voltage", type=float, default=3.0, help="PPK II source voltage in V")
    parser.add_argument("--port-ppk", help="serial port of the PPK II (default: first one found)")
    parser.add_argument("--out", type=Path, default=Path("measurements/idle"))
    args = parser.parse_args()

    ppk = open_ppk(args.port_ppk, args.voltage)

    # record from power-on, so the boot is in the recording too
    recorder = Recorder(ppk)
    recorder.start()
    ppk.toggle_DUT_power("ON")
    print(f"Recording {RECORD_S} s, please wait...")
    time.sleep(RECORD_S)
    data = recorder.stop()
    ppk.toggle_DUT_power("OFF")

    args.out.mkdir(parents=True, exist_ok=True)
    data.to_csv(args.out / "ppk_idle.csv", index=False)

    # phase starts = the short D1 pulses (longer lows are the pin before GPIO init)
    start, end = evaluate.low_runs(data["D1"].to_numpy())
    keep = (end - start) * evaluate.SAMPLE_US <= evaluate.MAX_PULSE_US
    marks = start[keep]
    if len(marks) < len(PHASES) + 1:
        sys.exit(f"Found {len(marks)} phase markers, expected {len(PHASES) + 1}. Is D1 wired to P0.04?")

    current = data["Current(uA)"].to_numpy()
    settle = int(SETTLE_S * 1e6 / evaluate.SAMPLE_US)
    baseline = None

    print(f"\n{'phase':<18}{'starts at':>11}{'mean':>12}{'median':>12}{'vs sleep':>12}")
    for i, name in enumerate(PHASES):
        phase = current[marks[i] + settle:marks[i + 1]]
        mean, median = phase.mean(), np.median(phase)
        baseline = mean if baseline is None else baseline
        t = data["Timestamp(ms)"][marks[i]] / 1000
        print(f"{name:<18}{t:>9.1f} s{mean:>9.1f} uA{median:>9.1f} uA{mean - baseline:>+9.1f} uA")

    print(f"\nWhole recording: {args.out / 'ppk_idle.csv'} (open it in a plot to see when the current changes)")


if __name__ == "__main__":
    main()