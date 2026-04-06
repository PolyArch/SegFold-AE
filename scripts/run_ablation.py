#!/usr/bin/env python3
"""Ablation Studies: Run SegFold with different configs.

Ablation groups on synthetic matrices:
  - window-size:    B loader window size sweep (1, 4, 8, 16, 32, 64)
  - k-reordering:   B row reordering strategies
  - crossbar-width: B loader row limit sweep (1, 2, 4, 8, 16)

Ablation groups on SuiteSparse matrices:
  - mapping-paper:       Ablation mapping with memory hierarchy
Usage:
    python3 scripts/run_ablation.py output/my_run
    python3 scripts/run_ablation.py output/my_run --ablation mapping-paper
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
        "window-32": "configs/ablation-baseline.yaml",
        "window-64": "configs/ablation-window-64.yaml",
    },
    "k-reordering": {
        "segfold":      "configs/ablation-baseline.yaml",
        "no-k-reorder": "configs/ablation-no-k-reordering.yaml",
        "multi-b-row":  "configs/ablation-multi-b-row-per-row.yaml",
    },
    "crossbar-width": {
        "brl-1":  "configs/ablation-brl-1.yaml",
        "brl-2":  "configs/ablation-brl-2.yaml",
        "brl-4":  "configs/ablation-baseline.yaml",
        "brl-8":  "configs/ablation-brl-8.yaml",
        "brl-16": "configs/ablation-brl-16.yaml",
    },
    "mapping-paper": {
        "segfold": "configs/ablation-map-paper-segfold.yaml",
        "ideal":   "configs/ablation-map-paper-ideal.yaml",
        "zero":    "configs/ablation-map-paper-zero.yaml",
    },
}

# Ablation groups that run on SuiteSparse matrices instead of synthetic
SUITESPARSE_ABLATIONS = {"mapping-paper"}

SUITESPARSE_MATRICES = [
    "fv1", "flowmeter0", "delaunay_n13",
    "ca-GrQc", "ca-CondMat", "poisson3Da",
    "bcspwr06", "tols4000", "rdb5000",
    "bcsstk03", "bcsstk18", "olm5000",
    "lp_d2q06c", "lp_woodw", "pcb3000", "rosen10",
]

SIZES = [256, 512, 1024]
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


def run_one_suitesparse(binary: Path, config: Path, ablation_name: str,
                        config_name: str, matrix: str,
                        matrix_dir: Path, out_dir: Path, tmp_dir: Path,
                        timeout: int) -> dict:
    """Run one SuiteSparse matrix simulation. Returns result dict."""
    mtx_path = matrix_dir / matrix / f"{matrix}.mtx"
    if not mtx_path.exists():
        print(f"  [MISS] {config_name}/{matrix}: {mtx_path} not found")
        return {"ablation": ablation_name, "config": config_name,
                "matrix": matrix, "cycles": -1, "ok": False}

    run_tmp = tmp_dir / f"{ablation_name}_{config_name}_{matrix}"
    run_tmp.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(binary),
        "--config", str(config),
        "--mtx-file", str(mtx_path),
        "--tmp-dir", str(run_tmp),
    ]

    result = {
        "ablation": ablation_name, "config": config_name,
        "matrix": matrix, "cycles": -1, "ok": False,
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
            print(f"  [FAIL] {config_name}/{matrix}: rc={proc.returncode}")
            return result

        # Move stats to output dir
        sub_dir = out_dir / config_name
        sub_dir.mkdir(parents=True, exist_ok=True)
        latest = find_latest_stats(run_tmp)
        if latest:
            dest = sub_dir / f"sim_{matrix}_stats.json"
            shutil.move(str(latest), str(dest))
            cfg_json = latest.with_name(latest.name.replace("_stats.json", "_config.json"))
            if cfg_json.exists():
                shutil.move(str(cfg_json), str(sub_dir / f"sim_{matrix}_config.json"))

        result["ok"] = True
        print(f"  [OK]   {config_name}/{matrix}: {cycles} cycles")
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {config_name}/{matrix}")
    except Exception as e:
        print(f"  [ERROR] {config_name}/{matrix}: {e}")

    return result


def run_ablation_group(binary, ablation_name, configs, out_dir, tmp_dir, timeout, jobs,
                       matrix_dir=None):
    """Run all configs x matrices for one ablation group."""
    abl_out = out_dir / ablation_name
    abl_out.mkdir(parents=True, exist_ok=True)

    if ablation_name in SUITESPARSE_ABLATIONS:
        return run_suitesparse_ablation(binary, ablation_name, configs,
                                        abl_out, tmp_dir, timeout, jobs, matrix_dir)

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


def run_suitesparse_ablation(binary, ablation_name, configs, abl_out,
                              tmp_dir, timeout, jobs, matrix_dir):
    """Run all configs x SuiteSparse matrices for a ablation mapping."""
    tasks = []
    for config_name, config_path in configs.items():
        config = PROJECT_ROOT / config_path
        if not config.exists():
            print(f"  [SKIP] Config not found: {config}")
            continue
        for matrix in SUITESPARSE_MATRICES:
            tasks.append((binary, config, ablation_name, config_name,
                          matrix, matrix_dir, abl_out, tmp_dir, timeout))

    results = []
    if jobs <= 1:
        for i, args in enumerate(tasks):
            print(f"[{i+1}/{len(tasks)}]", end="")
            results.append(run_one_suitesparse(*args))
    else:
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = {executor.submit(run_one_suitesparse, *args): args for args in tasks}
            for future in as_completed(futures):
                results.append(future.result())


    return results


def print_summary(ablation_name, results):
    """Print summary table."""
    if ablation_name in SUITESPARSE_ABLATIONS:
        print_summary_suitesparse(results)
    else:
        print_summary_synthetic(results)


def print_summary_suitesparse(results):
    """Print summary table for SuiteSparse ablation."""
    configs = sorted(set(r["config"] for r in results))
    print(f"\n{'':20s}", end="")
    for cfg in configs:
        print(f" {cfg:>14s}", end="")
    print()
    print("-" * (20 + 15 * len(configs)))

    for mat in SUITESPARSE_MATRICES:
        print(f"{mat:20s}", end="")
        for cfg in configs:
            match = [r for r in results if r["config"] == cfg and r["matrix"] == mat]
            if match and match[0]["cycles"] > 0:
                print(f" {match[0]['cycles']:>14}", end="")
            else:
                print(f" {'FAIL':>14s}", end="")
        print()


def print_summary_synthetic(results):
    """Print summary table for synthetic ablation."""
    from collections import defaultdict
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
    parser.add_argument("--matrix-dir", type=Path,
                        default=PROJECT_ROOT / "benchmarks" / "data" / "suitesparse")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=3600)
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

    print("=" * 50)
    print("Ablation Study Experiments")
    print(f"  ablations:  {list(ablations.keys())}")
    print(f"  jobs:       {args.jobs}")
    print(f"  output:     {out_dir}")
    print("=" * 50)

    for ablation_name, configs in ablations.items():
        if ablation_name in SUITESPARSE_ABLATIONS:
            n_runs = len(configs) * len(SUITESPARSE_MATRICES)
        else:
            n_runs = len(configs) * len(SIZES) * len(DENSITIES) * len(SEEDS)
        print(f"\n{'='*50}")
        print(f"Ablation: {ablation_name} ({len(configs)} configs, {n_runs} runs)")
        print(f"{'='*50}")

        results = run_ablation_group(
            binary, ablation_name, configs,
            out_dir, tmp_dir, args.timeout, args.jobs,
            matrix_dir=args.matrix_dir,
        )

        ok = sum(1 for r in results if r["ok"])
        print(f"\n  Succeeded: {ok}/{len(results)}")
        print_summary(ablation_name, results)


if __name__ == "__main__":
    main()
