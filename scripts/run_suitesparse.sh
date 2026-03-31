#!/bin/bash
set -e

# SuiteSparse Matrix Experiments: run the C++ ablation_runner on real-world
# sparse matrices from the SuiteSparse collection.

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# --- Parameters (override via env vars or positional arg) ---
ABLATION_RUNNER="${ABLATION_RUNNER:-$PROJECT_ROOT/csegfold/build/ablation_runner}"
CONFIG="${CONFIG:-$PROJECT_ROOT/configs/segfold.yaml}"
MAX_JOBS="${MAX_JOBS:-2}"
MATRIX_DIR="${MATRIX_DIR:-$PROJECT_ROOT/benchmarks/data/suitesparse}"

OUT_DIR="${1:?Usage: $0 <OUT_DIR>}"

# Matrices that fit in dense conversion (up to ~10k rows).
# Excluded: bcsstk17 (10974x10974), ckt11752_dc_1 (49702x49702) -- too large.
matrices=(
    "bcsstk01"
    "bcsstk03"
    "bcsstk08"
    "1138_bus"
    "bcspwr06"
    "bcspwr09"
    "gre_1107"
    "mbeacxc"
    "mbeaflw"
    "orani678"
    "qc2534"
    "tols4000"
    "G57"
    "olm5000"
    "rdb5000"
    "bcsstk16"
    "ash219"
)

# --- Validate prerequisites ---
if [ ! -f "$ABLATION_RUNNER" ]; then
    echo "Error: ablation_runner not found at $ABLATION_RUNNER"
    echo "Please build it first:"
    echo "  cd $(dirname "$ABLATION_RUNNER")"
    echo "  cmake --build . --target ablation_runner"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "Error: config file not found at $CONFIG"
    exit 1
fi

if [ ! -d "$MATRIX_DIR" ]; then
    echo "Error: matrix directory not found at $MATRIX_DIR"
    exit 1
fi

mkdir -p "$OUT_DIR/suitesparse"

# --- Parallelism helper ---
function run_with_limit {
    while [ "$(jobs | wc -l)" -ge "$MAX_JOBS" ]; do
        wait -n
    done
    "$@" &
}

# --- Run experiments ---
total=${#matrices[@]}
count=0

echo "=========================================="
echo "SuiteSparse matrix experiments"
echo "  matrices:   ${total}"
echo "  MAX_JOBS:   $MAX_JOBS"
echo "  MATRIX_DIR: $MATRIX_DIR"
echo "  OUT_DIR:    $OUT_DIR/suitesparse"
echo "=========================================="

for matrix in "${matrices[@]}"; do
    count=$((count + 1))
    echo "[$count/$total] Running matrix: $matrix"

    run_with_limit "$ABLATION_RUNNER" \
        --config "$CONFIG" \
        --out_dir "$OUT_DIR/suitesparse" \
        --suitesparse \
        --matrix "$matrix" \
        --matrix_dir "$MATRIX_DIR"
done

wait  # Wait for all background processes to finish

echo ""
echo "=========================================="
echo "All SuiteSparse experiments completed."
echo "Results in: $OUT_DIR/suitesparse"
echo "=========================================="
