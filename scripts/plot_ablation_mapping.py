#!/usr/bin/env python3
"""Plot ablation mapping on SuiteSparse matrices.

Produces a bar chart showing speedup of each mapping strategy
normalized to Zero-Offset, with optional side-by-side panels for
with-memory and no-memory runs.

Usage:
    python3 scripts/plot_ablation_mapping.py
    python3 scripts/plot_ablation_mapping.py --output output/plots/ablation_mapping.pdf
    python3 scripts/plot_ablation_mapping.py \
        --mem-csv output/ablation_mapping_suitesparse_results.csv \
        --nomem-csv output/ablation_mapping_suitesparse_nomem_results.csv
"""

import argparse
import os

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

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Mapping strategies: (csv_column_prefix, label, color)
STRATEGIES = [
    ("zero",    "Zero-Offset",        "#FFD54F"),
    ("ideal",   "Ideal-Network",      "#F4A7BB"),
    ("segfold", "SegFold (Ours)",     "#BBDEFB"),
]


def load_and_compute(csv_path):
    """Load CSV and compute speedup normalized to zero_cycles."""
    df = pd.read_csv(csv_path)
    exclude = {"Franz6", "Franz8", "psse1", "gemat1"}
    df = df[~df["matrix"].isin(exclude)].reset_index(drop=True)
    matrices = df["matrix"].tolist()

    speedups = {}
    valid_idx = []
    for i, row in df.iterrows():
        base = row["zero_cycles"]
        if pd.isna(base) or base <= 0:
            continue
        valid_idx.append(i)
        for prefix, _, _ in STRATEGIES:
            col = f"{prefix}_cycles"
            v = row.get(col)
            if pd.notna(v) and v > 0:
                speedups.setdefault(prefix, []).append(base / v)
            else:
                speedups.setdefault(prefix, []).append(0)

    valid_matrices = [matrices[i] for i in valid_idx]

    # Geomean
    for prefix, _, _ in STRATEGIES:
        vals = [v for v in speedups.get(prefix, []) if v > 0]
        geo = np.exp(np.mean(np.log(vals))) if vals else 0
        speedups.setdefault(prefix, []).append(geo)
    valid_matrices.append("GeoMean")

    return valid_matrices, speedups


def plot_panel(ax, matrices, speedups, title, y_max):
    """Draw one subplot panel."""
    n = len(matrices)
    present = [(p, l, c) for p, l, c in STRATEGIES
               if any(v > 0 for v in speedups.get(p, []))]
    n_strats = len(present)
    x = np.arange(n)
    bar_width = 0.8 / n_strats

    for j, (prefix, label, color) in enumerate(present):
        offset = (j - n_strats / 2 + 0.5) * bar_width
        vals = speedups[prefix]
        bars = ax.bar(x + offset, vals, bar_width,
                      label=label, color=color,
                      edgecolor="black", linewidth=0.5)
        # Bold border on GeoMean bar
        bars[-1].set_edgecolor("black")
        bars[-1].set_linewidth(1.5)

    # SegFold value labels
    if "segfold" in speedups:
        sf_idx = next(j for j, (p, _, _) in enumerate(present) if p == "segfold")
        sf_offset = (sf_idx - n_strats / 2 + 0.5) * bar_width
        for i, v in enumerate(speedups["segfold"]):
            if v <= 0:
                continue
            fw = "bold" if i == n - 1 else "normal"
            if v > y_max:
                ax.text(x[i] + sf_offset, y_max + 0.02, f"{v:.2f}x",
                        ha="center", va="bottom", fontsize=8, fontweight=fw,
                        clip_on=False)
            else:
                ax.text(x[i] + sf_offset, v + 0.02, f"{v:.2f}x",
                        ha="center", va="bottom", fontsize=8, fontweight=fw)

    ax.axvline(x=n - 1.5, color="gray", linestyle="-", linewidth=0.8)
    ax.axhline(y=1, color="gray", linestyle="--", linewidth=0.5)
    ax.grid(axis="y", alpha=0.3)
    ax.set_xlim(-0.6, n - 0.4)
    ax.set_ylim(0, y_max)
    ax.set_ylabel("Speedup (vs Zero-Offset)", fontsize=12)
    ax.set_title(title, fontsize=12)
    ax.set_xticks(x)
    ax.set_xticklabels(matrices, rotation=30, ha="right", fontsize=9)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mem-csv", default=None,
                        help="Path to ablation_mapping_suitesparse.csv")
    parser.add_argument("--nomem-csv", default=None,
                        help="Path to ablation_mapping_suitesparse_nomem_results.csv")
    parser.add_argument("--output", default=None)
    args = parser.parse_args()

    mem_csv = args.mem_csv or os.path.join(
        PROJECT_ROOT, "output", "ablation_mapping_suitesparse_results.csv")
    nomem_csv = args.nomem_csv
    out_path = args.output or os.path.join(
        PROJECT_ROOT, "output", "plots", "ablation_mapping.pdf")

    mem_matrices, mem_speedups = load_and_compute(mem_csv)
    nomem_data = None
    if nomem_csv and os.path.exists(nomem_csv):
        nomem_data = load_and_compute(nomem_csv)

    y_max = 1.75

    if nomem_data:
        fig, axes = plt.subplots(1, 2, figsize=(16, 4.5), sharey=True)
        plot_panel(axes[0], mem_matrices, mem_speedups, "With Memory Hierarchy", y_max)
        nomem_matrices, nomem_speedups = nomem_data
        plot_panel(axes[1], nomem_matrices, nomem_speedups, "Without Memory Hierarchy", y_max)
        handles, labels = axes[0].get_legend_handles_labels()
    else:
        fig, ax = plt.subplots(figsize=(12, 4.5))
        plot_panel(ax, mem_matrices, mem_speedups, "", y_max)
        handles, labels = ax.get_legend_handles_labels()

    # Legend at top
    fig.legend(handles, labels, loc="upper center", fontsize=11, ncol=len(STRATEGIES),
               framealpha=0.95, edgecolor="gray",
               handlelength=1.5, columnspacing=1.0,
                bbox_to_anchor=(0.5, 1.05))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    plt.tight_layout()
    fig.savefig(out_path, dpi=200, bbox_inches="tight")
    print(f"Saved: {out_path}")

    if out_path.endswith(".pdf"):
        png_path = out_path.replace(".pdf", ".png")
        fig.savefig(png_path, dpi=200, bbox_inches="tight")
        print(f"Saved: {png_path}")

    plt.close()


if __name__ == "__main__":
    main()
