#!/usr/bin/env python3
"""Ablation Studies: Run SegFold with different configs on synthetic matrices.

Four ablation groups:
  - window-size:    B loader window size sweep (1, 4, 8, 16, 32, 64)
  - k-reordering:   B row reordering strategies
  - crossbar-width: B loader row limit sweep (1, 2, 4, 8, 16)
  - mapping:        Dynamic routing / mapping strategies

Each config is tested on synthetic square matrices at sizes 256, 512, 1024
with densities 0.05 and 0.1, using 3 random seeds for averaging.

Usage:
    python3 scripts/run_ablation.py output/my_run
    python3 scripts/run_ablation.py output/my_run --ablation window-size
    python3 scripts/run_ablation.py output/my_run --jobs 4
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

ABLATIONS = {
    "window-size": {
        "window-1":  "configs/ablation-window-1.yaml",
        "window-4":  "configs/ablation-window-4.yaml",
        "window-8":  "configs/ablation-window-8.yaml",
        "window-16": "configs/ablation-window-16.yaml",
        "window-32": "configs/segfold.yaml",
        "window-64": "configs/ablation-window-64.yaml",
    },
    "k-reordering": {
        "segfold":      "configs/segfold.yaml",
        "no-k-reorder": "configs/ablation-no-k-reordering.yaml",
        "multi-b-row":  "configs/ablation-multi-b-row-per-row.yaml",
    },
    "crossbar-width": {
        "brl-1":  "configs/ablation-brl-1.yaml",
        "brl-2":  "configs/ablation-brl-2.yaml",
        "brl-4":  "configs/segfold.yaml",
        "brl-8":  "configs/ablation-brl-8.yaml",
        "brl-16": "configs/ablation-brl-16.yaml",
    },
    "mapping": {
        "segfold":     "configs/segfold.yaml",
        "map-to-csr":  "configs/ablation-map-to-csr.yaml",
        "map-to-zero": "configs/ablation-map-to-zero.yaml",
    },
    "mapping-dyntile": {
        "segfold":     "configs/ablation-map-dyntile-segfold.yaml",
        "ideal":       "configs/ablation-map-dyntile-ideal.yaml",
        "map-to-csr":  "configs/ablation-map-dyntile-csr.yaml",
        "map-to-zero": "configs/ablation-map-dyntile-zero.yaml",
    },
}

SIZES = [256, 512]
DENSITIES = [(0.05, 0.05), (0.1, 0.1)]
SEEDS = [0]

CYCLE_RE = re.compile(r"completed successfully in (\d+) cycles")


def find_latest_stats(tmp_dir: Path) -> Path | None:
    files = sorted(tmp_dir.glob("run_*_stats.json"), key=os.path.getmtime, reverse=True)
    return files[0] if files else None


def run_one(binary: Path, config: Path, ablation_name: str, config_name: str,
            size: int, da: float, db: float, seed: int,
            out_dir: Path, tmp_dir: Path, timeout: int) -> dict:
    """Run one synthetic matrix simulation. Returns result dict."""
    da_pct = int(da * 100)
    db_pct = int(db * 100)
    run_id = f"s{size}_dA{da_pct}_dB{db_pct}_r{seed}"

    # Per-run tmp dir for parallel safety
    run_tmp = tmp_dir / f"{ablation_name}_{config_name}_{run_id}"
    run_tmp.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(binary),
        "--config", str(config),
        "--M", str(size), "--K", str(size), "--N", str(size),
        "--density-a", str(da), "--density-b", str(db),
        "--seed", str(seed),
        "--tmp-dir", str(run_tmp),
    ]

    result = {
        "ablation": ablation_name, "config": config_name,
        "size": size, "densityA": da, "densityB": db, "seed": seed,
        "cycles": -1, "ok": False,
    }

    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
            cwd=str(PROJECT_ROOT),
        )
        m = CYCLE_RE.search(proc.stdout)
        cycles = int(m.group(1)) if m else -1
        result["cycles"] = cycles

        if proc.returncode != 0:
            print(f"  [FAIL] {config_name}/{run_id}: rc={proc.returncode}")
            return result

        # Move stats to output dir
        sub_dir = out_dir / config_name
        sub_dir.mkdir(parents=True, exist_ok=True)
        latest = find_latest_stats(run_tmp)
        if latest:
            dest = sub_dir / f"sim_{run_id}_stats.json"
            shutil.move(str(latest), str(dest))
            cfg_json = latest.with_name(latest.name.replace("_stats.json", "_config.json"))
            if cfg_json.exists():
                shutil.move(str(cfg_json), str(sub_dir / f"sim_{run_id}_config.json"))

        result["ok"] = True
        print(f"  [OK]   {config_name}/{run_id}: {cycles} cycles")
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {config_name}/{run_id}")
    except Exception as e:
        print(f"  [ERROR] {config_name}/{run_id}: {e}")

    return result


def run_ablation_group(binary, ablation_name, configs, out_dir, tmp_dir, timeout, jobs):
    """Run all configs x matrices for one ablation group."""
    abl_out = out_dir / ablation_name
    abl_out.mkdir(parents=True, exist_ok=True)

    tasks = []
    for config_name, config_path in configs.items():
        config = PROJECT_ROOT / config_path
        if not config.exists():
            print(f"  [SKIP] Config not found: {config}")
            continue
        for size in SIZES:
            for da, db in DENSITIES:
                for seed in SEEDS:
                    tasks.append((binary, config, ablation_name, config_name,
                                  size, da, db, seed, abl_out, tmp_dir, timeout))

    results = []
    if jobs <= 1:
        for i, args in enumerate(tasks):
            print(f"[{i+1}/{len(tasks)}]", end="")
            results.append(run_one(*args))
    else:
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = {executor.submit(run_one, *args): args for args in tasks}
            for future in as_completed(futures):
                results.append(future.result())

    return results


def print_summary(ablation_name, results):
    """Print summary table with average cycles per config x (size, density)."""
    from collections import defaultdict
    # Group by (config, size, density) and average across seeds
    groups = defaultdict(list)
    for r in results:
        if r["cycles"] > 0:
            key = (r["config"], r["size"], r["densityA"])
            groups[key].append(r["cycles"])

    configs = sorted(set(r["config"] for r in results))
    print(f"\n{'':20s}", end="")
    for cfg in configs:
        print(f" {cfg:>14s}", end="")
    print()
    print("-" * (20 + 15 * len(configs)))

    for size in SIZES:
        for da, _ in DENSITIES:
            label = f"s{size}_d{int(da*100)}"
            print(f"{label:20s}", end="")
            for cfg in configs:
                vals = groups.get((cfg, size, da), [])
                if vals:
                    avg = sum(vals) / len(vals)
                    print(f" {avg:>14.0f}", end="")
                else:
                    print(f" {'FAIL':>14s}", end="")
            print()


def main():
    parser = argparse.ArgumentParser(description="Ablation study experiments")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--ablation", type=str, default=None,
                        choices=list(ABLATIONS.keys()),
                        help="Run only this ablation group")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()

    binary = PROJECT_ROOT / "csegfold" / "build" / "csegfold"
    if not binary.exists():
        print(f"Error: {binary} not found. Run scripts/setup.sh first.")
        sys.exit(1)

    tmp_dir = PROJECT_ROOT / "csegfold" / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)

    out_dir = args.output_dir / "ablation"
    out_dir.mkdir(parents=True, exist_ok=True)

    ablations = {args.ablation: ABLATIONS[args.ablation]} if args.ablation else ABLATIONS

    total_configs = sum(len(c) for c in ablations.values())
    total_runs = total_configs * len(SIZES) * len(DENSITIES) * len(SEEDS)

    print("=" * 50)
    print("Ablation Study Experiments")
    print(f"  ablations:  {list(ablations.keys())}")
    print(f"  sizes:      {SIZES}")
    print(f"  densities:  {DENSITIES}")
    print(f"  seeds:      {SEEDS}")
    print(f"  total runs: {total_runs}")
    print(f"  jobs:       {args.jobs}")
    print(f"  output:     {out_dir}")
    print("=" * 50)

    for ablation_name, configs in ablations.items():
        n_runs = len(configs) * len(SIZES) * len(DENSITIES) * len(SEEDS)
        print(f"\n{'='*50}")
        print(f"Ablation: {ablation_name} ({len(configs)} configs, {n_runs} runs)")
        print(f"{'='*50}")

        results = run_ablation_group(
            binary, ablation_name, configs,
            out_dir, tmp_dir, args.timeout, args.jobs
        )

        ok = sum(1 for r in results if r["ok"])
        print(f"\n  Succeeded: {ok}/{len(results)}")
        print_summary(ablation_name, results)


if __name__ == "__main__":
    main()
