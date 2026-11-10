"""Evaluate the per-payload measurements of one or all setups: duration (timer) and energy (PPK II).

Input, written by run_benchmark.py (one pair of files per payload size):
    <root>/raw/<Setup>/ppk_<NNN>.csv     PPK II: Timestamp(ms), Current(uA), D0, D1
    <root>/raw/<Setup>/timer_<NNN>.csv   timer ticks of every encryption in ns
Output:
    <root>/filtered/<Setup>/<NNN>.csv          one row per encryption
    <root>/accumulated/<Setup>/summary.csv     one row per payload size

Durations come from the 16 MHz timer, the PPK II (10 us resolution) only gives the current during
the pulse. Both are joined by order: the n-th D1 pulse is the n-th timer record.

Pins (active low, both on Device B, only for the benchmarked notifications, no resends):
    D0 = P0.03, keystream generation (KSGEN start -> ENDKSGEN), nRF52 only
    D1 = P0.04, encryption (ENDKSGEN -> ENDCRYPT), starts on the same edge where D0 ends

Usage:
    python evaluate.py [--root measurements/enc] [--setup SecCon_16Bytes] [--voltage 3.0]
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

SETUPS = ("Legacy_7Bytes", "Legacy_16Bytes", "SecCon_7Bytes", "SecCon_16Bytes")
DEFAULT_PAYLOADS = "1,20,50,100,150,200,244"

ENCRYPT = "D1"  # P0.04
KSGEN = "D0"    # P0.03
CURRENT = "Current(uA)"

SAMPLE_US = 10           # PPK II at 100 kHz
MAX_PULSE_US = 1000      # longer lows are no encryption (e.g. pins read 0 before GPIO init)
IDLE_WINDOW_MS = 400     # baseline current: median of this window before the first pulse
MAX_DIFF_US = 10         # allowed difference between PPK pulse and timer duration (one sample)

# ksgen_encrypt = the whole CCM job (nRF52: ksgen + encrypt, nRF54L: only this one is measured)
PHASES = ("ksgen", "encrypt", "ksgen_encrypt")
OPS_COLUMNS = ["index"] + [f"{p}_us" for p in PHASES] + [
    f"{prefix}{p}{suffix}" for p in PHASES
    for prefix, suffix in (("ppk_", "_us"), ("", "_current_uA"), ("", "_energy_uJ"), ("", "_energy_net_uJ"))]
# the CCM is a fixed-cycle hardware job: durations barely vary (std 0), max only flags outliers.
# Energy varies (PPK noise), so it gets mean + std. Every single value stays in filtered/<NNN>.csv.
DURATION_STATS = ("mean", "max")
ENERGY_STATS = ("mean", "std")


def parse_payloads(text):
    """'1,20,50' or '1-244' or mixed '1-10,20'."""
    payloads = []
    for part in text.split(","):
        lo, _, hi = part.partition("-")
        payloads += range(int(lo), int(hi or lo) + 1)
    return payloads


def low_runs(pin):
    """Start (inclusive) and end (exclusive) rows of every run where the pin is low (active)."""
    d = np.diff(np.concatenate(([0], (pin == 0).astype(np.int8), [0])))
    return np.flatnonzero(d == 1), np.flatnonzero(d == -1)


def ksgen_runs(pin, enc_start):
    """The D0 pulse of every encryption: it ends where the D1 pulse starts (+-1 sample).

    A KSGEN shorter than one sample (~10 us at tiny payloads) can fall between two samples; those get NaN.
    """
    k_start, k_end = low_runs(pin)
    if not len(k_end):
        return np.zeros_like(enc_start), np.ones_like(enc_start), np.zeros(len(enc_start), bool)
    j = np.minimum(np.searchsorted(k_end, enc_start - 1), len(k_end) - 1)
    found = np.abs(k_end[j] - enc_start) <= 1
    return np.where(found, k_start[j], 0), np.where(found, k_end[j], 1), found


def evaluate_payload(ppk_path, timer_path, voltage):
    ppk = pd.read_csv(ppk_path, usecols=[CURRENT, ENCRYPT, KSGEN],
                      dtype={CURRENT: "float32", ENCRYPT: "int8", KSGEN: "int8"})
    timer = pd.read_csv(timer_path)
    current = ppk[CURRENT].to_numpy()
    csum = np.concatenate(([0.0], np.cumsum(current, dtype=np.float64)))

    start, end = low_runs(ppk[ENCRYPT].to_numpy())
    keep = (end - start) * SAMPLE_US <= MAX_PULSE_US
    start, end = start[keep], end[keep]

    first = start[0] if len(start) else len(current)
    idle_uA = float(np.median(current[max(0, first - IDLE_WINDOW_MS * 1000 // SAMPLE_US):first]))

    # timer durations in us. nRF52: KSGEN and ENCRYPT are separate steps, ksgen_encrypt is their sum.
    # nRF54L: the CCM does both in one job, the timer and D1 cover it as a whole -> only ksgen_encrypt.
    nrf52 = "delta_KSGEN_ns" in timer
    ops = pd.DataFrame({"index": timer["index"]})
    if nrf52:
        ops["ksgen_us"] = timer["delta_KSGEN_ns"] / 1000
        ops["encrypt_us"] = timer["delta_ENCRYPT_ns"] / 1000
        ops["ksgen_encrypt_us"] = ops["ksgen_us"] + ops["encrypt_us"]
    else:
        ops["ksgen_encrypt_us"] = timer["delta_ENCRYPT_ns"] / 1000

    if len(start) != len(timer):
        print(f"Warning: {ppk_path.name}: {len(start)} D1 pulses but {len(timer)} timer records, no energy")
        return ops.reindex(columns=OPS_COLUMNS), idle_uA

    # PPK window (start row, end row, found) per phase
    every = np.ones(len(start), bool)
    if nrf52:
        # D1 starts where KSGEN ends, so the whole job is D1 extended back by the timer's KSGEN length.
        # Unlike the D0 pulse this never gets lost between two samples.
        ksgen_samples = np.ceil(ops["ksgen_us"].to_numpy() / SAMPLE_US).astype(int)
        windows = {
            "ksgen": ksgen_runs(ppk[KSGEN].to_numpy(), start),
            "encrypt": (start, end, every),
            "ksgen_encrypt": (np.maximum(start - ksgen_samples, 0), end, every),
        }
    else:
        windows = {"ksgen_encrypt": (start, end, every)}

    for phase, (s, e, found) in windows.items():
        mean_uA = np.where(found, (csum[e] - csum[s]) / (e - s), np.nan)
        ops[f"ppk_{phase}_us"] = np.where(found, (e - s) * SAMPLE_US, np.nan)
        ops[f"{phase}_current_uA"] = mean_uA
        ops[f"{phase}_energy_uJ"] = mean_uA * ops[f"{phase}_us"] * 1e-6 * voltage
        ops[f"{phase}_energy_net_uJ"] = (mean_uA - idle_uA) * ops[f"{phase}_us"] * 1e-6 * voltage

    # same columns on every board, the ones a board does not have stay empty
    return ops.reindex(columns=OPS_COLUMNS), idle_uA


def summarize(payload, ops, idle_uA):
    """One row per payload; columns a board has no values for (e.g. ksgen on the nRF54L) stay empty."""
    row = {"payload": payload, "ops": len(ops)}
    for phase in PHASES:
        for stat in DURATION_STATS:
            row[f"{phase}_{stat}_us"] = ops[f"{phase}_us"].agg(stat)
    for phase in PHASES:
        for stat in ENERGY_STATS:
            row[f"{phase}_energy_{stat}_uJ"] = ops[f"{phase}_energy_uJ"].agg(stat)
    for stat in ENERGY_STATS:
        row[f"ksgen_encrypt_energy_net_{stat}_uJ"] = ops["ksgen_encrypt_energy_net_uJ"].agg(stat)
    # D0 pulses the PPK missed (KSGEN shorter than a sample); only meaningful if ksgen energy was computed
    measured = ops["ksgen_us"].notna() & ops["ksgen_encrypt_energy_uJ"].notna()
    row["ksgen_missing"] = int(ops.loc[measured, "ksgen_energy_uJ"].isna().sum()) if measured.any() else np.nan
    row["idle_uA"] = idle_uA
    return row


def evaluate_setup(root, setup, voltage, payloads=None):
    """payloads: only evaluate these (default: every raw file); their rows replace older ones in summary.csv."""
    raw = Path(root) / "raw" / setup
    filtered = Path(root) / "filtered" / setup
    accumulated = Path(root) / "accumulated" / setup
    filtered.mkdir(parents=True, exist_ok=True)
    accumulated.mkdir(parents=True, exist_ok=True)

    rows = []
    for timer_path in sorted(raw.glob("timer_*.csv")):
        payload = int(timer_path.stem.split("_")[1])
        if payloads is not None and payload not in payloads:
            continue
        ppk_path = raw / f"ppk_{payload:03d}.csv"
        if not ppk_path.exists():
            print(f"Warning: {ppk_path} missing, skipped")
            continue

        ops, idle_uA = evaluate_payload(ppk_path, timer_path, voltage)
        ops.round(4).to_csv(filtered / f"{payload:03d}.csv", index=False)
        rows.append(summarize(payload, ops, idle_uA))

        # the PPK pulses should match the timer within one sample
        for phase in PHASES:
            ppk_median, timer_median = ops[f"ppk_{phase}_us"].median(), ops[f"{phase}_us"].median()
            if abs(ppk_median - timer_median) > MAX_DIFF_US:  # False for empty (NaN) columns
                print(f"Warning: payload {payload}: {phase} PPK pulse {ppk_median} us "
                      f"vs timer {timer_median:.2f} us")

    if not rows:
        print(f"No measurements in {raw}")
        return

    # newer rows replace the old ones of the same payload, the other payloads are kept
    summary_path = accumulated / "summary.csv"
    summary = pd.DataFrame(rows).round(4)
    if summary_path.exists():
        old = pd.read_csv(summary_path)
        old = old[~old["payload"].isin(summary["payload"])]
        if not old.empty:
            summary = pd.concat([old, summary], ignore_index=True)
    summary = summary.sort_values("payload", ignore_index=True)
    summary.to_csv(summary_path, index=False)
    print(f"\n{setup}:\n{summary.to_string(index=False)}")
    print(f"Done: {summary_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path("measurements/enc"))
    parser.add_argument("--setup", choices=SETUPS, help="default: every setup that has raw data")
    parser.add_argument("--voltage", type=float, default=3.0, help="PPK II source voltage in V")
    args = parser.parse_args()

    setups = [args.setup] if args.setup else [s for s in SETUPS if any((args.root / "raw" / s).glob("timer_*.csv"))]
    if not setups:
        sys.exit(f"No measurements below {args.root / 'raw'}")
    for setup in setups:
        evaluate_setup(args.root, setup, args.voltage)


if __name__ == "__main__":
    main()