#!/usr/bin/env python3
"""Plot ablation study results.

Reads ablation stats from output_dir/ablation/{group}/{config}/sim_*_stats.json
and generates per-group plots.

Usage:
    python3 scripts/plot_ablation.py output/my_run
"""

import argparse
import json
import os
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif", "Times"],
    "mathtext.fontset": "dejavuserif",
    "font.size": 13,
})


def collect_ablation_stats(abl_dir: Path) -> dict:
    """Collect all stats from ablation/{config}/sim_*_stats.json."""
    results = defaultdict(dict)
    for config_dir in sorted(abl_dir.iterdir()):
        if not config_dir.is_dir():
            continue
        cfg = config_dir.name
        for sf in sorted(config_dir.glob("sim_*_stats.json")):
            with open(sf) as f:
                stats = json.load(f)
            rid = sf.stem.replace("sim_", "").replace("_stats", "")
            results[rid][cfg] = stats
    return results


def parse_run_id(rid: str):
    """Parse s256_dA5_dB5_r0 -> (256, 0.05, 0)."""
    parts = rid.split("_")
    size = int(parts[0][1:])
    da = int(parts[1][2:]) / 100.0
    seed = int(parts[3][1:])
    return size, da, seed


def plot_sweep(results, configs_ordered, param_name, param_values,
               title, xlabel, plots_dir, filename):
    """Plot a parameter sweep (line plot per size/density)."""
    sizes = sorted(set(parse_run_id(r)[0] for r in results))
    densities = sorted(set(parse_run_id(r)[1] for r in results))

    fig, axes = plt.subplots(1, len(densities), figsize=(7 * len(densities), 5),
                             squeeze=False)

    for di, da in enumerate(densities):
        ax = axes[0][di]
        for size in sizes:
            cycles = []
            x_vals = []
            for cfg, pval in zip(configs_ordered, param_values):
                # Find matching run
                for rid, cfg_stats in results.items():
                    s, d, _ = parse_run_id(rid)
                    if s == size and abs(d - da) < 0.001 and cfg in cfg_stats:
                        cycles.append(cfg_stats[cfg]["cycle"])
                        x_vals.append(pval)
                        break
            if cycles:
                ax.plot(x_vals, cycles, marker='o', label=f"N={size}")

        ax.set_xlabel(xlabel)
        ax.set_ylabel("Cycles")
        ax.set_title(f"density={da}")
        ax.legend()
        ax.grid(alpha=0.3)
        if all(isinstance(v, (int, float)) for v in param_values):
            ax.set_xscale('log', base=2)

    fig.suptitle(title, fontsize=15, y=1.02)
    fig.tight_layout()
    for ext in [".pdf", ".png"]:
        path = plots_dir / f"{filename}{ext}"
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


def plot_bar(results, configs_ordered, labels, title, plots_dir, filename):
    """Plot grouped bar chart for discrete config comparison."""
    sizes = sorted(set(parse_run_id(r)[0] for r in results))
    densities = sorted(set(parse_run_id(r)[1] for r in results))

    # Build data: x-axis = (size, density), groups = configs
    x_labels = []
    data = {cfg: [] for cfg in configs_ordered}

    for size in sizes:
        for da in densities:
            x_labels.append(f"N={size}\nd={da}")
            for cfg in configs_ordered:
                found = False
                for rid, cfg_stats in results.items():
                    s, d, _ = parse_run_id(rid)
                    if s == size and abs(d - da) < 0.001 and cfg in cfg_stats:
                        data[cfg].append(cfg_stats[cfg]["cycle"])
                        found = True
                        break
                if not found:
                    data[cfg].append(0)

    n = len(x_labels)
    n_cfgs = len(configs_ordered)
    x = np.arange(n)
    bar_width = 0.8 / n_cfgs
    colors = ["#BBDEFB", "#B5D99C", "#F4A7BB", "#FFD54F", "#E57373"]

    fig, ax = plt.subplots(figsize=(max(10, n * 1.5), 5))
    for j, cfg in enumerate(configs_ordered):
        offset = (j - n_cfgs / 2 + 0.5) * bar_width
        vals = data[cfg]
        ax.bar(x + offset, vals, bar_width, label=labels[j],
               color=colors[j % len(colors)], edgecolor="black", linewidth=0.5)

    ax.set_ylabel("Cycles")
    ax.set_title(title)
    ax.set_xticks(x)
    ax.set_xticklabels(x_labels, fontsize=10)
    ax.legend(fontsize=11)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    for ext in [".pdf", ".png"]:
        path = plots_dir / f"{filename}{ext}"
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", help="Directory with ablation/ subdirectory")
    args = parser.parse_args()

    out_dir = Path(args.output_dir).resolve()
    abl_root = out_dir / "ablation"
    if not abl_root.is_dir():
        print(f"Error: {abl_root} not found.")
        sys.exit(1)

    plots_dir = out_dir / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    # Window size sweep
    ws_dir = abl_root / "window-size"
    if ws_dir.is_dir():
        results = collect_ablation_stats(ws_dir)
        if results:
            configs = ["window-1", "window-4", "window-8", "window-16", "window-32", "window-64"]
            values = [1, 4, 8, 16, 32, 64]
            plot_sweep(results, configs, "window_size", values,
                       "B Loader Window Size Sweep", "Window Size",
                       plots_dir, "ablation_window_size")

    # Crossbar width sweep
    cw_dir = abl_root / "crossbar-width"
    if cw_dir.is_dir():
        results = collect_ablation_stats(cw_dir)
        if results:
            configs = ["brl-1", "brl-2", "brl-4", "brl-8", "brl-16"]
            values = [1, 2, 4, 8, 16]
            plot_sweep(results, configs, "b_loader_row_limit", values,
                       "Crossbar Width (B Loader Row Limit) Sweep",
                       "B Loader Row Limit",
                       plots_dir, "ablation_crossbar_width")

    # K-reordering
    kr_dir = abl_root / "k-reordering"
    if kr_dir.is_dir():
        results = collect_ablation_stats(kr_dir)
        if results:
            plot_bar(results,
                     ["segfold", "no-k-reorder", "multi-b-row"],
                     ["SegFold", "No K-Reorder", "Multi B-Row"],
                     "K-Reordering Ablation",
                     plots_dir, "ablation_k_reordering")

    # Mapping ablation on SuiteSparse is handled by plot_mapping_ablation.py


if __name__ == "__main__":
    main()
