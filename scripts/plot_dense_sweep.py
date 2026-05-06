#!/usr/bin/env python3
"""Plot dense-sweep cycle/MAC versus sparsity.

Reads dense_sweep.csv with per-seed cycle/MAC values. For each accelerator,
values are averaged across seeds at each (size, density), then geometrically
averaged across sizes for each density.

Usage:
    python3 scripts/plot_dense_sweep.py expected_results/data/dense_sweep.csv
    python3 scripts/plot_dense_sweep.py expected_results/data --output-dir expected_results/plots
"""

import argparse
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif", "Times"],
    "mathtext.fontset": "dejavuserif",
    "font.size": 13,
})

PROJECT_ROOT = Path(__file__).resolve().parent.parent

ACCELS = [
    ("segfold_cyc_per_mac", "SegFold (Ours)", "#42A5F5", "o"),
    ("spada_cyc_per_mac", "Spada", "#B5D99C", "s"),
    ("flex_op_cyc_per_mac", "Flexagon (OP only)", "#E57373", "^"),
    ("flex_gust_cyc_per_mac", "Flexagon (Gust only)", "#FFD54F", "D"),
]


def geomean(values):
    vals = pd.Series(values).dropna().astype(float)
    vals = vals[vals > 0]
    if vals.empty:
        return np.nan
    return float(np.exp(np.log(vals).mean()))


def resolve_csv_path(path_arg):
    path = Path(path_arg)
    if path.is_dir():
        path = path / "dense_sweep.csv"
    return path


def load_dense_sweep(csv_path):
    df = pd.read_csv(csv_path)
    df.columns = df.columns.str.strip()

    required = {"size", "density", "seed"}
    missing = sorted(required - set(df.columns))
    metric_cols = [col for col, _, _, _ in ACCELS if col in df.columns]
    if missing or not metric_cols:
        problems = []
        if missing:
            problems.append(f"missing columns: {', '.join(missing)}")
        if not metric_cols:
            problems.append("no *_cyc_per_mac accelerator columns found")
        raise ValueError("; ".join(problems))

    df = df.copy()
    for col in ["size", "density", "seed"] + metric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=["size", "density", "seed"])

    # First average replicated seeds for each size/sparsity point.
    seed_mean = (
        df.groupby(["size", "density"], as_index=False)[metric_cols]
        .mean(numeric_only=True)
    )

    # Then geomean across sizes for each sparsity point.
    rows = []
    for density, group in seed_mean.groupby("density", sort=True):
        row = {"density": float(density), "num_sizes": int(group["size"].nunique())}
        for col in metric_cols:
            row[col] = geomean(group[col])
        rows.append(row)

    summary = pd.DataFrame(rows).sort_values("density").reset_index(drop=True)
    return summary, metric_cols


def format_density_tick(value):
    if value >= 1.0:
        return "1.0"
    return f"{value:g}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "input",
        nargs="?",
        default=PROJECT_ROOT / "expected_results" / "data" / "dense_sweep.csv",
        help="dense_sweep.csv path, or a directory containing it.",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Directory for plots. Default: sibling ../plots for input data dir.",
    )
    parser.add_argument(
        "--output-name",
        default="dense_sweep_cyc_per_mac",
        help="Output file base name without extension.",
    )
    args = parser.parse_args()

    csv_path = resolve_csv_path(args.input).resolve()
    if not csv_path.exists():
        print(f"Error: {csv_path} not found.", file=sys.stderr)
        sys.exit(1)

    if args.output_dir:
        plots_dir = Path(args.output_dir).resolve()
    else:
        if csv_path.parent.name == "data":
            plots_dir = csv_path.parent.parent / "plots"
        else:
            plots_dir = csv_path.parent / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    summary, metric_cols = load_dense_sweep(csv_path)
    if summary.empty:
        print("Error: no valid dense-sweep rows after filtering.", file=sys.stderr)
        sys.exit(1)

    x = summary["density"].to_numpy(dtype=float)

    fig, ax = plt.subplots(figsize=(5.8, 5.0))

    present = [(col, label, color, marker) for col, label, color, marker in ACCELS
               if col in metric_cols and summary[col].notna().any()]
    for col, label, color, marker in present:
        ax.plot(
            x, summary[col].to_numpy(dtype=float),
            label=label,
            color=color,
            marker=marker,
            markersize=6,
            markeredgecolor="black",
            markeredgewidth=0.5,
            linewidth=2.0,
        )

    ax.set_xscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([format_density_tick(v) for v in x], rotation=0)
    ax.set_xlabel("sparsity")
    ax.set_ylabel("cycle/MAC")
    ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)
    ax.grid(axis="x", which="major", alpha=0.12)
    ax.set_xlim(x.min() * 0.9, x.max() * 1.08)

    plotted = summary[[col for col, _, _, _ in present]].to_numpy(dtype=float)
    plotted = plotted[np.isfinite(plotted) & (plotted > 0)]
    if plotted.size:
        ax.set_ylim(plotted.min() * 0.85, plotted.max() * 1.18)

    ax.legend(
        loc="upper center",
        fontsize=11,
        ncol=2,
        framealpha=0.95,
        edgecolor="gray",
        handlelength=1.8,
        columnspacing=1.0,
        bbox_to_anchor=(0.5, 1.25),
    )

    plt.tight_layout()
    for ext in [".pdf", ".png"]:
        path = plots_dir / f"{args.output_name}{ext}"
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
