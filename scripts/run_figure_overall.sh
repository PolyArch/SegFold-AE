#!/bin/bash
set -e

# ==========================================================================
# run_figure_overall.sh — Reproduce overall performance figure
#
# End-to-end: build → download matrices → simulate → collect → plot
# Produces: fig8_overall_speedup.pdf (SegFold vs Spada vs Flexagon, 11 matrices)
#
# Usage:
#   ./scripts/run_figure_overall.sh [--jobs N] [--skip-build] [--output-dir DIR]
# ==========================================================================

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# ── Defaults ──────────────────────────────────────────────────────────────
SKIP_BUILD=0
MAX_JOBS=""
OUT_DIR=""
CONFIG="configs/segfold.yaml"

# ── Argument parsing ─────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs)       MAX_JOBS="$2"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --output-dir) OUT_DIR="$2"; shift 2 ;;
        --config)     CONFIG="$2"; shift 2 ;;
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

CONFIG_PATH="$PROJECT_ROOT/$CONFIG"

echo ""
echo "=========================================="
echo " Overall Performance Figure"
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

# ── Step 2: Download matrices ───────────────────────────────────────────
echo "[$(date +%T)] Downloading matrices (if needed)..."
python3 "$PROJECT_ROOT/scripts/download_matrices.py"

# ── Step 3: Run simulation ──────────────────────────────────────────────
echo "[$(date +%T)] Running overall performance experiments..."
python3 "$PROJECT_ROOT/scripts/run_overall.py" "$OUT_DIR" \
    --jobs "$MAX_JOBS" --config "$CONFIG_PATH"

# ── Step 4: Collect results ─────────────────────────────────────────────
echo "[$(date +%T)] Collecting results..."
python3 "$PROJECT_ROOT/scripts/collect_results.py" "$OUT_DIR"

# ── Step 5: Generate plot ───────────────────────────────────────────────
echo "[$(date +%T)] Generating plot..."
python3 "$PROJECT_ROOT/scripts/plot_overall.py" "$OUT_DIR"

echo ""
echo "=========================================="
echo " Done! Output: $OUT_DIR/plots/fig8_overall_speedup.pdf"
echo "=========================================="
