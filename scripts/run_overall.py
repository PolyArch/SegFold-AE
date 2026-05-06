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
            timeout: int, label: str = "") -> tuple:
    """Run csegfold --mtx-file for one matrix. Returns (matrix, cycles, ok).

    label is an optional tag (e.g. "ideal") used to disambiguate tmp dirs and
    log lines when the same matrix is run with multiple configs.
    """
    mtx_path = matrix_dir / matrix / f"{matrix}.mtx"
    if not mtx_path.exists():
        print(f"  [MISS] {matrix}: {mtx_path} not found")
        return (matrix, -1, False)

    # Use per-matrix tmp dir to avoid file conflicts when running in parallel
    tag = f"{matrix}_{label}" if label else matrix
    mat_tmp_dir = tmp_dir / tag
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

        prefix = f"{label}/" if label else ""
        if result.returncode != 0:
            print(f"  [FAIL] {prefix}{matrix}: returncode={result.returncode}")
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

        print(f"  [OK]   {prefix}{matrix}: {cycles} cycles")
        return (matrix, cycles, True)
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {label}/{matrix}" if label else f"  [TIMEOUT] {matrix}")
        return (matrix, -1, False)
    except Exception as e:
        print(f"  [ERROR] {label}/{matrix}: {e}" if label else f"  [ERROR] {matrix}: {e}")
        return (matrix, -1, False)


def run_variant(binary, label, config, config_ir, matrix_dir, out_root, tmp_dir,
                jobs, timeout):
    """Run all MATRICES under one (config, config_ir) pair, in out_root/<label>/."""
    out_dir = out_root / label if label else out_root
    out_dir.mkdir(parents=True, exist_ok=True)

    print()
    print(f"--- variant: {label or 'segfold'} ---")
    print(f"  config:      {config}")
    print(f"  config (IR): {config_ir}")
    print(f"  output:      {out_dir}")

    results = {}
    if jobs <= 1:
        for i, mat in enumerate(MATRICES):
            cfg = config_ir if mat in IR_MATRICES else config
            print(f"[{i+1}/{len(MATRICES)}] ({cfg.name})", end=" ")
            mat, cycles, ok = run_one(binary, cfg, mat, matrix_dir, out_dir,
                                       tmp_dir, timeout, label=label)
            results[mat] = cycles
    else:
        futures = {}
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            for mat in MATRICES:
                cfg = config_ir if mat in IR_MATRICES else config
                future = executor.submit(run_one, binary, cfg, mat, matrix_dir,
                                         out_dir, tmp_dir, timeout, label)
                futures[future] = mat
            for future in as_completed(futures):
                mat, cycles, ok = future.result()
                results[mat] = cycles
    return results


def main():
    parser = argparse.ArgumentParser(description="Overall performance experiment")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--config", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold.yaml")
    parser.add_argument("--config-ir", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold-ir.yaml")
    parser.add_argument("--config-ideal", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold-ideal.yaml")
    parser.add_argument("--config-ir-ideal", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold-ir-ideal.yaml")
    parser.add_argument("--include-ideal", action="store_true",
                        help="Also run the paper's ideal SegFold (per-element "
                             "injection, no LUT, no crossbar bandwidth bound)")
    parser.add_argument("--ideal-only", action="store_true",
                        help="Run only the ideal variant (skip realistic)")
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

    out_root = args.output_dir / "overall"
    out_root.mkdir(parents=True, exist_ok=True)

    variants = []
    if not args.ideal_only:
        variants.append(("", args.config, args.config_ir))
    if args.include_ideal or args.ideal_only:
        variants.append(("ideal", args.config_ideal, args.config_ir_ideal))

    print("=" * 50)
    print("Overall Performance Experiment")
    print(f"  matrices:    {len(MATRICES)}")
    print(f"  IR matrices: {sorted(IR_MATRICES)}")
    print(f"  variants:    {[v[0] or 'segfold' for v in variants]}")
    print(f"  jobs:        {args.jobs}")
    print(f"  output:      {out_root}")
    print("=" * 50)

    all_results = {}
    for label, cfg, cfg_ir in variants:
        all_results[label or "segfold"] = run_variant(
            binary, label, cfg, cfg_ir, args.matrix_dir, out_root,
            tmp_dir, args.jobs, args.timeout,
        )

    print()
    print("=" * 50)
    print("Summary")
    print("=" * 50)
    labels = list(all_results.keys())
    header = f"  {'matrix':20s}" + "".join(f" {lab:>14s}" for lab in labels)
    print(header)
    print("  " + "-" * (20 + 15 * len(labels)))
    for mat in MATRICES:
        row = f"  {mat:20s}"
        for lab in labels:
            c = all_results[lab].get(mat, -1)
            row += f" {c:>14}" if c > 0 else f" {'FAIL':>14s}"
        print(row)


if __name__ == "__main__":
    main()
