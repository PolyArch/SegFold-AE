#!/bin/bash
set -e

# ==========================================================================
# run_all.sh — Master script for SegFold artifact evaluation
#
# Runs all experiments (synthetic, SuiteSparse, ablation), collects results,
# generates plots, and optionally compares against expected results.
#
# Usage:
#   ./scripts/run_all.sh [--jobs N] [--skip-build] [--config CONFIG]
# ==========================================================================

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# ── Defaults ──────────────────────────────────────────────────────────────

SKIP_BUILD=0
CONFIG="configs/segfold.yaml"
MAX_JOBS=""   # empty means auto-detect

# ── Argument parsing ─────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs)
            MAX_JOBS="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --config)
            CONFIG="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--jobs N] [--skip-build] [--config CONFIG]"
            echo ""
            echo "Options:"
            echo "  --jobs N        Max parallel jobs (default: auto-detect from RAM)"
            echo "  --skip-build    Skip running scripts/setup.sh"
            echo "  --config FILE   Config file (default: configs/segfold.yaml)"
            exit 0
            ;;
        *)
            echo "Error: unknown argument '$1'"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

# ── Auto-detect MAX_JOBS from RAM ────────────────────────────────────────

if [ -z "$MAX_JOBS" ]; then
    if [ -f /proc/meminfo ]; then
        mem_total_kb=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
        available_gb=$(( mem_total_kb / 1024 / 1024 ))
        MAX_JOBS=$(( available_gb / 4 ))
        # Clamp to [1, 16]
        if [ "$MAX_JOBS" -lt 1 ]; then
            MAX_JOBS=1
        fi
        if [ "$MAX_JOBS" -gt 16 ]; then
            MAX_JOBS=16
        fi
        echo "[run_all] Auto-detected ${available_gb} GB RAM -> MAX_JOBS=${MAX_JOBS}"
    else
        MAX_JOBS=4
        echo "[run_all] Cannot read /proc/meminfo; defaulting to MAX_JOBS=${MAX_JOBS}"
    fi
fi

export MAX_JOBS

# ── Helper: run with concurrency limit ───────────────────────────────────

function run_with_limit {
    while [ "$(jobs -rp | wc -l)" -ge "$MAX_JOBS" ]; do
        wait -n
    done
    "$@" &
}

# ── Helper: timestamp ────────────────────────────────────────────────────

function ts {
    date +"%Y-%m-%d %H:%M:%S"
}

# ── Resolve paths ────────────────────────────────────────────────────────

CONFIG_PATH="$PROJECT_ROOT/$CONFIG"
if [ ! -f "$CONFIG_PATH" ]; then
    echo "[run_all] ERROR: config file not found: $CONFIG_PATH"
    exit 1
fi

# ── Output directory ─────────────────────────────────────────────────────

date_str=$(date +"%Y%m%d_%H%M%S")
OUT_DIR="$PROJECT_ROOT/output/ae_${date_str}"
mkdir -p "$OUT_DIR"
export OUT_DIR

echo ""
echo "=========================================="
echo " SegFold Artifact Evaluation"
echo "=========================================="
echo " Start time  : $(ts)"
echo " Project root: $PROJECT_ROOT"
echo " Config      : $CONFIG_PATH"
echo " Output dir  : $OUT_DIR"
echo " Max jobs    : $MAX_JOBS"
echo " Skip build  : $SKIP_BUILD"
echo "=========================================="
echo ""

# ── Step 0: Setup / Build ────────────────────────────────────────────────

if [ "$SKIP_BUILD" -eq 0 ]; then
    echo "[$(ts)] Step 0: Running setup..."
    if [ -f "$PROJECT_ROOT/scripts/setup.sh" ]; then
        bash "$PROJECT_ROOT/scripts/setup.sh"
    else
        echo "[run_all] WARNING: scripts/setup.sh not found, skipping setup."
    fi
    echo "[$(ts)] Step 0: Setup complete."
    echo ""
else
    echo "[$(ts)] Step 0: Skipped (--skip-build)."
    echo ""
fi

# ── Download matrices if needed ──────────────────────────────────────────

echo "[$(ts)] Step 0b: Downloading SuiteSparse matrices (if needed)..."
python3 "$PROJECT_ROOT/scripts/download_matrices.py"
echo "[$(ts)] Step 0b: Matrix download complete."
echo ""

# ── Experiment plan ──────────────────────────────────────────────────────

echo "=========================================="
echo " Experiment Plan"
echo "=========================================="
echo " 1. Overall performance       (11 matrices)          ~15-30 min"
echo " 2. Non-square performance    (6 matrices)           ~10-20 min"
echo " 3. Speedup breakdown         (5 configs x 12 mat)   ~30-60 min"
echo " 4. Mapping ablation          (3 configs x 16 mat x2) ~30-60 min"
echo " 5. Collect results into CSV"
echo " 6. Generate plots"
echo ""
echo " Estimated total runtime: 2-3 hours (depends on hardware)"
echo "=========================================="
echo ""

# ── Step 1: Overall performance ──────────────────────────────────────────

echo "[$(ts)] Step 1: Running overall performance experiments..."
python3 "$PROJECT_ROOT/scripts/run_overall.py" "$OUT_DIR" \
    --jobs "$MAX_JOBS" --config "$CONFIG_PATH"
echo "[$(ts)] Step 1: Overall performance complete."
echo ""

# ── Step 2: Non-square performance ───────────────────────────────────────

echo "[$(ts)] Step 2: Running non-square performance experiments..."
python3 "$PROJECT_ROOT/scripts/run_nonsquare.py" "$OUT_DIR" \
    --jobs "$MAX_JOBS" --config "$CONFIG_PATH"
echo "[$(ts)] Step 2: Non-square performance complete."
echo ""

# ── Step 3: Speedup breakdown ────────────────────────────────────────────

echo "[$(ts)] Step 3: Running speedup breakdown experiments..."
python3 "$PROJECT_ROOT/scripts/run_breakdown.py" "$OUT_DIR" \
    --jobs "$MAX_JOBS"
echo "[$(ts)] Step 3: Speedup breakdown complete."
echo ""

# ── Step 4: Mapping ablation ────────────────────────────────────────────

echo "[$(ts)] Step 4: Running mapping ablation (with memory hierarchy)..."
python3 "$PROJECT_ROOT/scripts/run_ablation.py" "$OUT_DIR" \
    --ablation mapping-paper --jobs "$MAX_JOBS"
echo "[$(ts)] Step 4a: Mapping ablation (with mem) complete."

echo "[$(ts)] Step 4b: Running mapping ablation (without memory hierarchy)..."
python3 "$PROJECT_ROOT/scripts/run_ablation.py" "$OUT_DIR" \
    --ablation mapping-paper-nomem --jobs "$MAX_JOBS"
echo "[$(ts)] Step 4b: Mapping ablation (without mem) complete."
echo ""

# ── Step 5: Collect results ──────────────────────────────────────────────

echo "[$(ts)] Step 5: Collecting results..."
if [ -f "$PROJECT_ROOT/scripts/collect_results.py" ]; then
    python3 "$PROJECT_ROOT/scripts/collect_results.py" "$OUT_DIR"
    echo "[$(ts)] Step 5: Results collected."
else
    echo "[run_all] WARNING: scripts/collect_results.py not found, skipping."
fi
echo ""

# ── Step 6: Generate plots ───────────────────────────────────────────────

echo "[$(ts)] Step 6: Generating plots..."
for plot_script in plot_overall.py plot_nonsquare.py plot_breakdown.py; do
    if [ -f "$PROJECT_ROOT/scripts/$plot_script" ]; then
        echo "  Running $plot_script ..."
        python3 "$PROJECT_ROOT/scripts/$plot_script" "$OUT_DIR"
    else
        echo "  WARNING: scripts/$plot_script not found, skipping."
    fi
done
# Mapping ablation plot (reads CSVs from output root)
if [ -f "$PROJECT_ROOT/scripts/plot_mapping_ablation.py" ]; then
    echo "  Running plot_mapping_ablation.py ..."
    python3 "$PROJECT_ROOT/scripts/plot_mapping_ablation.py" \
        --mem-csv "$OUT_DIR/mapping_ablation_suitesparse.csv" \
        --nomem-csv "$OUT_DIR/mapping_ablation_suitesparse_nomem.csv" \
        --output "$OUT_DIR/plots/mapping_ablation_suitesparse.pdf"
fi
echo "[$(ts)] Step 6: Plots generated."
echo ""

# ── Step 7: Summary and comparison ───────────────────────────────────────

echo "=========================================="
echo " Summary"
echo "=========================================="
echo " Completed at : $(ts)"
echo " Output dir   : $OUT_DIR"
echo ""

# Count result files
num_stats=$(find "$OUT_DIR" -name "*_stats.json" 2>/dev/null | wc -l)
num_csv=$(find "$OUT_DIR" -name "*.csv" 2>/dev/null | wc -l)
num_plots=$(find "$OUT_DIR" -name "*.png" -o -name "*.pdf" 2>/dev/null | wc -l)
echo " Result files  : $num_stats stats JSON files"
echo " CSV files     : $num_csv"
echo " Plot files    : $num_plots"
echo ""

# Compare against expected results if present
EXPECTED_DIR="$PROJECT_ROOT/expected_results"
if [ -d "$EXPECTED_DIR" ] && [ "$(ls -A "$EXPECTED_DIR" 2>/dev/null)" ]; then
    echo "=========================================="
    echo " Comparing against expected results"
    echo "=========================================="
    echo " Expected results directory: $EXPECTED_DIR"
    echo ""

    mismatch=0
    for expected_file in "$EXPECTED_DIR"/*.csv; do
        [ -f "$expected_file" ] || continue
        base=$(basename "$expected_file")
        actual_file="$OUT_DIR/$base"
        if [ -f "$actual_file" ]; then
            if diff -q "$expected_file" "$actual_file" > /dev/null 2>&1; then
                echo "  PASS: $base"
            else
                echo "  DIFF: $base (differences found)"
                mismatch=1
            fi
        else
            echo "  MISS: $base (not produced)"
            mismatch=1
        fi
    done

    echo ""
    if [ "$mismatch" -eq 0 ]; then
        echo " All results match expected outputs."
    else
        echo " Some results differ from expected outputs."
        echo " This may be acceptable if differences are within tolerance."
        echo " Check individual files for details."
    fi
    echo ""
else
    echo " No expected_results/ directory found; skipping comparison."
    echo ""
fi

echo "=========================================="
echo " Artifact evaluation complete!"
echo "=========================================="
