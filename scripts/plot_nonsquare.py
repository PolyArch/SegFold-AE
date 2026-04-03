#!/usr/bin/env python3
"""Plot non-square performance: SegFold vs Spada dataflows.

Reads SegFold cycle counts from nonsquare_results.csv and merges with
pre-computed baseline data from data/baselines/nonsquare_baselines.csv.
Speedup normalized to Spada.

Usage:
    python3 scripts/plot_nonsquare.py OUTPUT_DIR
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

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MATRIX_ORDER = ["lp_woodw", "pcb3000", "gemat1", "Franz6", "Franz8", "psse1"]

ACCELS = [
    ("segfold_cycles", "SegFold (Ours)", "#BBDEFB"),
    ("spada_cycles",   "Spada",          "#B5D99C"),
    ("ip_cycles",      "Spada (IP only)",    "#F4A7BB"),
    ("op_cycles",      "Spada (OP only)",    "#E57373"),
    ("multirow_cycles","Spada (Gust only)",  "#FFD54F"),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", help="Directory with nonsquare_results.csv")
    args = parser.parse_args()

    out_dir = os.path.abspath(args.output_dir)
    segfold_path = os.path.join(out_dir, "nonsquare_results.csv")
    baseline_path = os.path.join(PROJECT_ROOT, "data", "baselines", "nonsquare_baselines.csv")

    if not os.path.exists(segfold_path):
        print(f"Error: {segfold_path} not found. Run collect_results.py first.")
        sys.exit(1)

    sf_df = pd.read_csv(segfold_path)
    sf_df = sf_df.rename(columns={"cycle": "segfold_cycles"})

    bl_df = pd.read_csv(baseline_path)

    df = pd.merge(sf_df[["matrix", "segfold_cycles"]], bl_df, on="matrix", how="outer")

    df["_order"] = df["matrix"].apply(
        lambda m: MATRIX_ORDER.index(m) if m in MATRIX_ORDER else 999
    )
    df = df.sort_values("_order").reset_index(drop=True)

    valid = []
    speedups = {col: [] for col, _, _ in ACCELS}

    for _, row in df.iterrows():
        sp = row.get("spada_cycles")
        if pd.isna(sp) or sp <= 0:
            continue
        valid.append(row["matrix"])
        for col, _, _ in ACCELS:
            v = row.get(col)
            if pd.notna(v) and v > 0:
                speedups[col].append(sp / v)
            else:
                speedups[col].append(0)

    # Geomean
    for col, _, _ in ACCELS:
        vals = [v for v in speedups[col] if v > 0]
        geo = np.exp(np.mean(np.log(vals))) if vals else 0
        speedups[col].append(geo)
    valid.append("GeoMean")

    n = len(valid)
    present = [(col, label, color) for col, label, color in ACCELS
               if any(v > 0 for v in speedups[col])]
    n_accels = len(present)
    x = np.arange(n)
    bar_width = 0.8 / n_accels

    fig, ax = plt.subplots(figsize=(max(12, n * 1.4), 6))

    for j, (col, label, color) in enumerate(present):
        offset = (j - n_accels / 2 + 0.5) * bar_width
        bars = ax.bar(x + offset, speedups[col], bar_width,
                      label=label, color=color,
                      edgecolor="black", linewidth=0.5)
        bars[-1].set_edgecolor("black")
        bars[-1].set_linewidth(1.5)

    # SegFold value labels
    sf_col = "segfold_cycles"
    sf_idx = next(j for j, (c, _, _) in enumerate(present) if c == sf_col)
    sf_offset = (sf_idx - n_accels / 2 + 0.5) * bar_width
    for i, v in enumerate(speedups[sf_col]):
        if v <= 0:
            continue
        fw = "bold" if i == n - 1 else "normal"
        ax.text(x[i] + sf_offset, v + 0.03, f"{v:.1f}x",
                ha="center", va="bottom", fontsize=10, fontweight=fw, clip_on=False)

    ax.axvline(x=n - 1.5, color="gray", linestyle="-", linewidth=0.8)
    ax.axhline(y=1, color="gray", linestyle="--", linewidth=0.5)
    ax.grid(axis="y", alpha=0.3)
    ax.set_xlim(-0.6, n - 0.4)
    ax.set_ylim(bottom=0)
    ax.set_ylabel("speedup", fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(valid, rotation=0, ha="center", fontsize=11)
    ax.legend(loc="upper center", fontsize=12, ncol=min(n_accels, 5),
              framealpha=0.95, edgecolor="gray",
              handlelength=1.5, columnspacing=1.0,
              bbox_to_anchor=(0.5, 1.2))

    plots_dir = os.path.join(out_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    for ext in [".pdf", ".png"]:
        path = os.path.join(plots_dir, f"nonsquare_speedup{ext}")
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
