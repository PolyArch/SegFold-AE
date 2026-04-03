#!/usr/bin/env python3
"""Plot speedup breakdown: stacked bars showing incremental feature contribution.

Reads breakdown_results.csv with columns:
    matrix, base_cycles, +tiling_cycles, +folding_cycles, +dynmap_cycles, full_cycles

Each matrix gets one stacked bar. Reference (1.0x) is base_cycles when > 0,
otherwise +tiling_cycles. Segments show incremental speedup.

Usage:
    python3 scripts/plot_breakdown.py OUTPUT_DIR
"""

import argparse
import os
import sys

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

Y_MAX = 3.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", help="Directory with breakdown_results.csv")
    args = parser.parse_args()

    out_dir = os.path.abspath(args.output_dir)
    csv_path = os.path.join(out_dir, "breakdown_results.csv")

    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run collect_results.py first.")
        sys.exit(1)

    df = pd.read_csv(csv_path)
    df.columns = df.columns.str.strip()

    # Need at least +tiling through full > 0
    needed = ["+tiling_cycles", "+folding_cycles", "+dynmap_cycles", "full_cycles"]
    mask = (df[needed] > 0).all(axis=1)
    df = df[mask].copy()

    if df.empty:
        print("ERROR: no valid rows after filtering")
        return

    n = len(df)
    base = df["base_cycles"].values.astype(float)
    tiling = df["+tiling_cycles"].values.astype(float)
    folding = df["+folding_cycles"].values.astype(float)
    dynmap = df["+dynmap_cycles"].values.astype(float)
    full = df["full_cycles"].values.astype(float)

    # Reference: base_cycles if > 0, else +tiling_cycles
    ref = np.where(base > 0, base, tiling)

    # Speedup = ref / config_cycles
    su_tiling = ref / tiling
    su_folding = ref / folding
    su_dynmap = ref / dynmap
    su_full = ref / full

    has_base = base > 0

    # Segments (incremental)
    seg_base = np.ones(n)
    seg_tiling = np.where(has_base, su_tiling - 1.0, 0.0)
    seg_folding = su_folding - su_tiling
    seg_dynmap = su_dynmap - su_folding
    seg_window = su_full - su_dynmap

    segments = np.array([seg_base, seg_tiling, seg_folding, seg_dynmap, seg_window])

    labels_colors = [
        ("Base",               "#B5D99C"),
        ("+ SegmentBC",        "#F4A7BB"),
        ("+ Spatial Folding",  "#BBDEFB"),
        ("+ IPM LUT",          "#FFD54F"),
        ("+ SelectA",          "#E57373"),
    ]

    # Sort by full speedup descending
    order = np.argsort(-su_full)
    matrices = [df["matrix"].tolist()[i] for i in order]
    segments = segments[:, order]
    su_full = su_full[order]
    has_base = has_base[order]

    n = len(matrices)
    fig, ax = plt.subplots(figsize=(max(14, n * 1.2), 7))
    x = np.arange(n)
    width = 0.6

    bottom = np.zeros(n)
    for i, (label, color) in enumerate(labels_colors):
        visible = np.minimum(segments[i], Y_MAX - bottom)
        visible = np.maximum(visible, 0)
        ax.bar(x, visible, width, bottom=bottom, label=label, color=color,
               edgecolor="black", linewidth=0.5)
        bottom += segments[i]

    # Label total speedup
    for j in range(n):
        suffix = "" if has_base[j] else "*"
        y_pos = min(su_full[j], Y_MAX) + 0.05
        ax.text(x[j], y_pos, f"{su_full[j]:.1f}x{suffix}",
                ha="center", va="bottom", fontsize=10, clip_on=False)

    ax.axhline(y=1.0, color="gray", linestyle="--", linewidth=0.5)
    ax.grid(axis="y", alpha=0.3)
    ax.set_ylim(0, Y_MAX)
    ax.set_xlim(-0.5, n - 0.5)
    ax.set_xticks(x)
    ax.set_xticklabels(matrices, rotation=45, ha="right", fontsize=18)
    ax.set_ylabel("speedup", fontsize=20)

    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles[::-1], labels[::-1], fontsize=18,
              loc="upper center", ncol=5, framealpha=0.95, edgecolor="gray",
              handlelength=1.5, columnspacing=1.0,
              bbox_to_anchor=(0.5, 1.22))

    ax.annotate("* base timed out; uses +tiling as 1.0x reference",
                xy=(0.01, 0.01), xycoords="axes fraction", fontsize=9,
                fontstyle="italic", color="gray")

    plt.tight_layout()

    plots_dir = os.path.join(out_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    for ext in [".pdf", ".png"]:
        path = os.path.join(plots_dir, f"breakdown_speedup{ext}")
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
