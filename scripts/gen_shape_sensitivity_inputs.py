#!/usr/bin/env python3
"""Generate inputs for the shape sensitivity study.

Two output families:
  1. Synthetic K x K uniform-sparse MTX matrices (one per (K, density, seed)),
     written to benchmarks/data/synthetic_square/K{K}_d{density:.6f}_s{seed}.mtx.
  2. Per-real-matrix transpose MTX files (one per real matrix), written to
     benchmarks/data/suitesparse/<name>/<name>_T.mtx.

Both are idempotent: existing destination files are skipped.

Usage:
    python3 scripts/gen_shape_sensitivity_inputs.py
    python3 scripts/gen_shape_sensitivity_inputs.py --matrices Franz6 --densities 0.0005
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import scipy.io
import scipy.sparse

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Mirror of run_nonsquare.py:25-32.
MATRIX_DIMS = {
    "lp_woodw": (1098, 8418),
    "pcb3000":  (3960, 7732),
    "gemat1":   (4929, 10595),
    "Franz6":   (7576, 3016),
    "Franz8":   (16728, 7176),
    "psse1":    (14318, 11028),
}

DEFAULT_DENSITIES = [0.0005, 0.001, 0.002, 0.004, 0.008, 0.016, 0.032]
DEFAULT_SEEDS = [42]


def gen_uniform_square_mtx(K: int, density: float, seed: int, out_path: Path) -> bool:
    """Write a K x K uniform-sparse MTX to out_path. Returns True if written."""
    if out_path.exists():
        return False
    out_path.parent.mkdir(parents=True, exist_ok=True)
    mat = scipy.sparse.random(
        K, K, density=density, format="coo",
        random_state=seed,
        data_rvs=lambda n: np.ones(n, dtype=np.int32),
    )
    scipy.io.mmwrite(str(out_path), mat, field="integer", symmetry="general")
    return True


def gen_transpose_mtx(src_path: Path, dst_path: Path) -> bool:
    """Write the transpose of an MTX file to dst_path. Returns True if written."""
    if dst_path.exists():
        return False
    if not src_path.exists():
        print(f"  [MISS] source MTX not found: {src_path}", file=sys.stderr)
        return False
    mat = scipy.io.mmread(str(src_path)).tocoo()
    mat_t = mat.transpose().tocoo()
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    scipy.io.mmwrite(str(dst_path), mat_t, field="integer", symmetry="general")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrices", nargs="+", default=list(MATRIX_DIMS.keys()),
                        help="Real matrix names (subset of %(default)s).")
    parser.add_argument("--densities", nargs="+", type=float, default=DEFAULT_DENSITIES)
    parser.add_argument("--seeds", nargs="+", type=int, default=DEFAULT_SEEDS)
    parser.add_argument("--out-synth-dir", type=Path,
                        default=PROJECT_ROOT / "benchmarks" / "data" / "synthetic_square")
    parser.add_argument("--out-suitesparse-dir", type=Path,
                        default=PROJECT_ROOT / "benchmarks" / "data" / "suitesparse")
    args = parser.parse_args()

    unknown = [m for m in args.matrices if m not in MATRIX_DIMS]
    if unknown:
        print(f"Error: unknown matrices: {unknown}", file=sys.stderr)
        sys.exit(1)

    unique_K = sorted({MATRIX_DIMS[m][1] for m in args.matrices})

    print("=" * 60)
    print("Shape Sensitivity: input generation")
    print(f"  matrices : {args.matrices}")
    print(f"  K values : {unique_K}")
    print(f"  densities: {args.densities}")
    print(f"  seeds    : {args.seeds}")
    print(f"  synth out: {args.out_synth_dir}")
    print(f"  ssp out  : {args.out_suitesparse_dir}/<name>/<name>_T.mtx")
    print("=" * 60)

    n_synth_written = n_synth_skipped = 0
    for K in unique_K:
        for density in args.densities:
            for seed in args.seeds:
                fname = f"K{K}_d{density:.6f}_s{seed}.mtx"
                out_path = args.out_synth_dir / fname
                if gen_uniform_square_mtx(K, density, seed, out_path):
                    n_synth_written += 1
                    print(f"  [WRITE] {out_path}")
                else:
                    n_synth_skipped += 1

    n_t_written = n_t_skipped = 0
    for matrix in args.matrices:
        src = args.out_suitesparse_dir / matrix / f"{matrix}.mtx"
        dst = args.out_suitesparse_dir / matrix / f"{matrix}_T.mtx"
        if gen_transpose_mtx(src, dst):
            n_t_written += 1
            print(f"  [WRITE] {dst}")
        else:
            n_t_skipped += 1

    print()
    print(f"Synthetic squares: wrote {n_synth_written}, skipped {n_synth_skipped}")
    print(f"Transposes      : wrote {n_t_written}, skipped {n_t_skipped}")


if __name__ == "__main__":
    main()
