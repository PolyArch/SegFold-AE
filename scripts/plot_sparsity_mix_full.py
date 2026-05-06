#!/usr/bin/env python3
"""Single-panel sparsity_mix swap-ratio heatmap.

Shows the d_A × d_B grid colored by swap_ratio = cyc(d_A, d_B) / cyc(d_B, d_A).
The visually-upper triangle (i > j with origin='lower') is the reciprocal of
the visible lower triangle, so it is grayed out as redundant. The diagonal is
trivially 1.00 and is kept visible.

Style matches other paper final-figures: serif Times font, font.size 13,
RdBu_r diverging colormap with LogNorm centered on 1.0.

Usage:
    python3 scripts/plot_sparsity_mix_full.py OUTPUT_DIR
    python3 scripts/plot_sparsity_mix_full.py OUTPUT_DIR --output PATH
"""

import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif", "Times"],
    "mathtext.fontset": "dejavuserif",
    "font.size": 13,
})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir",
        help="Sparsity-mix run dir containing sparsity_mix_results.csv.")
    parser.add_argument("--output", default=None,
        help="Output path base (no extension). Default: <output_dir>/plots/sparsity_mix_swap_ratio_full")
    args = parser.parse_args()

    csv_path = os.path.join(args.output_dir, "sparsity_mix_results.csv")
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.", file=sys.stderr); sys.exit(1)

    df = pd.read_csv(csv_path)
    df = df[df["cycle"].notna() & (df["cycle"] > 0)]
    K = int(df["K"].iloc[0])
    densities = sorted(set(df["density_A"].tolist() + df["density_B"].tolist()))
    n = len(densities)

    # Mean cycles per (d_A, d_B) across seeds.
    means = df.groupby(["density_A", "density_B"])["cycle"].mean()
    cycles = np.full((n, n), np.nan)
    for (dA, dB), v in means.items():
        cycles[densities.index(dA), densities.index(dB)] = v

    # Full-square swap ratio.
    ratio = np.full((n, n), np.nan)
    for i in range(n):
        for j in range(n):
            a = cycles[i, j]; b = cycles[j, i]
            if np.isfinite(a) and np.isfinite(b) and a > 0 and b > 0:
                ratio[i, j] = a / b

    finite = ratio[np.isfinite(ratio)]
    if finite.size == 0:
        print("No swap-pair data."); sys.exit(0)
    max_dev = max(abs(np.log10(finite)).max(), 0.05)
    norm = mcolors.LogNorm(vmin=10 ** (-max_dev), vmax=10 ** max_dev)

    # Mask the redundant upper triangle (cells where i > j with origin='lower'):
    # those are the anti-symmetric reciprocals of the lower triangle. The
    # diagonal stays at 1.00.
    redundant_mask = np.zeros((n, n), dtype=bool)
    for i in range(n):
        for j in range(n):
            if i > j:
                redundant_mask[i, j] = True
    plot_ratio = np.where(redundant_mask, np.nan, ratio)

    fig, ax = plt.subplots(figsize=(7.0, 6.0))

    # Light-gray rectangles in the masked half.
    for i in range(n):
        for j in range(n):
            if redundant_mask[i, j]:
                ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1,
                                            facecolor="#E5E5E5",
                                            edgecolor="none", zorder=0))

    im = ax.imshow(plot_ratio, origin="lower", cmap="RdBu_r", norm=norm,
                   aspect="auto", zorder=1)

    ax.set_xticks(range(n))
    ax.set_xticklabels([f"{d:g}" for d in densities], rotation=45, ha="right")
    ax.set_yticks(range(n))
    ax.set_yticklabels([f"{d:g}" for d in densities])
    ax.set_xlabel(r"density of B  ($d_B$)")
    ax.set_ylabel(r"density of A  ($d_A$)")
    ax.set_title(f"Swap ratio: cyc($d_A, d_B$) / cyc($d_B, d_A$)  at K={K}",
                 fontsize=13)

    for i in range(n):
        for j in range(n):
            v = plot_ratio[i, j]
            if not np.isfinite(v): continue
            color = "white" if abs(np.log10(v)) > max_dev * 0.6 else "black"
            ax.text(j, i, f"{v:.2f}", ha="center", va="center",
                    color=color, fontsize=11, zorder=2)

    # Thin dashed diagonal to mark the symmetric line (where ratio = 1.00).
    ax.plot([-0.5, n - 0.5], [-0.5, n - 0.5],
            color="black", linestyle="--", linewidth=0.7, alpha=0.55)

    cbar = fig.colorbar(im, ax=ax, label="ratio  (1.0 = symmetric)",
                        fraction=0.046, pad=0.04)

    plt.tight_layout()

    if args.output:
        base = args.output
    else:
        plots_dir = os.path.join(args.output_dir, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        base = os.path.join(plots_dir, "sparsity_mix_swap_ratio_full")
    if base.endswith((".pdf", ".png")):
        base = os.path.splitext(base)[0]
    for ext in (".pdf", ".png"):
        path = base + ext
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
