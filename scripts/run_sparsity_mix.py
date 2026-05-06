#!/usr/bin/env python3
"""Sparsity-mix sensitivity study: square synthetic A and B with independently varied densities.

For a fixed square size K, sweeps the full d_A x d_B grid. Each cell is one
csegfold run with both A and B generated synthetically by the binary itself
(--M --K --N --density-a --density-b --seed); no MTX files are written.

Output filename encodes (K, d_A, d_B, seed) in decimal so we can represent
densities below 1% (the existing percent-encoded scheme cannot).

Usage:
    python3 -u scripts/run_sparsity_mix.py output/sparsity_mix_<ts>
    python3 -u scripts/run_sparsity_mix.py output/sparsity_mix_<ts> \
        --K 4096 --densities 0.0005 0.001 0.008 --seeds 42 --jobs 4
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

DEFAULT_K = 4096
DEFAULT_DENSITIES = [0.0005, 0.001, 0.002, 0.004, 0.008, 0.016, 0.032, 0.064, 0.128]
DEFAULT_SEEDS = [42]

CYCLE_RE = re.compile(r"completed successfully in (\d+) cycles")


def find_latest_stats(tmp_dir: Path) -> Path | None:
    files = sorted(tmp_dir.glob("run_*_stats.json"), key=os.path.getmtime, reverse=True)
    return files[0] if files else None


def run_one(binary: Path, config: Path, K: int, dA: float, dB: float, seed: int,
            out_dir: Path, tmp_root: Path, timeout: int) -> tuple:
    cell_id = f"K{K}_dA{dA:.6f}_dB{dB:.6f}_s{seed}"
    cell_tmp = tmp_root / cell_id
    cell_tmp.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(binary),
        "--config", str(config),
        "--M", str(K), "--K", str(K), "--N", str(K),
        "--density-a", str(dA), "--density-b", str(dB),
        "--seed", str(seed),
        "--tmp-dir", str(cell_tmp),
    ]

    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
            cwd=str(PROJECT_ROOT),
        )
        m = CYCLE_RE.search(proc.stdout)
        cycles = int(m.group(1)) if m else -1

        if proc.returncode != 0:
            print(f"  [FAIL] {cell_id}: rc={proc.returncode}", flush=True)
            if proc.stderr:
                tail = proc.stderr.strip().split("\n")[-1][:200]
                print(f"         {tail}", flush=True)
            return (cell_id, -1, False)

        latest = find_latest_stats(cell_tmp)
        if latest:
            dest = out_dir / f"sim_{cell_id}_stats.json"
            shutil.move(str(latest), str(dest))
            cfg_src = latest.with_name(latest.name.replace("_stats.json", "_config.json"))
            if cfg_src.exists():
                shutil.move(str(cfg_src), str(out_dir / f"sim_{cell_id}_config.json"))

        print(f"  [OK]   {cell_id}: {cycles} cycles", flush=True)
        return (cell_id, cycles, True)
    except subprocess.TimeoutExpired:
        print(f"  [TIMEOUT] {cell_id}", flush=True)
        return (cell_id, -1, False)
    except Exception as e:
        print(f"  [ERROR] {cell_id}: {e}", flush=True)
        return (cell_id, -1, False)


def main():
    parser = argparse.ArgumentParser(description="Sparsity-mix sensitivity sweep")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--config", type=Path,
                        default=PROJECT_ROOT / "configs" / "segfold-dtonly.yaml")
    parser.add_argument("--K", type=int, default=DEFAULT_K,
                        help=f"Square matrix size (M=K=N). Default {DEFAULT_K}.")
    parser.add_argument("--densities", nargs="+", type=float, default=DEFAULT_DENSITIES,
                        help="Density grid for both A and B (cross-product).")
    parser.add_argument("--seeds", nargs="+", type=int, default=DEFAULT_SEEDS)
    parser.add_argument("--timeout", type=int, default=7200)
    parser.add_argument("--log-file", type=Path, default=None,
                        help="Optional: also append all output to this file (in addition to stdout).")
    args = parser.parse_args()

    binary = PROJECT_ROOT / "csegfold" / "build" / "csegfold"
    if not binary.exists():
        print(f"Error: {binary} not found.", file=sys.stderr); sys.exit(1)
    if not args.config.exists():
        print(f"Error: config not found: {args.config}", file=sys.stderr); sys.exit(1)

    config_name = args.config.stem
    out_root = args.output_dir / "sparsity_mix" / config_name
    out_root.mkdir(parents=True, exist_ok=True)
    tmp_root = PROJECT_ROOT / "csegfold" / "tmp" / "sparsity_mix" / config_name
    tmp_root.mkdir(parents=True, exist_ok=True)

    if args.log_file:
        args.log_file.parent.mkdir(parents=True, exist_ok=True)
        log_fh = open(args.log_file, "a", buffering=1)  # line-buffered

        class Tee:
            def __init__(self, *streams): self.streams = streams
            def write(self, s):
                for st in self.streams: st.write(s)
            def flush(self):
                for st in self.streams: st.flush()
        sys.stdout = Tee(sys.stdout, log_fh)

    tasks = [(args.K, dA, dB, seed)
             for dA in args.densities for dB in args.densities for seed in args.seeds]

    print("=" * 60, flush=True)
    print("Sparsity-Mix Sensitivity Experiment", flush=True)
    print(f"  K          : {args.K}", flush=True)
    print(f"  densities  : {args.densities}", flush=True)
    print(f"  seeds      : {args.seeds}", flush=True)
    print(f"  config     : {args.config}", flush=True)
    print(f"  output     : {out_root}", flush=True)
    print(f"  total runs : {len(tasks)}", flush=True)
    print("=" * 60, flush=True)

    results = {}

    def submit(t):
        K, dA, dB, seed = t
        return run_one(binary, args.config, K, dA, dB, seed,
                       out_root, tmp_root, args.timeout)

    if args.jobs <= 1:
        for i, t in enumerate(tasks):
            print(f"[{i+1}/{len(tasks)}]", end=" ", flush=True)
            cell_id, cycles, _ = submit(t)
            results[cell_id] = cycles
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futures = {ex.submit(submit, t): t for t in tasks}
            for fut in as_completed(futures):
                cell_id, cycles, _ = fut.result()
                results[cell_id] = cycles

    print(flush=True)
    print("=" * 60, flush=True)
    print("Summary", flush=True)
    print("=" * 60, flush=True)
    ok = sum(1 for c in results.values() if c > 0)
    print(f"  Succeeded: {ok}/{len(tasks)}", flush=True)
    for K, dA, dB, seed in tasks:
        cell_id = f"K{K}_dA{dA:.6f}_dB{dB:.6f}_s{seed}"
        c = results.get(cell_id, -1)
        status = f"{c:>10} cycles" if c > 0 else "     FAILED"
        print(f"    {cell_id:55s} {status}", flush=True)


if __name__ == "__main__":
    main()
