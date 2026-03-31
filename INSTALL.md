# Installation Guide

## System Requirements

- **OS**: Ubuntu 22.04+ (tested), other Linux distributions should also work
- **Compiler**: GCC 11+ (C++20 support required)
- **Build system**: CMake 3.15+
- **Python**: 3.10+
- **RAM**: 8 GB minimum, 16 GB recommended (see RAM requirements below)
- **Disk**: ~500 MB for source + benchmarks, ~2 GB for full experiment outputs

## Dependencies

### System packages (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake g++ git python3 python3-pip
```

### Python packages

```bash
pip3 install numpy scipy matplotlib pyyaml pandas
```

## Building from Source

### Quick build

```bash
./scripts/setup.sh
```

This script checks all dependencies, builds the simulator, and runs a smoke test.

### Manual build

```bash
cd csegfold
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=OFF
make -j$(nproc)
```

The build produces two executables in `csegfold/build/`:
- `csegfold` — main simulator
- `ablation_runner` — experiment runner for sweeps and ablation studies

### Build with Ramulator2 (optional)

To enable DRAM simulation with Ramulator2:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=ON
```

Note: Ramulator2 requires an active internet connection during build (fetched via CMake FetchContent).

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

Each simulation process allocates ~28 x S^2 bytes of dense storage, where S is the matrix dimension. Multiple processes run in parallel controlled by `MAX_JOBS`.

| Matrix Size | Per Process | MAX_JOBS=4 | MAX_JOBS=8 |
|-------------|-------------|------------|------------|
| 256x256     | ~80 MB      | ~320 MB    | ~640 MB    |
| 512x512     | ~524 MB     | ~2.1 GB   | ~4.2 GB    |
| 1024x1024   | ~3.5 GB     | ~14 GB    | ~28 GB     |
| 5000x5000   | ~3.3 GB     | ~13 GB    | ~26 GB     |

**SuiteSparse matrices**: Memory depends on the matrix dimensions (not sparsity), because the simulator converts to dense internally. Matrices up to ~10,000 rows are supported. The `ckt11752_dc_1` (49,702 x 49,702) and `bcsstk17` (10,974 x 10,974) matrices are excluded from the default sweep.

**Recommendation**: Set `MAX_JOBS` based on your available RAM. The scripts auto-detect RAM and set a safe default. Override with `--jobs N`:

```bash
./scripts/run_all.sh --jobs 2    # For 8 GB machines
./scripts/run_all.sh --jobs 4    # For 16 GB machines (default)
./scripts/run_all.sh --jobs 8    # For 32+ GB machines
```

## Verification

After building, verify the installation:

```bash
# Quick smoke test (< 5 seconds)
./csegfold/build/ablation_runner \
    --config configs/baseline.yaml \
    --out_dir /tmp/test \
    --size 48 --densityA 0.5 --densityB 0.5 --random_state 1

# SuiteSparse smoke test
./csegfold/build/ablation_runner \
    --config configs/baseline.yaml \
    --out_dir /tmp/test \
    --suitesparse --matrix bcsstk01 \
    --matrix_dir benchmarks/data/suitesparse
```
