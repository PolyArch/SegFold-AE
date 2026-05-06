#!/usr/bin/env python3
"""Plot non-square performance: SegFold vs Spada, normalized to Spada.

Reads SegFold cycle counts from fig9_nonsquare_results.csv and merges with
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

COL_MAP = {
    "segfold_cycles": ("SegFold", "SegFold (Ours)"),
    "spada_cycles":   ("Spada",   "Spada"),
}

COLORS = {
    "SegFold": "#42A5F5",   # medium blue
    "Spada":   "#B5D99C",   # light green
}

HATCHES = {
    "SegFold": "",
    "Spada":   "",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", help="Directory with fig9_nonsquare_results.csv")
    args = parser.parse_args()

    out_dir = os.path.abspath(args.output_dir)
    segfold_path = os.path.join(out_dir, "fig9_nonsquare_results.csv")
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

    # Parse accelerator keys
    accel_keys = []
    accel_data = {}
    for col, (key, label) in COL_MAP.items():
        if col not in df.columns:
            continue
        vals = [float(v) if pd.notna(v) and v != "" else np.nan for v in df[col]]
        accel_keys.append((key, label))
        accel_data[key] = vals

    matrices = df["matrix"].tolist()

    # Compute speedup normalized to Spada
    speedups = {key: [] for key, _ in accel_keys}
    valid_idx = []

    for i in range(len(matrices)):
        sp = accel_data["Spada"][i]
        if np.isnan(sp) or sp <= 0:
            continue
        valid_idx.append(i)
        for key, _ in accel_keys:
            v = accel_data[key][i]
            if not np.isnan(v) and v > 0:
                speedups[key].append(sp / v)
            else:
                speedups[key].append(0)

    valid_matrices = [matrices[i] for i in valid_idx]
    valid_dims = [f"({int(df.iloc[i]['M'])}\u00d7{int(df.iloc[i]['K'])})"
                  for i in valid_idx]
    labels = [f"{m}\n{d}" for m, d in zip(valid_matrices, valid_dims)]

    n = len(valid_matrices)
    n_accels = len(accel_keys)
    x = np.arange(n)
    bar_width = 0.55 / n_accels
    y_max = 2.0

    fig, ax = plt.subplots(figsize=(max(7, n * 1.05), 3.5))

    # Draw bars
    for j, (key, label) in enumerate(accel_keys):
        offset = (j - n_accels / 2 + 0.5) * bar_width
        ax.bar(x + offset, speedups[key], bar_width,
               label=label, color=COLORS[key], hatch=HATCHES.get(key, ""),
               edgecolor="black", linewidth=0.5)

    # Vertical separator between wide (cols>rows) and tall (rows>cols)
    # First 3 are wide, last 3 are tall
    sep_x = 2.5
    ax.axvline(x=sep_x, color="gray", linestyle="-", linewidth=0.8)
    ax.text((-0.6 + sep_x) / 2, y_max * 0.95, "Wide (cols > rows)",
            ha="center", va="top", fontsize=9, fontstyle="italic", color="#555555")
    ax.text((sep_x + n - 0.4) / 2, y_max * 0.95, "Tall (rows > cols)",
            ha="center", va="top", fontsize=9, fontstyle="italic", color="#555555")

    ax.axhline(y=1, color="gray", linestyle="--", linewidth=0.5)
    ax.grid(axis="y", alpha=0.3)
    ax.set_xlim(-0.6, n - 0.4)
    ax.set_ylim(0, y_max)

    # Value labels on SegFold bars
    sf_idx = next(j for j, (k, _) in enumerate(accel_keys) if k == "SegFold")
    sf_offset = (sf_idx - n_accels / 2 + 0.5) * bar_width
    for i, v in enumerate(speedups["SegFold"]):
        if v <= 0:
            continue
        fw = "normal"
        if v > y_max:
            ax.text(x[i] + sf_offset, y_max + 0.03, f"{v:.1f}x",
                    ha="center", va="bottom", fontsize=9, fontweight=fw,
                    clip_on=False)
        else:
            ax.text(x[i] + sf_offset, v + 0.04, f"{v:.1f}x",
                    ha="center", va="bottom", fontsize=9, fontweight=fw)

    ax.set_ylabel("speedup", fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=0, ha="center", fontsize=9)
    ax.legend(loc="upper center", fontsize=10, ncol=8,
              framealpha=0.95, edgecolor="gray",
              handlelength=1.5, columnspacing=1.0,
              bbox_to_anchor=(0.5, 1.25))

    plots_dir = os.path.join(out_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)

    plt.tight_layout()
    for ext in [".pdf", ".png"]:
        path = os.path.join(plots_dir, f"fig9_nonsquare_speedup{ext}")
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
