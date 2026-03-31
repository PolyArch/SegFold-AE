#!/bin/bash
set -e

# Synthetic Matrix Experiments: sweep over density and size combinations.

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# --- Parameters (override via env vars or positional arg) ---
ABLATION_RUNNER="${ABLATION_RUNNER:-$PROJECT_ROOT/csegfold/build/ablation_runner}"
CONFIG="${CONFIG:-$PROJECT_ROOT/configs/segfold.yaml}"
MAX_JOBS="${MAX_JOBS:-4}"

OUT_DIR="${1:?Usage: $0 <OUT_DIR>}"

# --- Experiment sweep ---
densitiesA=(0.05 0.1 0.2 0.4)
densitiesB=(0.05 0.1 0.2 0.4)
sizes=(256 512)
NREPS=6

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

mkdir -p "$OUT_DIR"

# --- Parallelism helper ---
function run_with_limit {
    while [ "$(jobs | wc -l)" -ge "$MAX_JOBS" ]; do
        wait -n
    done
    "$@" &
}

# --- Run experiments ---
total=$(( ${#densitiesA[@]} * ${#densitiesB[@]} * ${#sizes[@]} * NREPS ))
count=0

echo "=========================================="
echo "Synthetic matrix experiment sweep"
echo "  densitiesA: ${densitiesA[*]}"
echo "  densitiesB: ${densitiesB[*]}"
echo "  sizes:      ${sizes[*]}"
echo "  NREPS:      $NREPS"
echo "  total runs: $total"
echo "  MAX_JOBS:   $MAX_JOBS"
echo "  OUT_DIR:    $OUT_DIR"
echo "=========================================="

for densityA in "${densitiesA[@]}"; do
    for densityB in "${densitiesB[@]}"; do
        for size in "${sizes[@]}"; do
            for i in $(seq 1 $NREPS); do
                count=$((count + 1))
                echo "[$count/$total] densityA=$densityA densityB=$densityB size=$size rep=$i"

                run_with_limit "$ABLATION_RUNNER" \
                    --config "$CONFIG" \
                    --out_dir "$OUT_DIR" \
                    --densityA "$densityA" \
                    --densityB "$densityB" \
                    --size "$size" \
                    --random_state "$i"
            done
        done
    done
done

wait  # Wait for all background processes to finish

echo ""
echo "=========================================="
echo "All synthetic experiments completed."
echo "Results in: $OUT_DIR"
echo "=========================================="
