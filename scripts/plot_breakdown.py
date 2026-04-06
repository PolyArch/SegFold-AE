#!/usr/bin/env python3
"""Plot speedup breakdown: stacked bars showing incremental feature contribution.

Reads breakdown_results.csv with columns:
    matrix, base_cycles, +tiling_cycles, +folding_cycles, +dynmap_cycles, full_cycles

Each matrix gets one stacked bar. Reference (1.0x) is base_cycles when > 0
and SegmentBC helps, otherwise +tiling_cycles. Segments show incremental speedup.

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

    # Reference: base_cycles when > 0 and SegmentBC helps; otherwise tiling_cycles
    has_base = base > 0
    segbc_hurts = has_base & (tiling > base)
    ref = np.where(has_base & ~segbc_hurts, base, tiling)

    # Speedup = ref / config_cycles
    su_tiling = ref / tiling
    su_folding = ref / folding
    su_dynmap = ref / dynmap
    su_full = ref / full

    # Segments (incremental)
    seg_base = np.ones(n)
    seg_tiling = np.where(has_base & ~segbc_hurts, su_tiling - 1.0, 0.0)
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

    # Geomean of cumulative speedups across all matrices
    geo_tiling = np.exp(np.mean(np.log(np.maximum(su_tiling, 1e-10))))
    geo_folding = np.exp(np.mean(np.log(np.maximum(su_folding, 1e-10))))
    geo_dynmap = np.exp(np.mean(np.log(np.maximum(su_dynmap, 1e-10))))
    geo_full = np.exp(np.mean(np.log(np.maximum(su_full, 1e-10))))

    # Incremental geomean segments
    geo_segments = np.array([[1.0],
                             [max(geo_tiling - 1.0, 0.0)],
                             [geo_folding - geo_tiling],
                             [geo_dynmap - geo_folding],
                             [geo_full - geo_dynmap]])

    # Sort by full speedup descending
    order = np.argsort(-su_full)
    matrices = [df["matrix"].tolist()[i] for i in order]
    segments = segments[:, order]
    su_full = su_full[order]
    has_base = has_base[order]
    segbc_hurts = segbc_hurts[order]

    # Append GeoMean bar
    matrices.append("GeoMean")
    segments = np.hstack([segments, geo_segments])
    su_full = np.append(su_full, geo_full)
    has_base = np.append(has_base, True)
    segbc_hurts = np.append(segbc_hurts, False)

    n = len(matrices)
    fig, ax = plt.subplots(figsize=(max(14, n * 1.2), 7))
    x = np.arange(n)
    width = 0.6

    bottom = np.zeros(n)
    for i, (label, color) in enumerate(labels_colors):
        visible = np.minimum(segments[i], Y_MAX - bottom)
        visible = np.maximum(visible, 0)
        bars = ax.bar(x, visible, width, bottom=bottom, label=label, color=color,
                      edgecolor="black", linewidth=0.5)
        # Bold border on GeoMean bar
        bars[-1].set_edgecolor("black")
        bars[-1].set_linewidth(1.5)
        bottom += segments[i]

    # Vertical separator before GeoMean
    ax.axvline(x=n - 1.5, color="gray", linestyle="-", linewidth=0.8)

    # Label total speedup
    for j in range(n):
        is_geomean = (j == n - 1)
        if not has_base[j]:
            suffix = "*"
        elif segbc_hurts[j]:
            suffix = "\u2020"
        else:
            suffix = ""
        y_pos = min(su_full[j], Y_MAX) + 0.05
        fw = "bold" if is_geomean else "normal"
        ax.text(x[j], y_pos, f"{su_full[j]:.1f}x{suffix}",
                ha="center", va="bottom", fontsize=10, fontweight=fw,
                clip_on=False)

    ax.axhline(y=1.0, color="gray", linestyle="--", linewidth=0.5)
    ax.grid(axis="y", alpha=0.3)
    ax.set_ylim(0, Y_MAX)
    ax.set_xlim(-0.5, n - 0.5)
    ax.set_xticks(x)
    ax.set_xticklabels(matrices, rotation=45, ha="right", fontsize=18)
    ax.set_ylabel("speedup", fontsize=20)

    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles, labels, fontsize=18,
              loc="upper center", ncol=5, framealpha=0.95, edgecolor="gray",
              handlelength=1.5, columnspacing=1.0,
              bbox_to_anchor=(0.5, 1.22))

    notes = []
    if (~has_base).any():
        notes.append("* base timed out; uses +SegmentBC as 1.0x reference")
    if segbc_hurts.any():
        notes.append("\u2020 SegmentBC slows down; uses +SegmentBC as 1.0x reference")
    if notes:
        ax.annotate("\n".join(notes),
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
