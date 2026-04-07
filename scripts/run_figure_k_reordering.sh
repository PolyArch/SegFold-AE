#!/bin/bash
set -e

# ==========================================================================
# run_figure_k_reordering.sh — Reproduce k-reordering ablation results
#
# End-to-end: build → simulate (synthetic) → collect → generate summary
# Produces: ablation_k_reordering.txt (3 configs, synthetic matrices)
#
# Usage:
#   ./scripts/run_figure_k_reordering.sh [--jobs N] [--skip-build] [--output-dir DIR]
# ==========================================================================

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# ── Defaults ──────────────────────────────────────────────────────────────
SKIP_BUILD=0
MAX_JOBS=""
OUT_DIR=""

# ── Argument parsing ─────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs)       MAX_JOBS="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --output-dir) OUT_DIR="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--jobs N] [--skip-build] [--output-dir DIR]"
            exit 0 ;;
        *) echo "Error: unknown argument '$1'"; exit 1 ;;
    esac
done

# ── Auto-detect MAX_JOBS from RAM ────────────────────────────────────────
if [ -z "$MAX_JOBS" ]; then
    if [ -f /proc/meminfo ]; then
        mem_total_kb=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
        available_gb=$(( mem_total_kb / 1024 / 1024 ))
        MAX_JOBS=$(( available_gb / 4 ))
        [ "$MAX_JOBS" -lt 1 ] && MAX_JOBS=1
        [ "$MAX_JOBS" -gt 16 ] && MAX_JOBS=16
    else
        MAX_JOBS=4
    fi
fi

# ── Output directory ─────────────────────────────────────────────────────
if [ -z "$OUT_DIR" ]; then
    date_str=$(date +"%Y%m%d_%H%M%S")
    OUT_DIR="$PROJECT_ROOT/output/ae_${date_str}"
fi
mkdir -p "$OUT_DIR"

echo ""
echo "=========================================="
echo " K-Reordering Ablation"
echo "=========================================="
echo " Output : $OUT_DIR"
echo " Jobs   : $MAX_JOBS"
echo "=========================================="
echo ""

# ── Step 1: Build (if needed) ────────────────────────────────────────────
if [ "$SKIP_BUILD" -eq 0 ]; then
    if [ ! -f "$PROJECT_ROOT/csegfold/build/csegfold" ]; then
        echo "[$(date +%T)] Building simulator..."
        bash "$PROJECT_ROOT/scripts/setup.sh"
    else
        echo "[$(date +%T)] Simulator binary found, skipping build."
    fi
fi

# ── Step 2: Run simulation (synthetic matrices, no download needed) ─────
echo "[$(date +%T)] Running k-reordering ablation experiments..."
python3 "$PROJECT_ROOT/scripts/run_ablation.py" "$OUT_DIR" \
    --ablation k-reordering --jobs "$MAX_JOBS"

# ── Step 3: Collect results ─────────────────────────────────────────────
echo "[$(date +%T)] Collecting results..."
python3 "$PROJECT_ROOT/scripts/collect_results.py" "$OUT_DIR"

# ── Step 4: Generate summary ────────────────────────────────────────────
echo "[$(date +%T)] Generating summary..."
python3 "$PROJECT_ROOT/scripts/plot_ablation.py" "$OUT_DIR"

echo ""
echo "=========================================="
echo " Done! Output: $OUT_DIR/plots/ablation_k_reordering.txt"
echo "=========================================="
