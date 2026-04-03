#!/usr/bin/env python3
"""Speedup Breakdown: Run 5 incremental ablation configs on 12 matrices.

Reproduces the paper's stacked bar chart showing the contribution of
each optimization (SegmentBC, Spatial Folding, IPM LUT, SelectA).

Uses csegfold --mtx-file for CSR-only simulation (no dense conversion).

Usage:
    python3 scripts/run_breakdown.py output/my_run
    python3 scripts/run_breakdown.py output/my_run --jobs 4
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

CONFIGS = [
    "breakdown-base",
    "breakdown-plus-tiling",
    "breakdown-plus-folding",
    "breakdown-plus-dynmap",
    "segfold",
]

MATRICES = [
    "bcsstk03", "bcspwr06", "ca-GrQc", "tols4000",
    "olm5000", "fv1", "bcsstk18", "lp_d2q06c",
    "lp_woodw", "gemat1", "rosen10", "pcb3000",
]

CYCLE_RE = re.compile(r"completed successfully in (\d+) cycles")


def find_latest_stats(tmp_dir: Path) -> Path | None:
    files = sorted(tmp_dir.glob("run_*_stats.json"), key=os.path.getmtime, reverse=True)
    return files[0] if files else None


def run_one(binary: Path, config: Path, config_name: str, matrix: str,
            matrix_dir: Path, out_dir: Path, tmp_dir: Path,
            timeout: int) -> tuple:
    mtx_path = matrix_dir / matrix / f"{matrix}.mtx"
    if not mtx_path.exists():
        print(f"  [MISS] {config_name}/{matrix}: {mtx_path} not found")
        return (config_name, matrix, -1)

    sub_dir = out_dir / config_name
    sub_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(binary),
        "--config", str(config),
        "--mtx-file", str(mtx_path),
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
            cwd=str(PROJECT_ROOT),
        )
        m = CYCLE_RE.search(result.stdout)
        cycles = int(m.group(1)) if m else -1

        if result.returncode != 0:
            print(f"  [FAIL] {config_name}/{matrix}: rc={result.returncode}")
            return (config_name, matrix, -1)

        latest = find_latest_stats(tmp_dir)
        if latest:
            dest = sub_dir / f"sim_{matrix}_stats.json"
            shutil.move(str(latest), str(dest))
            config_json = latest.with_name(latest.name.replace("_stats.json", "_config.json"))
            if config_json.exists():
                shutil.move(str(config_json), str(sub_dir / f"sim_{matrix}_config.json"))

        print(f"  [OK]   {config_name}/{matrix}: {cycles} cycles")
        return (config_name, matrix, cycles)
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {config_name}/{matrix}")
        return (config_name, matrix, -2)
    except Exception as e:
        print(f"  [ERROR] {config_name}/{matrix}: {e}")
        return (config_name, matrix, -1)


def main():
    parser = argparse.ArgumentParser(description="Speedup breakdown experiment")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--matrix-dir", type=Path,
                        default=PROJECT_ROOT / "benchmarks" / "data" / "suitesparse")
    parser.add_argument("--timeout", type=int, default=3600)
    args = parser.parse_args()

    binary = PROJECT_ROOT / "csegfold" / "build" / "csegfold"
    if not binary.exists():
        print(f"Error: {binary} not found. Run scripts/setup.sh first.")
        sys.exit(1)

    config_paths = {}
    for name in CONFIGS:
        path = PROJECT_ROOT / "configs" / f"{name}.yaml"
        if not path.exists():
            print(f"Error: config not found: {path}")
            sys.exit(1)
        config_paths[name] = path

    tmp_dir = PROJECT_ROOT / "csegfold" / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)

    out_dir = args.output_dir / "breakdown"
    out_dir.mkdir(parents=True, exist_ok=True)

    total = len(CONFIGS) * len(MATRICES)
    print("=" * 50)
    print("Speedup Breakdown Experiment")
    print(f"  configs:    {len(CONFIGS)}")
    print(f"  matrices:   {len(MATRICES)}")
    print(f"  total runs: {total}")
    print(f"  output:     {out_dir}")
    print("=" * 50)

    # Run sequentially (tmp/ file conflicts with parallel)
    results = {}
    count = 0
    for config_name in CONFIGS:
        print(f"\n--- Config: {config_name} ---")
        for matrix in MATRICES:
            count += 1
            print(f"[{count}/{total}]", end=" ")
            cfg, mat, cycles = run_one(
                binary, config_paths[config_name], config_name, matrix,
                args.matrix_dir, out_dir, tmp_dir, args.timeout
            )
            results[(cfg, mat)] = cycles

    # Print summary table
    print()
    print("=" * 50)
    print("Summary")
    print("=" * 50)

    header = f"{'matrix':20s}"
    for cfg in CONFIGS:
        short = cfg.replace("breakdown-", "").replace("plus-", "+")
        header += f" {short:>12s}"
    print(header)
    print("-" * len(header))

    for mat in MATRICES:
        row = f"{mat:20s}"
        for cfg in CONFIGS:
            c = results.get((cfg, mat), -1)
            row += f" {c:>12}" if c > 0 else f" {'FAIL':>12}"
        print(row)

    ok = sum(1 for c in results.values() if c > 0)
    print(f"\nSucceeded: {ok}/{total}")


if __name__ == "__main__":
    main()
