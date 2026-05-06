#!/usr/bin/env python3
"""Combined two-panel sensitivity figure (fig12 a + b in one image).

Generates a side-by-side figure:
  (a) Crossbar Width sweep   — normalized cycles vs crossbar width
  (b) Window Size sweep      — normalized cycles vs window size

Each panel has 6 lines (3 matrix sizes × 2 densities); cycles are normalized
to the baseline config for each (size, density) pair.

Style follows scripts/plot_nonsquare_combined.py: paper-consistent colors,
serif Times font, transparent legend boxes, sub-captions placed below each
panel rather than as titles. Default output is the paper's final-figs dir.

Usage:
    python3 scripts/plot_sensitivity_combined.py OUTPUT_DIR
    python3 scripts/plot_sensitivity_combined.py OUTPUT_DIR \
        --output ../ISCA-rebuttal/paper/final-figs/sensitivity_combined.pdf
"""

import argparse
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import ScalarFormatter

# Reuse data-loading helpers from plot_ablation.py.
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from plot_ablation import (  # noqa: E402
    collect_ablation_stats, parse_run_id,
    SIZE_COLORS, SIZE_MARKERS, MARKERSIZE, DENSITY_STYLES,
)

PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_OUTPUT = PROJECT_ROOT.parent / "ISCA-rebuttal" / "paper" / "final-figs" / "sensitivity_combined.pdf"

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif", "Times"],
    "mathtext.fontset": "dejavuserif",
    "font.size": 14,
})


def normalize_cycles(results, baseline_cfg):
    """Return {(size, density, seed): {param_cfg: norm_cycle}}."""
    norm = {}
    for rid, cfgs in results.items():
        size, da, seed = parse_run_id(rid)
        if baseline_cfg not in cfgs:
            continue
        base = cfgs[baseline_cfg].get("cycle")
        if not base or base <= 0:
            continue
        per_cfg = {}
        for cfg, stats in cfgs.items():
            c = stats.get("cycle")
            if c and c > 0:
                per_cfg[cfg] = c / base
        norm[(size, da, seed)] = per_cfg
    return norm


def draw_sweep_panel(ax, norm_data, configs_ordered, param_values,
                     xlabel, baseline_cfg, show_legend=True):
    """Plot one sensitivity sweep panel into ax."""
    sizes = sorted({k[0] for k in norm_data})
    densities = sorted({k[1] for k in norm_data})

    for size in sizes:
        for density in densities:
            xs, ys = [], []
            for cfg, x in zip(configs_ordered, param_values):
                vals = [norm_data[(size, density, s)][cfg]
                        for s in {k[2] for k in norm_data}
                        if (size, density, s) in norm_data
                        and cfg in norm_data[(size, density, s)]]
                if vals:
                    xs.append(x); ys.append(np.mean(vals))
            if not xs: continue
            ax.plot(xs, ys,
                    color=SIZE_COLORS.get(size),
                    linestyle=DENSITY_STYLES.get(density, "-"),
                    marker=SIZE_MARKERS.get(size, "o"),
                    markersize=MARKERSIZE,
                    linewidth=1.8, alpha=0.6,
                    label=f"N={size}, d={density}")

    ax.axhline(y=1.0, color="gray", linestyle="--", linewidth=0.8, zorder=0)

    if all(isinstance(v, (int, float)) for v in param_values):
        ax.set_xscale("log", base=2)
    ax.set_xticks(param_values)
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.xaxis.set_minor_formatter(plt.NullFormatter())

    ax.set_xlabel(xlabel, fontsize=15)
    ax.set_ylabel(f"Normalized Cycles\n(norm. to {baseline_cfg.split('-')[-1]})",
                  fontsize=14)
    ax.yaxis.grid(True, linestyle=":", linewidth=0.5, alpha=0.7)
    ax.set_axisbelow(True)

    if show_legend:
        ax.legend(loc="upper right", framealpha=0.7, edgecolor="none",
                  ncol=1, fontsize=16, handlelength=2.2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path,
                        help="Run dir containing ablation/{crossbar-width, window-size}/")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"Output PDF path. Default: {DEFAULT_OUTPUT}")
    args = parser.parse_args()

    cw_dir = args.output_dir / "ablation" / "crossbar-width"
    ws_dir = args.output_dir / "ablation" / "window-size"
    if not cw_dir.is_dir():
        print(f"Error: {cw_dir} not found.", file=sys.stderr); sys.exit(1)
    if not ws_dir.is_dir():
        print(f"Error: {ws_dir} not found.", file=sys.stderr); sys.exit(1)

    cw_results = collect_ablation_stats(cw_dir)
    ws_results = collect_ablation_stats(ws_dir)
    if not cw_results or not ws_results:
        print("Error: empty ablation results.", file=sys.stderr); sys.exit(1)

    cw_configs = ["brl-1", "brl-2", "brl-4", "brl-8", "brl-16"]
    cw_values  = [1, 2, 4, 8, 16]
    cw_baseline = "brl-4"

    ws_configs = ["window-1", "window-4", "window-8",
                  "window-16", "window-32", "window-64"]
    ws_values  = [1, 4, 8, 16, 32, 64]
    ws_baseline = "window-32"

    cw_norm = normalize_cycles(cw_results, cw_baseline)
    ws_norm = normalize_cycles(ws_results, ws_baseline)

    fig, (ax_a, ax_b) = plt.subplots(
        1, 2, figsize=(13.5, 4.2),
        gridspec_kw={"wspace": 0.28},
    )

    draw_sweep_panel(ax_a, cw_norm, cw_configs, cw_values,
                     xlabel="Crossbar Width",
                     baseline_cfg=cw_baseline, show_legend=True)
    draw_sweep_panel(ax_b, ws_norm, ws_configs, ws_values,
                     xlabel="Window Size",
                     baseline_cfg=ws_baseline, show_legend=True)

    # (a)/(b) sub-captions placed BELOW each panel.
    ax_a.text(0.5, -0.22, "(a) Crossbar Width Sweep",
              transform=ax_a.transAxes, ha="center", va="top", fontsize=16)
    ax_b.text(0.5, -0.22, "(b) Window Size Sweep",
              transform=ax_b.transAxes, ha="center", va="top", fontsize=16)

    plt.tight_layout()

    out = args.output
    os.makedirs(out.parent, exist_ok=True)
    base = str(out)
    if base.endswith((".pdf", ".png")):
        base = os.path.splitext(base)[0]
    for ext in (".pdf", ".png"):
        path = base + ext
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


if __name__ == "__main__":
    main()
