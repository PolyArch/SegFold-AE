#!/usr/bin/env python3
"""Download SuiteSparse matrices needed for paper experiments.

Downloads .mtx files from the SuiteSparse Matrix Collection into
benchmarks/data/suitesparse/<name>/<name>.mtx.  Already-cached
matrices are skipped.

Usage:
    python3 scripts/download_matrices.py
    python3 scripts/download_matrices.py --matrix-dir /path/to/suitesparse
"""

import argparse
import os
import tarfile
import tempfile
import urllib.request
from pathlib import Path

SS_URL = "https://suitesparse-collection-website.herokuapp.com/MM/{group}/{name}.tar.gz"

# All matrices needed across all three experiments.
# (name, SuiteSparse group)
ALL_MATRICES = [
    # Square (overall + breakdown)
    ("bcsstk03", "HB"),
    ("bcsstk18", "HB"),
    ("bcspwr06", "HB"),
    ("ca-GrQc", "SNAP"),
    ("ca-CondMat", "SNAP"),
    ("tols4000", "Bai"),
    ("olm5000", "Bai"),
    ("rdb5000", "Bai"),
    ("fv1", "Norris"),
    ("flowmeter0", "Oberwolfach"),
    ("delaunay_n13", "DIMACS10"),
    ("poisson3Da", "FEMLAB"),
    # Non-square (overall + nonsquare + breakdown)
    ("gemat1", "HB"),
    ("psse1", "YCheng"),
    ("lp_woodw", "LPnetlib"),
    ("lp_d2q06c", "LPnetlib"),
    ("pcb3000", "Meszaros"),
    ("rosen10", "Meszaros"),
    ("Franz6", "JGD_Franz"),
    ("Franz8", "JGD_Franz"),
]


def download_matrix(name: str, group: str, matrix_dir: Path) -> Path:
    """Download a matrix from SuiteSparse if not cached. Returns .mtx path."""
    dest_dir = matrix_dir / name
    mtx_path = dest_dir / f"{name}.mtx"

    if mtx_path.exists():
        return mtx_path

    url = SS_URL.format(group=group, name=name)
    print(f"  Downloading {name} from {url} ...")
    dest_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(suffix=".tar.gz", delete=False) as tmp:
        tmp_path = tmp.name

    try:
        urllib.request.urlretrieve(url, tmp_path)
        with tarfile.open(tmp_path, "r:gz") as tar:
            for member in tar.getmembers():
                if member.name.endswith(".mtx"):
                    f = tar.extractfile(member)
                    if f:
                        with open(mtx_path, "wb") as out:
                            out.write(f.read())
                        break
    finally:
        os.unlink(tmp_path)

    if mtx_path.exists():
        print(f"    OK: {mtx_path}")
    else:
        print(f"    ERROR: failed to extract {name}")

    return mtx_path


def main():
    project_root = Path(__file__).resolve().parent.parent
    default_dir = project_root / "benchmarks" / "data" / "suitesparse"

    parser = argparse.ArgumentParser(description="Download SuiteSparse matrices")
    parser.add_argument("--matrix-dir", type=Path, default=default_dir,
                        help=f"Target directory (default: {default_dir})")
    args = parser.parse_args()

    matrix_dir = args.matrix_dir
    matrix_dir.mkdir(parents=True, exist_ok=True)

    total = len(ALL_MATRICES)
    downloaded = 0
    skipped = 0

    print("=" * 50)
    print(f"Downloading SuiteSparse matrices")
    print(f"  Target:   {matrix_dir}")
    print(f"  Matrices: {total}")
    print("=" * 50)

    for name, group in ALL_MATRICES:
        mtx_path = matrix_dir / name / f"{name}.mtx"
        if mtx_path.exists():
            print(f"  [skip] {name}")
            skipped += 1
        else:
            download_matrix(name, group, matrix_dir)
            if (matrix_dir / name / f"{name}.mtx").exists():
                downloaded += 1

    print()
    print("=" * 50)
    print(f"  Downloaded: {downloaded}")
    print(f"  Skipped:    {skipped}")
    print(f"  Total:      {total}")
    print("=" * 50)


if __name__ == "__main__":
    main()
