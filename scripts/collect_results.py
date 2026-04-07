#!/usr/bin/env python3
"""
Collect experiment stats from JSON files into CSV tables.

Produces per-experiment CSV tables:
  - overall_results.csv
  - nonsquare_results.csv
  - breakdown_results.csv
  - ablation_mapping_suitesparse_results.csv
  - ablation_mapping_suitesparse_nomem_results.csv
  - ablation_{group}_results.csv  (window-size, crossbar-width, k-reordering)

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

# Regex for suitesparse filenames (sim_ prefix):
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


# ---------------------------------------------------------------------------
# Paper experiment collector (overall, nonsquare, breakdown, ablation)
# ---------------------------------------------------------------------------

EXPERIMENT_DIRS = {"overall", "nonsquare", "suitesparse"}

def collect_experiment_dir(exp_dir: Path) -> pd.DataFrame:
    """Collect all stats from a flat experiment directory (overall/ or nonsquare/)."""
    rows = []
    for fpath in sorted(exp_dir.glob("*_stats.json")):
        stats = load_stats(fpath)
        if not stats:
            continue
        m = SUITESPARSE_SIM_RE.match(fpath.name)
        if m:
            row = extract_stats(stats)
            row["matrix"] = m.group(1)
            rows.append(row)
    return pd.DataFrame(rows)


def collect_breakdown_dir(breakdown_dir: Path) -> pd.DataFrame:
    """Collect stats from breakdown/{config}/sim_{matrix}_stats.json."""
    rows = []
    for config_dir in sorted(breakdown_dir.iterdir()):
        if not config_dir.is_dir():
            continue
        config_name = config_dir.name
        for fpath in sorted(config_dir.glob("*_stats.json")):
            stats = load_stats(fpath)
            if not stats:
                continue
            m = SUITESPARSE_SIM_RE.match(fpath.name)
            if m:
                row = extract_stats(stats)
                row["matrix"] = m.group(1)
                row["config"] = config_name
                rows.append(row)
    return pd.DataFrame(rows)


def collect_paper_results(output_dir: Path):
    """Collect results for paper experiments: overall, nonsquare, breakdown."""
    written = []

    # Overall performance
    overall_dir = output_dir / "overall"
    if overall_dir.is_dir():
        df = collect_experiment_dir(overall_dir)
        if not df.empty:
            out_path = output_dir / "overall_results.csv"
            df.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(df)} rows -> {out_path}")

    # Non-square performance
    nonsquare_dir = output_dir / "nonsquare"
    if nonsquare_dir.is_dir():
        df = collect_experiment_dir(nonsquare_dir)
        if not df.empty:
            out_path = output_dir / "nonsquare_results.csv"
            df.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(df)} rows -> {out_path}")

    # Breakdown (incremental ablation)
    breakdown_dir = output_dir / "breakdown"
    if breakdown_dir.is_dir():
        df = collect_breakdown_dir(breakdown_dir)
        if not df.empty:
            # Pivot: one row per matrix, columns = config cycles
            config_order = [
                "breakdown-base", "breakdown-plus-tiling",
                "breakdown-plus-folding", "breakdown-plus-dynmap", "segfold",
            ]
            pivot = df.pivot_table(
                index="matrix", columns="config", values="cycle", aggfunc="first"
            )
            # Reorder columns to match incremental order
            ordered_cols = [c for c in config_order if c in pivot.columns]
            pivot = pivot[ordered_cols].reset_index()
            # Rename columns for readability
            col_map = {
                "breakdown-base": "base_cycles",
                "breakdown-plus-tiling": "+tiling_cycles",
                "breakdown-plus-folding": "+folding_cycles",
                "breakdown-plus-dynmap": "+dynmap_cycles",
                "segfold": "full_cycles",
            }
            pivot.rename(columns=col_map, inplace=True)

            out_path = output_dir / "breakdown_results.csv"
            pivot.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(pivot)} rows -> {out_path}")

    # Ablation studies
    ablation_dir = output_dir / "ablation"
    # Groups that run on SuiteSparse matrices (pivoted to {config}_cycles columns)
    suitesparse_groups = {"mapping-paper", "mapping-paper-nomem"}
    if ablation_dir.is_dir():
        for group_dir in sorted(ablation_dir.iterdir()):
            if not group_dir.is_dir():
                continue
            group_name = group_dir.name

            if group_name in suitesparse_groups:
                # SuiteSparse ablation: pivot into matrix x {config}_cycles
                rows_by_matrix = {}
                for config_dir in sorted(group_dir.iterdir()):
                    if not config_dir.is_dir():
                        continue
                    config_name = config_dir.name
                    for fpath in sorted(config_dir.glob("sim_*_stats.json")):
                        stats = load_stats(fpath)
                        if not stats:
                            continue
                        m_ss = SUITESPARSE_SIM_RE.match(fpath.name)
                        if not m_ss:
                            continue
                        matrix = m_ss.group(1)
                        if matrix not in rows_by_matrix:
                            rows_by_matrix[matrix] = {"matrix": matrix}
                        rows_by_matrix[matrix][f"{config_name}_cycles"] = stats.get("cycle")
                        rows_by_matrix[matrix][f"{config_name}_util"] = stats.get("avg_util")
                if rows_by_matrix:
                    df = pd.DataFrame(list(rows_by_matrix.values()))
                    # Compute speedup_vs_zero if zero_cycles exists
                    if "zero_cycles" in df.columns and "segfold_cycles" in df.columns:
                        df["speedup_vs_zero"] = (
                            df["zero_cycles"] / df["segfold_cycles"]
                        ).round(2)
                    if group_name == "mapping-paper":
                        out_name = "ablation_mapping_suitesparse_results.csv"
                    else:
                        out_name = "ablation_mapping_suitesparse_nomem_results.csv"
                    out_path = output_dir / out_name
                    df.to_csv(out_path, index=False)
                    written.append(out_path)
                    print(f"Wrote {len(df)} rows -> {out_path}")
            else:
                # Synthetic ablation
                rows = []
                for config_dir in sorted(group_dir.iterdir()):
                    if not config_dir.is_dir():
                        continue
                    config_name = config_dir.name
                    for fpath in sorted(config_dir.glob("sim_*_stats.json")):
                        stats = load_stats(fpath)
                        if not stats:
                            continue
                        row = extract_stats(stats)
                        row["config"] = config_name
                        # Parse synthetic run ID: sim_s256_dA5_dB5_r0_stats.json
                        m = SYNTHETIC_RE.match(fpath.name)
                        if m:
                            row["matrix_size"] = int(m.group(1))
                            row["density_A"] = int(m.group(2))
                            row["density_B"] = int(m.group(3))
                            row["run"] = int(m.group(4))
                        rows.append(row)
                if rows:
                    df = pd.DataFrame(rows)
                    out_path = output_dir / f"ablation_{group_name}_results.csv"
                    df.to_csv(out_path, index=False)
                    written.append(out_path)
                    print(f"Wrote {len(df)} rows -> {out_path}")

    return written


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

    written = []

    # Collect paper experiment results (overall, nonsquare, breakdown)
    written += collect_paper_results(output_dir)

    if not written:
        print("No data collected -- nothing to write.")
    else:
        print(f"\nCollected {len(written)} CSV file(s).")


if __name__ == "__main__":
    main()
