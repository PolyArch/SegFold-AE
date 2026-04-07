# Installation Guide

## System Requirements

- **OS**: Ubuntu 22.04+ (tested), other Linux distributions should also work
- **Compiler**: GCC 10+ (C++20 support required)
- **Build system**: CMake 3.15+
- **Python**: 3.8+
- **RAM**: 8 GB minimum, 16 GB recommended (see RAM requirements below)
- **Disk**: ~500 MB for source + benchmarks, ~2 GB for full experiment outputs
- **Internet**: Required during build (Ramulator2 fetched via CMake) and for downloading SuiteSparse matrices

## Dependencies

### System packages (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake g++ git python3 python3-pip curl
```

### Python packages

```bash
pip install -r requirements.txt
```

This installs: numpy, scipy, matplotlib, pandas, pyyaml.

> **Note:** Only the plotting and result collection scripts need these packages. The experiment runner scripts and the C++ simulator have no Python package dependencies.

## Building from Source

### Quick build (recommended)

```bash
./scripts/setup.sh
```

This script:
1. Checks all system dependencies (CMake, `g++`, Python)
2. Installs Python packages from `requirements.txt`
3. Builds the C++ simulator with Ramulator2 HBM2 DRAM backend
4. Runs a smoke test to verify the build

### Manual build

```bash
cd csegfold
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=ON
make -j$(nproc)
```

> **Note:** Ramulator2 is required for the paper experiments (HBM2 DRAM modeling). CMake will automatically fetch it via FetchContent during the first build, which requires an internet connection.

The build produces the main executable `csegfold/build/csegfold`, which supports:
- `--mtx-file <path>` — load a SuiteSparse .mtx file (CSR-only, memory-efficient)
- `--matrix-file <path>` — load a pre-generated binary matrix file
- Synthetic matrix generation via `--M`, `--K`, `--N`, `--density-a`, `--density-b`

## Docker

### Using Docker Compose (recommended)

```bash
docker compose build
docker compose run artifact ./scripts/run_all.sh
```

### Using Docker directly

```bash
docker build -t segfold-artifact .
docker run --memory=16g -v $(pwd)/output:/workspace/segfold/output segfold-artifact ./scripts/run_all.sh
```

## RAM Requirements

The simulator uses CSR (Compressed Sparse Row) format via `--mtx-file`, so memory usage scales with the number of nonzeros (nnz), not the full matrix dimensions. This makes even large matrices like ca-CondMat (23,133 x 23,133) feasible on modest hardware.

Peak memory usage varies significantly across experiments:

| Experiment | Config | Peak RAM / proc |
|------------|--------|-----------------|
| Overall performance | `segfold.yaml` | ~5 GB |
| Non-square performance | `segfold.yaml` | ~5 GB |
| Speedup breakdown | `breakdown-base.yaml` | ~50 GB |
| Ablation mapping | `ablation-map-*.yaml` | ~40 GB |
| Synthetic ablations | various | ~4 GB |

The `run_all.sh` script auto-detects available RAM and sets parallelism accordingly — using higher concurrency for lightweight experiments and lower concurrency for memory-intensive ones (breakdown and mapping).

**Recommendation**: Set `--jobs` based on your available RAM:

```bash
./scripts/run_all.sh --jobs 1    # For 64 GB machines
./scripts/run_all.sh --jobs 4    # For 256 GB machines
./scripts/run_all.sh --jobs 8    # For 512+ GB machines
```

## Downloading Benchmark Matrices

```bash
python3 scripts/download_matrices.py
```

Downloads 21 SuiteSparse matrices (~50 MB total) from https://sparse.tamu.edu/. Already-downloaded matrices are skipped.

## Verification

After building, verify the installation:

```bash
# Quick smoke test with synthetic matrix (< 5 seconds)
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --M 48 --K 48 --N 48 \
    --density-a 0.5 --density-b 0.5 --seed 1

# SuiteSparse smoke test (requires download_matrices.py first)
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --mtx-file benchmarks/data/suitesparse/bcspwr06/bcspwr06.mtx
```
