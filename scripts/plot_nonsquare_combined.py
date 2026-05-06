#!/usr/bin/env python3
"""Combined two-panel nonsquare figure (vertically aligned matrix axis).

Top panel    : SegFold vs Spada speedup on real nonsquare SuiteSparse matrices
                (data from comparison/results/nonsquare_suitesparse_results.csv).
Bottom panel : Direction 1 vs Direction 2 speedup using a synthetic square S
                (data from a SegFold-AE shape sensitivity run; default density=0.004).

Style follows scripts/plot_shape_sensitivity_bar.py: font.size=13, bar_width=0.38,
matching colors (top panel uses SegFold/Spada colors; bottom uses dir1/dir2 colors).

Usage:
    python3 scripts/plot_nonsquare_combined.py \
        --top-csv ../ISCA-rebuttal/comparison/results/nonsquare_suitesparse_results.csv \
        --bottom-csv output/shape_sens_20260427_170250/shape_sensitivity_results.csv \
        --density 0.004 \
        --output ../ISCA-rebuttal/paper/final-figs/nonsquare_combined.pdf
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
    "font.size": 14,
})

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX_ORDER = ["lp_woodw", "pcb3000", "gemat1", "Franz6", "Franz8", "psse1"]
SEP_X = 2.5  # split between wide (M < K) and tall (M > K)
BAR_WIDTH = 0.38

# Top panel colors (SegFold vs Spada)
TOP_COLORS = {"SegFold": "#42A5F5", "Spada": "#B5D99C"}
TOP_LABELS = {"SegFold": "SegFold (Ours)", "Spada": "Spada"}

# Bottom panel colors (dir1 vs dir2) — same as plot_shape_sensitivity_bar.py
BOT_COLORS = {1: "#42A5F5", 2: "#FFCCBC"}
BOT_LABELS = {1: r"Direction 1: $A_{\rm real} \times S$",
              2: r"Direction 2: $S \times A_{\rm real}^{\!\top}$"}


def parse_num(val):
    if pd.isna(val) or val == "" or val == "#VALUE!":
        return np.nan
    if isinstance(val, (int, float)):
        return float(val)
    return float(str(val).replace(",", ""))


def load_top(csv_path):
    """Returns (matrices, M_dims, K_dims, segfold_speedups, spada_speedups)."""
    df = pd.read_csv(csv_path)
    by_matrix = df.set_index("matrix")
    matrices, M_dims, K_dims, segs, spadas = [], [], [], [], []
    for m in MATRIX_ORDER:
        if m not in by_matrix.index:
            continue
        row = by_matrix.loc[m]
        sp = parse_num(row["spada_cycles"])
        sf = parse_num(row["segfold_cycles"])
        if np.isnan(sp) or sp <= 0:
            continue
        matrices.append(m)
        M_dims.append(int(row["M"]))
        K_dims.append(int(row["K"]))
        spadas.append(1.0)  # Spada is the baseline
        segs.append(sp / sf if (not np.isnan(sf) and sf > 0) else 0.0)
    return matrices, M_dims, K_dims, segs, spadas


def load_bottom(csv_path, density):
    """Returns (matrices, dir1_speedups, dir2_speedups) where dir1 = 1.0 baseline."""
    df = pd.read_csv(csv_path)
    df = df[df["cycle"].notna() & (df["cycle"] > 0)]
    sub = df[np.isclose(df["density"], density, atol=1e-9)]
    matrices, dir1_norm, dir2_norm = [], [], []
    for m in MATRIX_ORDER:
        m_sub = sub[sub["matrix"] == m]
        if m_sub.empty: continue
        d1 = m_sub[m_sub["direction"] == 1]
        d2 = m_sub[m_sub["direction"] == 2]
        c1 = float(d1["cycle"].iloc[0]) if not d1.empty else np.nan
        c2 = float(d2["cycle"].iloc[0]) if not d2.empty else np.nan
        matrices.append(m)
        dir1_norm.append(1.0)
        dir2_norm.append(c1 / c2 if (c1 > 0 and c2 > 0) else 0.0)
    return matrices, dir1_norm, dir2_norm


def draw_two_bar_panel(ax, x, n, left_vals, right_vals,
                       left_color, right_color, left_label, right_label,
                       y_label, annotate_fmt="{:.2f}×", show_legend=True,
                       y_max_min=1.5, show_split_text=True,
                       legend_anchor=(0.5, 1.24)):
    """Draw a paired-bar panel (left + right per matrix).

    The annotated value is placed above the right (comparison) bar.
    """
    ax.bar(x - BAR_WIDTH / 2, left_vals, BAR_WIDTH,
           label=left_label, color=left_color,
           edgecolor="black", linewidth=0.5)
    ax.bar(x + BAR_WIDTH / 2, right_vals, BAR_WIDTH,
           label=right_label, color=right_color,
           edgecolor="black", linewidth=0.5)

    for i, v in enumerate(right_vals):
        if v <= 0: continue
        ax.text(x[i] + BAR_WIDTH / 2, v + 0.04,
                annotate_fmt.format(v), ha="center", va="bottom", fontsize=10)

    ax.axhline(y=1.0, color="gray", linestyle="--", linewidth=0.6)
    y_max = max(y_max_min, max(right_vals + [1.0]) * 1.18)
    ax.set_ylim(0, y_max)
    ax.set_xlim(-0.6, n - 0.4)

    # Wide / Tall split
    ax.axvline(x=SEP_X, color="gray", linestyle="-", linewidth=0.8)
    if show_split_text:
        ax.text((-0.6 + SEP_X) / 2, y_max * 0.95, "Wide  (M < K)",
                ha="center", va="top", fontsize=10,
                fontstyle="italic", color="#555555")
        ax.text((SEP_X + n - 0.4) / 2, y_max * 0.95, "Tall  (M > K)",
                ha="center", va="top", fontsize=10,
                fontstyle="italic", color="#555555")

    ax.grid(axis="y", alpha=0.3)
    ax.set_ylabel(y_label, fontsize=14, labelpad=8)
    if show_legend:
        ax.legend(loc="upper center", fontsize=12, ncol=2,
                  framealpha=0.95, edgecolor="gray",
                  bbox_to_anchor=legend_anchor)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--top-csv",
        default="/home/alice/github.com/JJxinruiwu/ISCA-rebuttal/comparison/results/nonsquare_suitesparse_results.csv")
    parser.add_argument("--bottom-csv",
        default="/home/alice/github.com/JJxinruiwu/SegFold-AE/output/shape_sens_20260427_170250/shape_sensitivity_results.csv")
    parser.add_argument("--density", type=float, default=0.004)
    parser.add_argument("--output",
        default=os.path.join(PROJECT_ROOT, "expected_results", "plots", "nonsquare_combined.pdf"))
    args = parser.parse_args()

    if not os.path.exists(args.top_csv):
        print(f"Error: top CSV not found: {args.top_csv}", file=sys.stderr); sys.exit(1)
    if not os.path.exists(args.bottom_csv):
        print(f"Error: bottom CSV not found: {args.bottom_csv}", file=sys.stderr); sys.exit(1)

    t_mats, t_M, t_K, segs, spadas = load_top(args.top_csv)
    b_mats, dir1, dir2 = load_bottom(args.bottom_csv, args.density)

    if t_mats != b_mats:
        print(f"WARN: matrix lists differ between panels:\n  top: {t_mats}\n  bot: {b_mats}",
              file=sys.stderr)
    matrices = t_mats
    n = len(matrices)
    x = np.arange(n)

    # Two-line labels: matrix name on top, dimensions below.
    labels = [f"{m}\n({M}×{K})" for m, M, K in zip(matrices, t_M, t_K)]

    fig, (ax_t, ax_b) = plt.subplots(
        2, 1, figsize=(max(7, n * 1.1), 7.4),
        sharex=True,
        gridspec_kw={"hspace": 0.54},
        constrained_layout=False,
    )

    # ---- top panel: SegFold vs Spada ----
    draw_two_bar_panel(
        ax_t, x, n,
        left_vals=spadas, right_vals=segs,
        left_color=TOP_COLORS["Spada"], right_color=TOP_COLORS["SegFold"],
        left_label=TOP_LABELS["Spada"], right_label=TOP_LABELS["SegFold"],
        y_label="speedup over Spada",
        annotate_fmt="{:.2f}×", show_legend=True, y_max_min=2.0,
        show_split_text=True, legend_anchor=(0.5, 1.24),
    )

    # ---- bottom panel: dir1 vs dir2 ----
    # Single-line y-label so both panels' plot areas have the same left margin
    # and the bars are vertically aligned. Density info moved to the (b)
    # subfigure caption.
    draw_two_bar_panel(
        ax_b, x, n,
        left_vals=dir1, right_vals=dir2,
        left_color=BOT_COLORS[1], right_color=BOT_COLORS[2],
        left_label=BOT_LABELS[1], right_label=BOT_LABELS[2],
        y_label="speedup over Direction 1",
        annotate_fmt="{:.2f}×", show_legend=True, y_max_min=1.5,
        show_split_text=False,  # already shown on top panel
        legend_anchor=(0.5, 1.27),
    )

    # X tick labels go on the bottom panel only (sharex=True hides top labels).
    ax_b.set_xticks(x)
    ax_b.set_xticklabels(labels, rotation=45, ha="right", fontsize=11)
    ax_b.tick_params(axis="x", pad=2)

    # (a) / (b) sub-captions placed BELOW each panel. (b) is pushed further
    # down to clear the two-line, 45-degree-rotated x-tick labels.
    ax_t.text(0.5, -0.13,
              "(a) SegFold vs Spada on real nonsquare matrices",
              transform=ax_t.transAxes, ha="center", va="top", fontsize=12)
    ax_b.text(0.5, -0.68,
              rf"(b) Direction 1 ($A_{{\rm real}}\!\times\!S$) vs Direction 2 ($S\!\times\!A_{{\rm real}}^{{\!\top}}$),  density$(S)={args.density:g}$",
              transform=ax_b.transAxes, ha="center", va="top", fontsize=12)

    fig.align_ylabels([ax_t, ax_b])
    fig.subplots_adjust(left=0.12, right=0.99, top=0.88, bottom=0.28, hspace=0.54)
    out = args.output
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if out.endswith(".pdf"):
        bases = [out, out[:-4] + ".png"]
    elif out.endswith(".png"):
        bases = [out[:-4] + ".pdf", out]
    else:
        bases = [out + ".pdf", out + ".png"]
    for b in bases:
        fig.savefig(b, dpi=200, bbox_inches="tight")
        print(f"Saved: {b}")
    plt.close()


if __name__ == "__main__":
    main()
