#!/usr/bin/env python3
"""Overall Performance: Run SegFold on 11 SuiteSparse matrices.

Reproduces the paper's overall speedup comparison figure.
Only SegFold is simulated; Spada/Flexagon baselines are pre-computed
in data/baselines/overall_baselines.csv.

Uses csegfold --mtx-file for CSR-only simulation (no dense conversion).

Usage:
    python3 scripts/run_overall.py output/my_run
    python3 scripts/run_overall.py output/my_run --jobs 4
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

MATRICES = [
    "fv1", "flowmeter0", "delaunay_n13",
    "ca-GrQc", "ca-CondMat", "poisson3Da",
    "bcspwr06", "tols4000", "rdb5000",
    "psse1", "gemat1",
]

# Matrices that use segfold-ir.yaml (irregular, need decompose_a_row / larger tiles)
IR_MATRICES = {"ca-GrQc", "ca-CondMat", "poisson3Da"}

CYCLE_RE = re.compile(r"completed successfully in (\d+) cycles")


def find_latest_stats(tmp_dir: Path) -> Path | None:
    """Find the most recently created *_stats.json in tmp_dir."""
    files = sorted(tmp_dir.glob("run_*_stats.json"), key=os.path.getmtime, reverse=True)
    return files[0] if files else None


def run_one(binary: Path, config: Path, matrix: str,
            matrix_dir: Path, out_dir: Path, tmp_dir: Path,
            timeout: int) -> tuple:
    """Run csegfold --mtx-file for one matrix. Returns (matrix, cycles, ok)."""
    mtx_path = matrix_dir / matrix / f"{matrix}.mtx"
    if not mtx_path.exists():
        print(f"  [MISS] {matrix}: {mtx_path} not found")
        return (matrix, -1, False)

    # Use per-matrix tmp dir to avoid file conflicts when running in parallel
    mat_tmp_dir = tmp_dir / matrix
    mat_tmp_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(binary),
        "--config", str(config),
        "--mtx-file", str(mtx_path),
        "--tmp-dir", str(mat_tmp_dir),
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
            cwd=str(PROJECT_ROOT),
        )
        m = CYCLE_RE.search(result.stdout)
        cycles = int(m.group(1)) if m else -1

        if result.returncode != 0:
            print(f"  [FAIL] {matrix}: returncode={result.returncode}")
            if result.stderr:
                lines = result.stderr.strip().split("\n")
                print(f"         {lines[-1][:200]}")
            return (matrix, -1, False)

        # Move stats JSON from per-matrix tmp/ to out_dir
        latest = find_latest_stats(mat_tmp_dir)
        if latest:
            dest = out_dir / f"sim_{matrix}_stats.json"
            shutil.move(str(latest), str(dest))
            config_json = latest.with_name(latest.name.replace("_stats.json", "_config.json"))
            if config_json.exists():
                shutil.move(str(config_json), str(out_dir / f"sim_{matrix}_config.json"))

        print(f"  [OK]   {matrix}: {cycles} cycles")
        return (matrix, cycles, True)
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {matrix}")
        return (matrix, -1, False)
    except Exception as e:
        print(f"  [ERROR] {matrix}: {e}")
        return (matrix, -1, False)


def main():
    parser = argparse.ArgumentParser(description="Overall performance experiment")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--config", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold.yaml")
    parser.add_argument("--config-ir", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold-ir.yaml")
    parser.add_argument("--matrix-dir", type=Path,
                        default=PROJECT_ROOT / "benchmarks" / "data" / "suitesparse")
    parser.add_argument("--timeout", type=int, default=3600)
    args = parser.parse_args()

    binary = PROJECT_ROOT / "csegfold" / "build" / "csegfold"
    if not binary.exists():
        print(f"Error: {binary} not found. Run scripts/setup.sh first.")
        sys.exit(1)

    tmp_dir = PROJECT_ROOT / "csegfold" / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)

    out_dir = args.output_dir / "overall"
    out_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 50)
    print("Overall Performance Experiment")
    print(f"  matrices: {len(MATRICES)}")
    print(f"  config:      {args.config}")
    print(f"  config (IR): {args.config_ir}")
    print(f"  IR matrices: {sorted(IR_MATRICES)}")
    print(f"  jobs:     {args.jobs}")
    print(f"  output:   {out_dir}")
    print("=" * 50)

    results = {}
    if args.jobs <= 1:
        for i, mat in enumerate(MATRICES):
            cfg = args.config_ir if mat in IR_MATRICES else args.config
            print(f"[{i+1}/{len(MATRICES)}] ({cfg.name})", end=" ")
            mat, cycles, ok = run_one(binary, cfg, mat,
                                       args.matrix_dir, out_dir, tmp_dir,
                                       args.timeout)
            results[mat] = cycles
    else:
        futures = {}
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            for mat in MATRICES:
                cfg = args.config_ir if mat in IR_MATRICES else args.config
                future = executor.submit(run_one, binary, cfg, mat,
                                         args.matrix_dir, out_dir, tmp_dir,
                                         args.timeout)
                futures[future] = mat
            for future in as_completed(futures):
                mat, cycles, ok = future.result()
                results[mat] = cycles

    print()
    print("=" * 50)
    print("Summary")
    print("=" * 50)
    ok = sum(1 for c in results.values() if c > 0)
    print(f"  Succeeded: {ok}/{len(MATRICES)}")
    for mat in MATRICES:
        c = results.get(mat, -1)
        status = f"{c:>10} cycles" if c > 0 else "     FAILED"
        print(f"    {mat:20s} {status}")


if __name__ == "__main__":
    main()
