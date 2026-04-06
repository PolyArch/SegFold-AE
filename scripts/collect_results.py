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

    # Skip paper experiment directories (handled by collect_paper_results)
    paper_dirs = {"overall", "nonsquare", "breakdown"}
    stats_files = sorted(
        f for f in output_dir.rglob("*_stats.json")
        if not any(p in f.relative_to(output_dir).parts for p in paper_dirs)
    )
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
# Paper experiment collector (overall, nonsquare, breakdown)
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
                    suffix = "_nomem" if "nomem" in group_name else ""
                    out_path = output_dir / f"mapping_ablation_suitesparse{suffix}.csv"
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

    # Also collect legacy experiment results (synthetic, suitesparse, ablation)
    df_syn, df_ss, df_abl = collect(output_dir)

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
