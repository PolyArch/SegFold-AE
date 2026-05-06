#!/usr/bin/env python3
"""
Collect experiment stats from JSON files into CSV tables.

Produces per-experiment CSV tables:
  - fig8_overall_results.csv           (Figure 8: overall performance)
  - fig9_nonsquare_results.csv         (Figure 9: non-square performance)
  - fig10_ablation_mapping_results.csv (Figure 10: mapping ablation)
  - fig11_breakdown_results.csv        (Figure 11: speedup breakdown)
  - ablation_{group}_results.csv       (window-size, crossbar-width, k-reordering)

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

# Regex for shape sensitivity filenames:
#   sim_<matrix>_d<density>_s<seed>_stats.json
SHAPE_SENS_RE = re.compile(
    r"sim_(?P<matrix>.+?)_d(?P<density>[\d.]+)_s(?P<seed>\d+)_stats\.json$"
)

# Regex for sparsity-mix filenames:
#   sim_K<K>_dA<dA>_dB<dB>_s<seed>_stats.json
SPARSITY_MIX_RE = re.compile(
    r"sim_K(?P<K>\d+)_dA(?P<dA>[\d.]+)_dB(?P<dB>[\d.]+)_s(?P<seed>\d+)_stats\.json$"
)

# (M, K) for the 6 nonsquare matrices (mirror of run_nonsquare.py:25-32).
SHAPE_SENS_MATRIX_DIMS = {
    "lp_woodw": (1098, 8418),
    "pcb3000":  (3960, 7732),
    "gemat1":   (4929, 10595),
    "Franz6":   (7576, 3016),
    "Franz8":   (16728, 7176),
    "psse1":    (14318, 11028),
}


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


def collect_shape_sensitivity_dir(shape_dir: Path) -> pd.DataFrame:
    """Collect stats from shape_sensitivity/<config>/dir{1,2}/sim_<matrix>_d<density>_s<seed>_stats.json."""
    rows = []
    for config_dir in sorted(shape_dir.iterdir()):
        if not config_dir.is_dir():
            continue
        config_name = config_dir.name
        for direction_dir in sorted(config_dir.iterdir()):
            if not direction_dir.is_dir() or not direction_dir.name.startswith("dir"):
                continue
            try:
                direction = int(direction_dir.name[3:])
            except ValueError:
                continue
            for fpath in sorted(direction_dir.glob("*_stats.json")):
                m = SHAPE_SENS_RE.match(fpath.name)
                if not m:
                    continue
                stats = load_stats(fpath)
                if not stats:
                    continue
                matrix = m.group("matrix")
                row = extract_stats(stats)
                row["matrix"] = matrix
                row["density"] = float(m.group("density"))
                row["seed"] = int(m.group("seed"))
                row["direction"] = direction
                row["direction_label"] = (
                    "A_real x S" if direction == 1 else "S x A_real^T"
                )
                row["config"] = config_name
                dims = SHAPE_SENS_MATRIX_DIMS.get(matrix)
                row["M"] = dims[0] if dims else None
                row["K"] = dims[1] if dims else None
                rows.append(row)
    if not rows:
        return pd.DataFrame()
    df = pd.DataFrame(rows)
    front = ["matrix", "M", "K", "density", "seed", "direction",
             "direction_label", "config"]
    cols = front + [c for c in df.columns if c not in front]
    return df[cols]


def collect_sparsity_mix_dir(mix_dir: Path) -> pd.DataFrame:
    """Collect stats from sparsity_mix/<config>/sim_K<K>_dA<dA>_dB<dB>_s<seed>_stats.json."""
    rows = []
    for config_dir in sorted(mix_dir.iterdir()):
        if not config_dir.is_dir():
            continue
        config_name = config_dir.name
        for fpath in sorted(config_dir.glob("*_stats.json")):
            m = SPARSITY_MIX_RE.match(fpath.name)
            if not m:
                continue
            stats = load_stats(fpath)
            if not stats:
                continue
            row = extract_stats(stats)
            row["K"] = int(m.group("K"))
            row["density_A"] = float(m.group("dA"))
            row["density_B"] = float(m.group("dB"))
            row["seed"] = int(m.group("seed"))
            row["config"] = config_name
            rows.append(row)
    if not rows:
        return pd.DataFrame()
    df = pd.DataFrame(rows)
    front = ["K", "density_A", "density_B", "seed", "config"]
    cols = front + [c for c in df.columns if c not in front]
    return df[cols]


def collect_paper_results(output_dir: Path):
    """Collect results for paper experiments: overall, nonsquare, breakdown."""
    written = []

    # Overall performance
    overall_dir = output_dir / "overall"
    if overall_dir.is_dir():
        df = collect_experiment_dir(overall_dir)
        if not df.empty:
            out_path = output_dir / "fig8_overall_results.csv"
            df.to_csv(out_path, index=False)
            written.append(out_path)

        # Ideal SegFold variant (if --include-ideal was used)
        ideal_dir = overall_dir / "ideal"
        if ideal_dir.is_dir():
            ideal_df = collect_experiment_dir(ideal_dir)
            if not ideal_df.empty:
                out_path = output_dir / "fig8_overall_ideal_results.csv"
                ideal_df.to_csv(out_path, index=False)
                written.append(out_path)
                print(f"Wrote {len(ideal_df)} rows -> {out_path}")

    # Non-square performance
    nonsquare_dir = output_dir / "nonsquare"
    if nonsquare_dir.is_dir():
        df = collect_experiment_dir(nonsquare_dir)
        if not df.empty:
            out_path = output_dir / "fig9_nonsquare_results.csv"
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

            out_path = output_dir / "fig11_breakdown_results.csv"
            pivot.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(pivot)} rows -> {out_path}")

    # Shape sensitivity (per-direction operand-role sweep)
    shape_dir = output_dir / "shape_sensitivity"
    if shape_dir.is_dir():
        df = collect_shape_sensitivity_dir(shape_dir)
        if not df.empty:
            out_path = output_dir / "shape_sensitivity_results.csv"
            df.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(df)} rows -> {out_path}")

    # Sparsity-mix (square synthetic, full d_A x d_B grid)
    mix_dir = output_dir / "sparsity_mix"
    if mix_dir.is_dir():
        df = collect_sparsity_mix_dir(mix_dir)
        if not df.empty:
            out_path = output_dir / "sparsity_mix_results.csv"
            df.to_csv(out_path, index=False)
            written.append(out_path)
            print(f"Wrote {len(df)} rows -> {out_path}")

    # Ablation studies
    ablation_dir = output_dir / "ablation"
    # Groups that run on SuiteSparse matrices (pivoted to {config}_cycles columns)
    suitesparse_groups = {"mapping-paper"}
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
                    out_path = output_dir / "fig10_ablation_mapping_results.csv"
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
                    # Map group names to figure-numbered filenames
                    group_csv_names = {
                        "crossbar-width": "fig12a_ablation_crossbar_width_results.csv",
                        "window-size": "fig12b_ablation_window_size_results.csv",
                        "k-reordering": "tab4_k_reordering_results.csv",
                    }
                    csv_name = group_csv_names.get(group_name, f"ablation_{group_name}_results.csv")
                    out_path = output_dir / csv_name
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
