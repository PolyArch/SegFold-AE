#!/usr/bin/env python3
"""
Generate publication-quality plots from the CSV tables produced by
collect_results.py.

Reads:
    OUTPUT_DIR/synthetic_results.csv
    OUTPUT_DIR/suitesparse_results.csv
    OUTPUT_DIR/ablation_results.csv

Saves plots (PDF + PNG) to OUTPUT_DIR/plots/.

Usage:
    python3 scripts/plot_results.py OUTPUT_DIR
"""

import argparse
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # non-interactive backend
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# ---------------------------------------------------------------------------
# Style
# ---------------------------------------------------------------------------
_STYLE_APPLIED = False

def _apply_style():
    global _STYLE_APPLIED
    if _STYLE_APPLIED:
        return
    # Try seaborn-v0_8 first (matplotlib >= 3.6), fall back gracefully.
    for style in ("seaborn-v0_8", "seaborn", "seaborn-whitegrid"):
        try:
            plt.style.use(style)
            break
        except OSError:
            continue
    plt.rcParams.update({
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "font.size": 11,
        "axes.titlesize": 13,
        "axes.labelsize": 12,
        "legend.fontsize": 9,
        "figure.figsize": (8, 5),
    })
    _STYLE_APPLIED = True


def _save(fig, plot_dir: Path, name: str):
    """Save figure as both PDF and PNG."""
    fig.savefig(plot_dir / f"{name}.pdf", bbox_inches="tight")
    fig.savefig(plot_dir / f"{name}.png", bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved {name}.pdf / .png")


# ---------------------------------------------------------------------------
# Plot functions
# ---------------------------------------------------------------------------

def plot_cycles_vs_density(df: pd.DataFrame, plot_dir: Path):
    """
    Line plot of cycle count vs density_B for each matrix size,
    with error bars across runs.  Fixed density_A grouping.
    """
    if df.empty or "cycle" not in df.columns:
        return

    sizes = sorted(df["matrix_size"].unique())
    densities_a = sorted(df["density_A"].unique())

    for da in densities_a:
        sub = df[df["density_A"] == da]
        if sub.empty:
            continue

        fig, ax = plt.subplots()
        for size in sizes:
            part = sub[sub["matrix_size"] == size]
            if part.empty:
                continue
            grp = part.groupby("density_B")["cycle"].agg(["mean", "std"]).reset_index()
            grp.sort_values("density_B", inplace=True)
            ax.errorbar(
                grp["density_B"], grp["mean"], yerr=grp["std"],
                marker="o", capsize=3, label=f"size={size}",
            )

        ax.set_xlabel("Density B (%)")
        ax.set_ylabel("Cycles")
        ax.set_title(f"Cycle Count vs Density B  (density_A={da}%)")
        ax.legend()
        ax.grid(True, linestyle="--", alpha=0.5)
        _save(fig, plot_dir, f"cycles_vs_densityB_dA{da}")


def plot_util_vs_density(df: pd.DataFrame, plot_dir: Path):
    """
    Line plot of average utilization vs density_B, per matrix size.
    """
    if df.empty or "avg_util" not in df.columns:
        return

    sizes = sorted(df["matrix_size"].unique())
    densities_a = sorted(df["density_A"].unique())

    for da in densities_a:
        sub = df[df["density_A"] == da]
        if sub.empty:
            continue

        fig, ax = plt.subplots()
        for size in sizes:
            part = sub[sub["matrix_size"] == size]
            if part.empty:
                continue
            grp = part.groupby("density_B")["avg_util"].agg(["mean", "std"]).reset_index()
            grp.sort_values("density_B", inplace=True)
            ax.errorbar(
                grp["density_B"], grp["mean"], yerr=grp["std"],
                marker="s", capsize=3, label=f"size={size}",
            )

        ax.set_xlabel("Density B (%)")
        ax.set_ylabel("Avg Utilization")
        ax.set_title(f"Utilization vs Density B  (density_A={da}%)")
        ax.legend()
        ax.grid(True, linestyle="--", alpha=0.5)
        _save(fig, plot_dir, f"util_vs_densityB_dA{da}")


def plot_ablation_comparison(df: pd.DataFrame, plot_dir: Path):
    """
    Grouped bar chart: cycles by config, grouped by density combination.
    If the ablation data has suitesparse matrices instead of densities,
    group by matrix_name.
    """
    if df.empty or "config_name" not in df.columns or "cycle" not in df.columns:
        return

    configs = sorted(df["config_name"].unique())
    n_configs = len(configs)
    if n_configs == 0:
        return

    # Determine grouping key
    has_density = "density_A" in df.columns and "density_B" in df.columns
    has_matrix = "matrix_name" in df.columns

    if has_density:
        df = df.copy()
        df["group_label"] = (
            "s" + df["matrix_size"].astype(str)
            + "_dA" + df["density_A"].astype(str)
            + "_dB" + df["density_B"].astype(str)
        )
        group_col = "group_label"
    elif has_matrix:
        group_col = "matrix_name"
    else:
        # Fallback: single group
        df = df.copy()
        df["group_label"] = "all"
        group_col = "group_label"

    groups = sorted(df[group_col].unique())
    n_groups = len(groups)

    # Compute means
    means = df.groupby([group_col, "config_name"])["cycle"].mean().unstack(fill_value=0)

    fig, ax = plt.subplots(figsize=(max(8, n_groups * 1.2), 5))
    x = np.arange(n_groups)
    width = 0.8 / max(n_configs, 1)
    colors = plt.colormaps.get_cmap("tab10")

    for i, cfg in enumerate(configs):
        vals = [means.loc[g, cfg] if g in means.index and cfg in means.columns else 0
                for g in groups]
        offset = (i - (n_configs - 1) / 2) * width
        ax.bar(x + offset, vals, width=width, label=cfg,
               color=colors(i % 10))

    ax.set_xticks(x)
    ax.set_xticklabels(groups, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("Cycles (mean)")
    ax.set_title("Ablation Comparison: Cycles by Configuration")
    ax.legend(title="Config")
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    fig.tight_layout()
    _save(fig, plot_dir, "ablation_comparison")


def plot_suitesparse_results(df: pd.DataFrame, plot_dir: Path):
    """
    Bar chart: cycles per SuiteSparse matrix.
    """
    if df.empty or "matrix_name" not in df.columns or "cycle" not in df.columns:
        return

    grp = df.groupby("matrix_name")["cycle"].mean().sort_values()

    fig, ax = plt.subplots(figsize=(max(8, len(grp) * 0.6), 5))
    x = np.arange(len(grp))
    bars = ax.bar(x, grp.values, color="steelblue", edgecolor="white", linewidth=0.5)

    ax.set_xticks(x)
    ax.set_xticklabels(grp.index, rotation=55, ha="right", fontsize=8)
    ax.set_ylabel("Cycles")
    ax.set_title("SuiteSparse Results: Cycles per Matrix")
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    # Value labels on top of short bars
    max_val = grp.values.max() if len(grp) > 0 else 1
    for bar, val in zip(bars, grp.values):
        if val < max_val * 0.3:
            ax.text(bar.get_x() + bar.get_width() / 2, val + max_val * 0.01,
                    f"{val:.0f}", ha="center", va="bottom", fontsize=7)

    fig.tight_layout()
    _save(fig, plot_dir, "suitesparse_cycles")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate plots from collected CSV results."
    )
    parser.add_argument(
        "output_dir",
        type=str,
        help="Directory containing the CSVs produced by collect_results.py.",
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    if not output_dir.is_dir():
        print(f"Error: {output_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    _apply_style()

    plot_dir = output_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)

    generated_any = False

    # ---- Synthetic ----
    syn_csv = output_dir / "synthetic_results.csv"
    if syn_csv.exists():
        print(f"Reading {syn_csv}")
        df_syn = pd.read_csv(syn_csv)
        plot_cycles_vs_density(df_syn, plot_dir)
        plot_util_vs_density(df_syn, plot_dir)
        generated_any = True
    else:
        print(f"Skipping synthetic plots ({syn_csv} not found)")

    # ---- SuiteSparse ----
    ss_csv = output_dir / "suitesparse_results.csv"
    if ss_csv.exists():
        print(f"Reading {ss_csv}")
        df_ss = pd.read_csv(ss_csv)
        plot_suitesparse_results(df_ss, plot_dir)
        generated_any = True
    else:
        print(f"Skipping SuiteSparse plots ({ss_csv} not found)")

    # ---- Ablation ----
    abl_csv = output_dir / "ablation_results.csv"
    if abl_csv.exists():
        print(f"Reading {abl_csv}")
        df_abl = pd.read_csv(abl_csv)
        plot_ablation_comparison(df_abl, plot_dir)
        generated_any = True
    else:
        print(f"Skipping ablation plots ({abl_csv} not found)")

    if generated_any:
        print(f"\nAll plots saved to {plot_dir}")
    else:
        print("\nNo CSV data found -- no plots generated.")


if __name__ == "__main__":
    main()
