#!/bin/bash
set -e

# Ablation Study: Compare SegFold configurations
# Adapted from exp/run_ablation_study.sh for artifact evaluation.

# ---------------------------------------------------------------------------
# Parameters
# ---------------------------------------------------------------------------
OUT_DIR="${1:?Usage: $0 <OUT_DIR> [MAX_JOBS]}"
MAX_JOBS="${MAX_JOBS:-${2:-4}}"

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

ABLATION_RUNNER="${ABLATION_RUNNER:-$PROJECT_ROOT/csegfold/build/ablation_runner}"

# ---------------------------------------------------------------------------
# Validate
# ---------------------------------------------------------------------------
if [ ! -f "$ABLATION_RUNNER" ]; then
    echo "Error: ablation_runner not found at $ABLATION_RUNNER"
    echo "Please build it first:"
    echo "  cd $PROJECT_ROOT/csegfold/build"
    echo "  cmake --build . --target ablation_runner"
    exit 1
fi

# ---------------------------------------------------------------------------
# Configurations to compare
# ---------------------------------------------------------------------------
config_names=(
    "segfold"
    "ablation-crossbar"
    "ablation-folding"
    "ablation-map-to-csr"
    "ablation-map-to-ideal"
    "ablation-map-to-zero"
    "ablation-multi-b-row-per-row"
    "ablation-no-k-reordering"
    "ablation-no-partial-b"
)

config_files=()
for name in "${config_names[@]}"; do
    cfg="$PROJECT_ROOT/configs/${name}.yaml"
    if [ ! -f "$cfg" ]; then
        echo "Warning: config file not found: $cfg  (skipping)"
    fi
    config_files+=("$cfg")
done

# ---------------------------------------------------------------------------
# Sweep parameters
# ---------------------------------------------------------------------------
densitiesA=(0.05 0.1 0.2 0.4)
densitiesB=(0.05 0.1 0.2 0.4)
sizes=(256 512)
NREPS=3

# ---------------------------------------------------------------------------
# Parallelism helper
# ---------------------------------------------------------------------------
function run_with_limit {
    while [ "$(jobs -rp | wc -l)" -ge "$MAX_JOBS" ]; do
        wait -n
    done
    "$@" &
}

# ---------------------------------------------------------------------------
# Run sweep
# ---------------------------------------------------------------------------
echo "=========================================="
echo "Starting Ablation Study"
echo "=========================================="
echo "Runner:       $ABLATION_RUNNER"
echo "Output dir:   $OUT_DIR"
echo "MAX_JOBS:     $MAX_JOBS"
echo "Configs:      ${#config_names[@]}"
echo "densitiesA:   ${densitiesA[*]}"
echo "densitiesB:   ${densitiesB[*]}"
echo "sizes:        ${sizes[*]}"
echo "NREPS:        $NREPS"
echo "=========================================="
echo ""

total=0
launched=0

for config_idx in "${!config_names[@]}"; do
    config_name="${config_names[$config_idx]}"
    config_file="${config_files[$config_idx]}"

    if [ ! -f "$config_file" ]; then
        continue
    fi

    echo "[ablation] ===================="
    echo "[ablation] Configuration: $config_name"
    echo "[ablation] Config file:   $config_file"
    echo "[ablation] ===================="

    subdir="$OUT_DIR/ablation/$config_name"
    mkdir -p "$subdir"

    for dA in "${densitiesA[@]}"; do
        for dB in "${densitiesB[@]}"; do
            for sz in "${sizes[@]}"; do
                for rep in $(seq 1 "$NREPS"); do
                    total=$((total + 1))
                    launched=$((launched + 1))
                    echo "[ablation] ($launched) $config_name: densityA=$dA densityB=$dB size=$sz rep=$rep"

                    run_with_limit "$ABLATION_RUNNER" \
                        --config "$config_file" \
                        --out_dir "$subdir" \
                        --densityA "$dA" \
                        --densityB "$dB" \
                        --size "$sz" \
                        --random_state "$rep"
                done
            done
        done
    done
done

wait  # Wait for all background jobs to finish

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "=========================================="
echo "Ablation Study Complete"
echo "=========================================="
echo "Total runs launched: $total"
echo ""

for config_name in "${config_names[@]}"; do
    subdir="$OUT_DIR/ablation/$config_name"
    if [ -d "$subdir" ]; then
        num_results=$(find "$subdir" -name "*_stats.json" 2>/dev/null | wc -l)
        echo "  $config_name: $num_results result files"
    else
        echo "  $config_name: (no output directory)"
    fi
done

echo ""
echo "Results stored in: $OUT_DIR/ablation/"
echo "=========================================="
