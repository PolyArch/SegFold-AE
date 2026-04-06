#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# ─── Step 1: Check system dependencies ───────────────────────────────────────

echo "=== Step 1: Checking system dependencies ==="

# cmake >= 3.15
if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake is not installed. Please install cmake >= 3.15." >&2
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | sed 's/[^0-9.]//g')
CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)
if [ "$CMAKE_MAJOR" -lt 3 ] || { [ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 15 ]; }; then
    echo "ERROR: cmake >= 3.15 is required (found $CMAKE_VERSION)." >&2
    exit 1
fi
echo "  cmake $CMAKE_VERSION ... OK"

# g++ with C++20 support
if ! command -v g++ &>/dev/null; then
    echo "ERROR: g++ is not installed. Please install a g++ version that supports C++20." >&2
    exit 1
fi

GXX_VERSION=$(g++ -dumpversion)
GXX_MAJOR=$(echo "$GXX_VERSION" | cut -d. -f1)
if [ "$GXX_MAJOR" -lt 10 ]; then
    echo "ERROR: g++ with C++20 support is required (need >= 10, found $GXX_VERSION)." >&2
    exit 1
fi
echo "  g++ $GXX_VERSION (C++20 supported) ... OK"

# ─── Step 2: Check Python dependencies ───────────────────────────────────────

echo "=== Step 2: Checking Python dependencies ==="

if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 is not installed." >&2
    exit 1
fi
echo "  python3 $(python3 --version 2>&1 | awk '{print $2}') ... OK"

# Auto-install from requirements.txt if present
REQ_FILE="$PROJECT_ROOT/requirements.txt"
if [ -f "$REQ_FILE" ]; then
    echo "  Installing Python dependencies from requirements.txt ..."
    python3 -m pip install --quiet -r "$REQ_FILE"
fi

PYTHON_PACKAGES=(numpy scipy matplotlib pyyaml pandas)
for pkg in "${PYTHON_PACKAGES[@]}"; do
    # pyyaml is imported as 'yaml'
    import_name="$pkg"
    if [ "$pkg" = "pyyaml" ]; then
        import_name="yaml"
    fi
    if ! python3 -c "import $import_name" &>/dev/null; then
        echo "ERROR: Python package '$pkg' is not installed." >&2
        echo "       Run: pip install -r requirements.txt" >&2
        exit 1
    fi
    echo "  $pkg ... OK"
done

# ─── Step 3: Build the C++ simulator ─────────────────────────────────────────

echo "=== Step 3: Building the C++ simulator ==="

cd csegfold
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=ON
make -j"$(nproc)"
cd "$PROJECT_ROOT"

echo "  Build complete."

# ─── Step 4: Verify build ────────────────────────────────────────────────────

echo "=== Step 4: Verifying build ==="

if [ ! -x csegfold/build/csegfold ]; then
    echo "ERROR: csegfold executable not found after build." >&2
    exit 1
fi
echo "  csegfold executable found ... OK"


# ─── Step 5: Run smoke test ──────────────────────────────────────────────────

echo "=== Step 5: Running smoke test ==="

./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --M 48 --K 48 --N 48 \
    --density-a 0.5 --density-b 0.5 --seed 1

echo "  Smoke test passed."

# ─── Step 6: Done ────────────────────────────────────────────────────────────

echo "=== Setup complete ==="
echo "SegFold artifact evaluation environment is ready."
