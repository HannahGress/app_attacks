"""Evaluate the per-payload measurements of one or all setups: duration (timer) and energy (PPK II).

Input, written by run_benchmark.py (one pair of files per payload size):
    <root>/raw/<Setup>/ppk_<NNN>.csv     PPK II: Timestamp(ms), Current(uA), D0, D1 (enc) or D2, D3 (dec)
    <root>/raw/<Setup>/timer_<NNN>.csv   timer ticks of every encryption/decryption in ns
Output:
    <root>/filtered/<Setup>/<NNN>.csv          one row per encryption/decryption
    <root>/accumulated/<Setup>/summary.csv     one row per payload size

Durations come from the 16 MHz timer, the PPK II (10 us resolution) only gives the current during
the pulse. Both are joined by order: the n-th D1 (or tagged D2) pulse is the n-th timer record.

Pins (active low, all on Device B):
    enc, only for the benchmarked notifications, no resends:
    D0 = P0.03, keystream generation (KSGEN start -> ENDKSGEN), nRF52 only
    D1 = P0.04, encryption (ENDKSGEN -> ENDCRYPT), starts on the same edge where D0 ends
    dec:
    D2 = P0.28, decryption (RADIO ADDRESS -> ENDCRYPT) of every received packet, includes the reception
    D3 = P0.29, goes low after a D2 pulse that was measured

Usage:
    python evaluate.py [--mode enc|dec] [--setup SecCon_16Bytes]
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
DECRYPT = "D2"  # P0.28
TAG = "D3"      # P0.29
PINS = {"enc": (KSGEN, ENCRYPT), "dec": (DECRYPT, TAG)}  # recorded per mode
CURRENT = "Current(uA)"

VOLTAGE_V = 3.0          # PPK II source voltage for Device B (always 3.0 V)
SAMPLE_US = 10           # PPK II at 100 kHz
MAX_PULSE_US = 1000      # longer lows are no encryption (e.g. pins read 0 before GPIO init)
MAX_DECRYPT_US = 2500    # decryption takes as long as the reception (244 B: 1 ms on 2M, 2 ms on 1M)
IDLE_WINDOW_MS = 400     # baseline current: median of this window before the first pulse
MAX_DIFF_US = 10         # allowed difference between PPK pulse and timer duration (one sample)

# ksgen_encrypt = the whole CCM job (nRF52: ksgen + encrypt, nRF54L: only this one is measured)
# dec on the nRF52: KSGEN runs earlier and has no pin, so only "decrypt" has an energy
PHASES = {
    "enc": ("ksgen", "encrypt", "ksgen_encrypt"),
    "dec": ("ksgen", "decrypt", "ksgen_decrypt"),
}
NET_PHASES = {"enc": ("ksgen_encrypt",), "dec": ("decrypt", "ksgen_decrypt")}  # net energy in the summary
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


def tagged_runs(pin, tag):
    """The D2 pulses that are followed by a D3 falling edge before the next D2 pulse."""
    start, end = low_runs(pin)
    tag_start, _ = low_runs(tag)
    k = np.searchsorted(end, tag_start, side="right") - 1  # last pulse that ended before the tag
    k = np.unique(k[k >= 0])                               # -1: D3 was low before the first pulse
    return start[k], end[k]


def ops_columns(mode):
    phases = PHASES[mode]
    return ["index"] + [f"{p}_us" for p in phases] + [
        f"{prefix}{p}{suffix}" for p in phases
        for prefix, suffix in (("ppk_", "_us"), ("", "_current_uA"), ("", "_energy_uJ"), ("", "_energy_net_uJ"))]


def evaluate_payload(ppk_path, timer_path):
    timer = pd.read_csv(timer_path)
    mode = "dec" if "delta_DECRYPT_ns" in timer else "enc"
    _, op, ksgen_op = PHASES[mode]
    delta_op = f"delta_{op.upper()}_ns"
    columns = ops_columns(mode)

    pins = PINS[mode]
    ppk = pd.read_csv(ppk_path, usecols=[CURRENT, *pins], dtype={CURRENT: "float32", **{p: "int8" for p in pins}})
    current = ppk[CURRENT].to_numpy()
    csum = np.concatenate(([0.0], np.cumsum(current, dtype=np.float64)))

    if mode == "dec":
        start, end = tagged_runs(ppk[DECRYPT].to_numpy(), ppk[TAG].to_numpy())
        max_us = MAX_DECRYPT_US
    else:
        start, end = low_runs(ppk[ENCRYPT].to_numpy())
        max_us = MAX_PULSE_US
    keep = (end - start) * SAMPLE_US <= max_us
    start, end = start[keep], end[keep]

    first = start[0] if len(start) else len(current)
    idle_uA = float(np.median(current[max(0, first - IDLE_WINDOW_MS * 1000 // SAMPLE_US):first]))

    # timer durations in us. nRF52: KSGEN and ENCRYPT are separate steps, ksgen_encrypt is their sum.
    # nRF54L: the CCM does both in one job, the timer and D1 cover it as a whole -> only ksgen_encrypt.
    # Same for DECRYPT.
    nrf52 = "delta_KSGEN_ns" in timer
    ops = pd.DataFrame({"index": timer["index"]})
    if nrf52:
        ops["ksgen_us"] = timer["delta_KSGEN_ns"] / 1000
        ops[f"{op}_us"] = timer[delta_op] / 1000
        ops[f"{ksgen_op}_us"] = ops["ksgen_us"] + ops[f"{op}_us"]
    else:
        ops[f"{ksgen_op}_us"] = timer[delta_op] / 1000

    if len(start) != len(timer):
        name = "tagged D2" if mode == "dec" else "D1"
        print(f"Warning: {ppk_path.name}: {len(start)} {name} pulses but {len(timer)} timer records, no energy")
        return ops.reindex(columns=columns), idle_uA, mode

    # PPK window (start row, end row, found) per phase
    every = np.ones(len(start), bool)
    if not nrf52:
        windows = {ksgen_op: (start, end, every)}
    elif mode == "dec":
        windows = {"decrypt": (start, end, every)}
    else:
        # D1 starts where KSGEN ends, so the whole job is D1 extended back by the timer's KSGEN length.
        # Unlike the D0 pulse this never gets lost between two samples.
        ksgen_samples = np.ceil(ops["ksgen_us"].to_numpy() / SAMPLE_US).astype(int)
        windows = {
            "ksgen": ksgen_runs(ppk[KSGEN].to_numpy(), start),
            "encrypt": (start, end, every),
            "ksgen_encrypt": (np.maximum(start - ksgen_samples, 0), end, every),
        }

    for phase, (s, e, found) in windows.items():
        mean_uA = np.where(found, (csum[e] - csum[s]) / (e - s), np.nan)
        ops[f"ppk_{phase}_us"] = np.where(found, (e - s) * SAMPLE_US, np.nan)
        ops[f"{phase}_current_uA"] = mean_uA
        ops[f"{phase}_energy_uJ"] = mean_uA * ops[f"{phase}_us"] * 1e-6 * VOLTAGE_V
        ops[f"{phase}_energy_net_uJ"] = (mean_uA - idle_uA) * ops[f"{phase}_us"] * 1e-6 * VOLTAGE_V

    # same columns on every board, the ones a board does not have stay empty
    return ops.reindex(columns=columns), idle_uA, mode


def summarize(payload, ops, idle_uA, mode):
    """One row per payload; columns a board has no values for (e.g. ksgen on the nRF54L) stay empty."""
    row = {"payload": payload, "ops": len(ops)}
    for phase in PHASES[mode]:
        for stat in DURATION_STATS:
            row[f"{phase}_{stat}_us"] = ops[f"{phase}_us"].agg(stat)
    for phase in PHASES[mode]:
        for stat in ENERGY_STATS:
            row[f"{phase}_energy_{stat}_uJ"] = ops[f"{phase}_energy_uJ"].agg(stat)
    for phase in NET_PHASES[mode]:
        for stat in ENERGY_STATS:
            row[f"{phase}_energy_net_{stat}_uJ"] = ops[f"{phase}_energy_net_uJ"].agg(stat)
    if mode == "enc":
        # D0 pulses the PPK missed (KSGEN shorter than a sample); only meaningful if ksgen energy was computed
        measured = ops["ksgen_us"].notna() & ops["ksgen_encrypt_energy_uJ"].notna()
        row["ksgen_missing"] = int(ops.loc[measured, "ksgen_energy_uJ"].isna().sum()) if measured.any() else np.nan
    row["idle_uA"] = idle_uA
    return row


def evaluate_setup(root, setup, payloads=None):
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

        ops, idle_uA, mode = evaluate_payload(ppk_path, timer_path)
        ops.round(4).to_csv(filtered / f"{payload:03d}.csv", index=False)
        rows.append(summarize(payload, ops, idle_uA, mode))

        # the PPK pulses should match the timer within one sample
        for phase in PHASES[mode]:
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
    parser.add_argument("--mode", choices=PHASES, default="enc")
    parser.add_argument("--root", type=Path, help="default: measurements/<mode>")
    parser.add_argument("--setup", choices=SETUPS, help="default: every setup that has raw data")
    args = parser.parse_args()
    args.root = args.root or Path("measurements") / args.mode

    setups = [args.setup] if args.setup else [s for s in SETUPS if any((args.root / "raw" / s).glob("timer_*.csv"))]
    if not setups:
        sys.exit(f"No measurements below {args.root / 'raw'}")
    for setup in setups:
        evaluate_setup(args.root, setup)


if __name__ == "__main__":
    main()