#!/usr/bin/env python3
"""Plot ablation study results.

Reads ablation stats from output_dir/ablation/{group}/{config}/sim_*_stats.json
and generates per-group plots:
  - window-size:    Line plot of cycles vs window size
  - crossbar-width: Line plot of cycles vs crossbar width
  - k-reordering:   Table of average relative speedup/slowdown

Ablation mapping on SuiteSparse is handled by plot_ablation_mapping.py.

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

SIZE_MARKERS = {256: "o", 512: "s", 1024: "D"}
SIZE_COLORS = {256: "#1f77b4", 512: "#ff7f0e", 1024: "#2ca02c"}


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


def plot_sweep(results, configs_ordered, param_values, param_labels,
               ylabel, xlabel, plots_dir, filename, baseline_cfg=None):
    """Plot a parameter sweep as normalized speedup line plot."""
    sizes = sorted(set(parse_run_id(r)[0] for r in results))
    densities = sorted(set(parse_run_id(r)[1] for r in results))

    fig, axes = plt.subplots(1, len(densities),
                             figsize=(6 * len(densities), 4),
                             squeeze=False)

    for di, da in enumerate(densities):
        ax = axes[0][di]
        for size in sizes:
            # Collect cycles for each config
            cycles = []
            x_vals = []
            for cfg, pval in zip(configs_ordered, param_values):
                for rid, cfg_stats in results.items():
                    s, d, _ = parse_run_id(rid)
                    if s == size and abs(d - da) < 0.001 and cfg in cfg_stats:
                        cycles.append(cfg_stats[cfg]["cycle"])
                        x_vals.append(pval)
                        break

            if not cycles:
                continue

            # Normalize to baseline if provided
            if baseline_cfg is not None:
                base_idx = configs_ordered.index(baseline_cfg)
                if base_idx < len(cycles) and cycles[base_idx] > 0:
                    base_val = cycles[base_idx]
                    y_vals = [base_val / c if c > 0 else 0 for c in cycles]
                else:
                    y_vals = cycles
            else:
                y_vals = cycles

            ax.plot(x_vals, y_vals,
                    marker=SIZE_MARKERS.get(size, "o"),
                    color=SIZE_COLORS.get(size, None),
                    label=f"N={size}", linewidth=1.5, markersize=6)

        ax.set_xlabel(xlabel, fontsize=12)
        if di == 0:
            ax.set_ylabel(ylabel, fontsize=12)
        ax.set_title(f"density = {da}", fontsize=12)
        ax.legend(fontsize=10)
        ax.grid(alpha=0.3)
        ax.set_xticks(param_values)
        ax.set_xticklabels(param_labels, fontsize=10)
        if baseline_cfg is not None:
            ax.axhline(y=1, color="gray", linestyle="--", linewidth=0.5)
        if all(isinstance(v, (int, float)) for v in param_values):
            ax.set_xscale('log', base=2)

    fig.tight_layout()
    for ext in [".pdf", ".png"]:
        path = plots_dir / f"{filename}{ext}"
        fig.savefig(path, dpi=200, bbox_inches="tight")
        print(f"Saved: {path}")
    plt.close()


def compute_k_reordering_summary(results, baseline_cfg, configs, labels):
    """Compute average relative speedup/slowdown for k-reordering configs."""
    sizes = sorted(set(parse_run_id(r)[0] for r in results))
    densities = sorted(set(parse_run_id(r)[1] for r in results))

    print("\n" + "=" * 70)
    print("K-Reordering / Multi B-Row Ablation Summary")
    print("=" * 70)

    # Detailed table
    header = f"{'Config':25s}"
    for size in sizes:
        for da in densities:
            header += f"  N={size},d={da}"
    header += f"  {'Avg':>10s}"
    print(header)
    print("-" * len(header))

    for cfg, label in zip(configs, labels):
        if cfg == baseline_cfg:
            continue
        row = f"{label:25s}"
        ratios = []
        for size in sizes:
            for da in densities:
                base_cycles = None
                cfg_cycles = None
                for rid, cfg_stats in results.items():
                    s, d, _ = parse_run_id(rid)
                    if s == size and abs(d - da) < 0.001:
                        if baseline_cfg in cfg_stats:
                            base_cycles = cfg_stats[baseline_cfg]["cycle"]
                        if cfg in cfg_stats:
                            cfg_cycles = cfg_stats[cfg]["cycle"]
                if base_cycles and cfg_cycles and cfg_cycles > 0:
                    ratio = base_cycles / cfg_cycles
                    ratios.append(ratio)
                    row += f"  {ratio:>10.3f}x"
                else:
                    row += f"  {'N/A':>10s}"
        if ratios:
            avg = np.exp(np.mean(np.log(ratios)))
            row += f"  {avg:>10.3f}x"
            if avg >= 1:
                print(f"{row}  (avg {(avg-1)*100:.1f}% faster)")
            else:
                print(f"{row}  (avg {(1-avg)*100:.1f}% slower)")
        else:
            print(row)

    print()


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
            configs = ["window-1", "window-4", "window-8",
                       "window-16", "window-32", "window-64"]
            values = [1, 4, 8, 16, 32, 64]
            labels = ["1", "4", "8", "16", "32", "64"]
            plot_sweep(results, configs, values, labels,
                       "Speedup (norm. to W=32)", "Window Size",
                       plots_dir, "ablation_window_size",
                       baseline_cfg="window-32")

    # Crossbar width sweep
    cw_dir = abl_root / "crossbar-width"
    if cw_dir.is_dir():
        results = collect_ablation_stats(cw_dir)
        if results:
            configs = ["brl-1", "brl-2", "brl-4", "brl-8", "brl-16"]
            values = [1, 2, 4, 8, 16]
            labels = ["1", "2", "4", "8", "16"]
            plot_sweep(results, configs, values, labels,
                       "Speedup (norm. to BRL=4)", "B Loader Row Limit",
                       plots_dir, "ablation_crossbar_width",
                       baseline_cfg="brl-4")

    # K-reordering — compute average relative speedup/slowdown
    kr_dir = abl_root / "k-reordering"
    if kr_dir.is_dir():
        results = collect_ablation_stats(kr_dir)
        if results:
            configs = ["segfold", "no-k-reorder", "multi-b-row"]
            labels = ["SegFold (baseline)", "No K-Reorder", "Multi B-Row per Row"]
            compute_k_reordering_summary(results, "segfold", configs, labels)

    # Ablation mapping on SuiteSparse is handled by plot_ablation_mapping.py


if __name__ == "__main__":
    main()
