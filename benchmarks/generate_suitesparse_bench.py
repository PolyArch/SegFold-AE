import os
import json
import argparse
from pathlib import Path
import numpy as np
from scipy.io import loadmat, mmread
from scipy.sparse import coo_matrix
import mat73

def ensure_dir(p): 
    os.makedirs(p, exist_ok=True)

def save_arr(path, arr: np.ndarray):
    np.save(path, arr.astype(np.float32), allow_pickle=False)

def load_suite_sparse_data_mat(file_path):
    try:
        mat_data = loadmat(file_path)
        problem_struct = mat_data["Problem"]
        A = problem_struct["A"][0, 0]
    except NotImplementedError:
        mat_data = mat73.loadmat(file_path)
        problem_struct = mat_data["Problem"]
        A = problem_struct["A"]
    return coo_matrix(A).toarray(), coo_matrix(A).toarray()

def load_suitesparse_matrix(data_dir: str, matrix_name: str) -> tuple[np.ndarray, np.ndarray]:
    mat_file_dir = os.path.join(data_dir, matrix_name)
    mat_file_path = os.path.join(mat_file_dir, f"{matrix_name}.mtx")
    if not os.path.exists(mat_file_path):
        raise FileNotFoundError(f"Matrix file {mat_file_path} not found")
    mat = mmread(mat_file_path)
    mat_b_file_path = os.path.join(mat_file_dir, f"{matrix_name}_b.mtx")
    if not os.path.exists(mat_b_file_path):
        return mat.toarray(), mat.toarray()
    mat_b = mmread(mat_b_file_path)
    return mat.toarray(), mat_b.toarray()

def find_suitesparse_matrices(suitesparse_dir):
    suitesparse_path = Path(suitesparse_dir)
    if not suitesparse_path.exists():
        raise ValueError(f"SuiteSparse directory not found: {suitesparse_dir}")
    
    mat_files = list(suitesparse_path.glob("*.mat"))
    if not mat_files:
        raise ValueError(f"No .mat files found in {suitesparse_dir}")
    
    return sorted(mat_files)

def calculate_density(matrix):
    return np.count_nonzero(matrix) / matrix.size

def process_matrix(mat_file_path, output_dir):
    A, B = load_suite_sparse_data_mat(str(mat_file_path))
    A, B = A.astype(np.float32), B.astype(np.float32)
    
    matrix_name = mat_file_path.stem
    wid = f"suitesparse-{matrix_name}"
    
    metadata = {
        "id": wid,
        "kind": "suitesparse",
        "source_file": mat_file_path.name,
        "m": int(A.shape[0]),
        "k": int(A.shape[1]), 
        "n": int(B.shape[1]),
        "densityA": float(calculate_density(A)),
        "densityB": float(calculate_density(B))
    }
    
    matrix_out_dir = os.path.join(output_dir, wid)
    ensure_dir(matrix_out_dir)
    
    save_arr(os.path.join(matrix_out_dir, "A.npy"), A)
    save_arr(os.path.join(matrix_out_dir, "B.npy"), B)
    
    with open(os.path.join(matrix_out_dir, "meta.json"), "w") as f:
        json.dump(metadata, f, indent=2)
    
    return metadata

def main():
    parser = argparse.ArgumentParser(description="Generate SuiteSparse benchmark data")
    parser.add_argument("--out", default="out_suitesparse", help="Output directory")
    parser.add_argument("--suitesparse-dir", default=None, help="SuiteSparse matrices directory")
    parser.add_argument("--matrix", default=None, help="Process specific matrix file")
    parser.add_argument("--list", action="store_true", help="List available matrices")
    
    args = parser.parse_args()
    
    script_dir = Path(__file__).parent
    
    if args.suitesparse_dir:
        suitesparse_dir = args.suitesparse_dir
    else:
        suitesparse_dir = script_dir / "suitesparse"
    
    # Make output directory relative to benchmark folder
    if not os.path.isabs(args.out):
        output_dir = script_dir / args.out
    else:
        output_dir = Path(args.out)
    
    mat_files = find_suitesparse_matrices(suitesparse_dir)
    
    if args.list:
        for i, mat_file in enumerate(mat_files, 1):
            print(f"{i}. {mat_file.name}")
        return
    
    if args.matrix:
        mat_files = [f for f in mat_files if f.name == args.matrix]
        if not mat_files:
            print(f"Matrix '{args.matrix}' not found")
            return
    
    ensure_dir(output_dir)
    
    all_metadata = []
    for mat_file in mat_files:
        try:
            metadata = process_matrix(mat_file, str(output_dir))
            all_metadata.append(metadata)
        except Exception as e:
            print(f"Failed to process {mat_file.name}: {e}")
    
    manifest = {"benchmarks": all_metadata, "total_matrices": len(all_metadata)}
    with open(os.path.join(str(output_dir), "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    
    print(f"Processed {len(all_metadata)} matrices → {output_dir}")

if __name__ == "__main__":
    main()