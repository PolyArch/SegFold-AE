#!/usr/bin/env python3
"""
Collect experiment stats from JSON files into CSV tables.

Recursively finds all *_stats.json files under an output directory and
produces three CSV tables:
  - synthetic_results.csv   (files matching sim_s{size}_dA{dA}_dB{dB}_r{run}_stats.json)
  - suitesparse_results.csv (files matching *_{matrix_name}_segfold_stats.json)
  - ablation_results.csv    (synthetic files nested inside config subdirectories)

Usage:
    python3 scripts/collect_results.py OUTPUT_DIR
"""

import argparse
import json
import re
import sys
from pathlib import Path

import pandas as pd

# ---------------------------------------------------------------------------
# Stats keys to extract from every JSON file
# ---------------------------------------------------------------------------
STAT_KEYS = [
    "cycle",
    "macs",
    "ops",
    "avg_util",
    "max_util",
    "spad_load_hits",
    "spad_load_misses",
    "spad_stores",
    "avg_b_elements_on_switch",
    "avg_pes_waiting_spad",
    "avg_pes_fifo_blocked_stall",
    "avg_pes_fifo_empty_stall",
    "avg_sw_idle_b_row_conflict",
    "avg_sw_idle_no_b_element",
    "avg_sw_idle_not_in_range",
    "avg_sw_move_stall_by_fifo",
    "avg_sw_move_stall_by_network",
    "a_nnz",
    "b_nnz",
    "c_nnz",
]

# Regex for synthetic filenames:
#   sim_s256_dA5_dB10_r3_stats.json
SYNTHETIC_RE = re.compile(
    r"sim_s(\d+)_dA(\d+)_dB(\d+)_r(\d+)_stats\.json$"
)

# Regex for suitesparse filenames (old format produced by the Python simulator):
#   <HH:MM:SS>_<pid>_<matrix_name>_segfold_stats.json
SUITESPARSE_RE = re.compile(
    r"[\d:]+_\d+_(.+?)_segfold_stats\.json$"
)

# Regex for suitesparse filenames (new format, sim_ prefix):
#   sim_<matrix_name>_stats.json  (no s/dA/dB/r fields)
SUITESPARSE_SIM_RE = re.compile(
    r"sim_(?!s\d+_dA)(.+?)_stats\.json$"
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_stats(filepath: Path) -> dict:
    """Load a stats JSON file, returning an empty dict on failure."""
    try:
        with open(filepath, "r") as f:
            return json.load(f)
    except Exception as exc:
        print(f"  Warning: could not load {filepath}: {exc}", file=sys.stderr)
        return {}


def extract_stats(stats: dict) -> dict:
    """Pull the desired keys out of a loaded stats dict."""
    return {k: stats.get(k) for k in STAT_KEYS}


def detect_config_name(filepath: Path, output_dir: Path) -> str:
    """
    If *filepath* lives inside a config subdirectory (e.g.
    ``output_dir / <run_dir> / ours / file.json``), return the config
    directory name (``"ours"``).  Otherwise return ``""`` (empty string).
    """
    try:
        rel = filepath.relative_to(output_dir)
    except ValueError:
        return ""
    # rel parts examples:
    #   ("ours", "sim_..._stats.json")                -- depth 2, config = "ours"
    #   ("ablation_cpp_..", "ours", "sim_..._stats.json") -- depth 3, config = "ours"
    #   ("sim_..._stats.json",)                        -- depth 1, no config
    parts = rel.parts
    if len(parts) >= 2:
        # The component directly above the file is the candidate config name.
        candidate = parts[-2]
        # Skip if it looks like a top-level date-stamped output directory
        if not re.match(r"^\d{8}", candidate):
            return candidate
    return ""


# ---------------------------------------------------------------------------
# Collector
# ---------------------------------------------------------------------------

def collect(output_dir: Path):
    """Walk *output_dir*, classify files, and return three DataFrames."""
    synthetic_rows = []
    suitesparse_rows = []
    ablation_rows = []

    stats_files = sorted(output_dir.rglob("*_stats.json"))
    if not stats_files:
        print(f"No *_stats.json files found under {output_dir}")
        return pd.DataFrame(), pd.DataFrame(), pd.DataFrame()

    print(f"Found {len(stats_files)} stats file(s) under {output_dir}")

    for fpath in stats_files:
        fname = fpath.name
        stats = load_stats(fpath)
        if not stats:
            continue

        row = extract_stats(stats)
        row["source_file"] = str(fpath)

        config_name = detect_config_name(fpath, output_dir)

        # ---- synthetic? ----
        m_syn = SYNTHETIC_RE.match(fname)
        if m_syn:
            row["matrix_size"] = int(m_syn.group(1))
            row["density_A"] = int(m_syn.group(2))
            row["density_B"] = int(m_syn.group(3))
            row["run"] = int(m_syn.group(4))

            if config_name:
                row["config_name"] = config_name
                ablation_rows.append(row.copy())
            else:
                synthetic_rows.append(row.copy())
            continue

        # ---- suitesparse (old format)? ----
        m_ss = SUITESPARSE_RE.match(fname)
        if m_ss:
            row["matrix_name"] = m_ss.group(1)
            if config_name:
                row["config_name"] = config_name
                ablation_rows.append(row.copy())
            else:
                suitesparse_rows.append(row.copy())
            continue

        # ---- suitesparse (sim_ format)? ----
        m_ss2 = SUITESPARSE_SIM_RE.match(fname)
        if m_ss2:
            row["matrix_name"] = m_ss2.group(1)
            if config_name:
                row["config_name"] = config_name
                ablation_rows.append(row.copy())
            else:
                suitesparse_rows.append(row.copy())
            continue

        # Unrecognised filename -- skip silently
        print(f"  Skipping unrecognised file: {fpath}", file=sys.stderr)

    df_syn = pd.DataFrame(synthetic_rows)
    df_ss = pd.DataFrame(suitesparse_rows)
    df_abl = pd.DataFrame(ablation_rows)

    # Sort for reproducibility
    if not df_syn.empty:
        df_syn.sort_values(
            ["matrix_size", "density_A", "density_B", "run"],
            inplace=True,
            ignore_index=True,
        )
    if not df_ss.empty:
        df_ss.sort_values(["matrix_name"], inplace=True, ignore_index=True)
    if not df_abl.empty:
        sort_cols = ["config_name"]
        if "matrix_size" in df_abl.columns:
            sort_cols += ["matrix_size", "density_A", "density_B"]
        if "matrix_name" in df_abl.columns:
            sort_cols += ["matrix_name"]
        existing = [c for c in sort_cols if c in df_abl.columns]
        df_abl.sort_values(existing, inplace=True, ignore_index=True)

    return df_syn, df_ss, df_abl


# ---------------------------------------------------------------------------
# Summary printer
# ---------------------------------------------------------------------------

def print_summary(df_syn: pd.DataFrame, df_ss: pd.DataFrame, df_abl: pd.DataFrame):
    """Print human-readable summary statistics."""
    sep = "=" * 72

    if not df_syn.empty:
        print(f"\n{sep}")
        print("SYNTHETIC RESULTS SUMMARY")
        print(sep)
        print(f"  Total records : {len(df_syn)}")
        if "cycle" in df_syn.columns:
            grp = df_syn.groupby("matrix_size")["cycle"]
            summary = grp.agg(["mean", "std", "min", "max"])
            print("\n  Cycles by matrix size:")
            print(summary.to_string(float_format="%.1f").replace("\n", "\n  "))
        if "avg_util" in df_syn.columns:
            grp = df_syn.groupby("matrix_size")["avg_util"]
            summary = grp.agg(["mean", "std", "min", "max"])
            print("\n  Utilization by matrix size:")
            print(summary.to_string(float_format="%.4f").replace("\n", "\n  "))

    if not df_ss.empty:
        print(f"\n{sep}")
        print("SUITESPARSE RESULTS SUMMARY")
        print(sep)
        print(f"  Total records : {len(df_ss)}")
        if "cycle" in df_ss.columns:
            print(f"  Mean cycles   : {df_ss['cycle'].mean():.1f}")
            print(f"  Mean util     : {df_ss['avg_util'].mean():.4f}")

    if not df_abl.empty:
        print(f"\n{sep}")
        print("ABLATION RESULTS SUMMARY")
        print(sep)
        print(f"  Total records : {len(df_abl)}")
        if "config_name" in df_abl.columns and "cycle" in df_abl.columns:
            grp = df_abl.groupby("config_name")
            for name, group in grp:
                mean_cyc = group["cycle"].mean()
                mean_util = group["avg_util"].mean() if "avg_util" in group.columns else float("nan")
                print(f"  [{name}]  records={len(group)}  "
                      f"mean_cycles={mean_cyc:.1f}  mean_util={mean_util:.4f}")

    print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Collect *_stats.json results into CSV tables."
    )
    parser.add_argument(
        "output_dir",
        type=str,
        help="Root directory to search for *_stats.json files.",
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    if not output_dir.is_dir():
        print(f"Error: {output_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    df_syn, df_ss, df_abl = collect(output_dir)

    # ---- Write CSVs ----
    written = []

    if not df_syn.empty:
        out_path = output_dir / "synthetic_results.csv"
        df_syn.to_csv(out_path, index=False)
        written.append(out_path)
        print(f"Wrote {len(df_syn)} rows -> {out_path}")

    if not df_ss.empty:
        out_path = output_dir / "suitesparse_results.csv"
        df_ss.to_csv(out_path, index=False)
        written.append(out_path)
        print(f"Wrote {len(df_ss)} rows -> {out_path}")

    if not df_abl.empty:
        out_path = output_dir / "ablation_results.csv"
        df_abl.to_csv(out_path, index=False)
        written.append(out_path)
        print(f"Wrote {len(df_abl)} rows -> {out_path}")

    if not written:
        print("No data collected -- nothing to write.")
    else:
        print_summary(df_syn, df_ss, df_abl)


if __name__ == "__main__":
    main()
