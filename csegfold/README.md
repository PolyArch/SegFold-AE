# csegfold

Cycle-accurate C++ simulator for the SegFold sparse-sparse matrix multiplication (SpMSpM) accelerator. Includes a Ramulator2-based HBM2 DRAM backend, configurable PE array, and full memory hierarchy modeling.

## Building

```bash
# Recommended (from project root)
./scripts/setup.sh

# Manual
cd csegfold
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=ON
make -j$(nproc)
```

CMake automatically fetches these dependencies via FetchContent:
- [yaml-cpp 0.8.0](https://github.com/jbeder/yaml-cpp) — YAML configuration loading
- [indicators v2.3](https://github.com/p-ranav/indicators) — progress bars
- [Ramulator2](https://github.com/CMU-SAFARI/ramulator2) — HBM2 DRAM simulation (when `-DENABLE_RAMULATOR2=ON`)

### Build Targets

| Target | Description |
|--------|-------------|
| `csegfold` | Main simulator executable (used by all experiment scripts, including ablations) |
| `csegfold_mem` | Static library exposing the memory/DRAM backend via C FFI (for external simulators) |

## Running

```bash
./csegfold [options]
```

### Command-Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--config`, `-c` | `test.yaml` | YAML configuration file |
| `--mtx-file <path>` | — | Load a SuiteSparse `.mtx` file (CSR-only, memory-efficient) |
| `--mtx-file-b <path>` | — | Separate B matrix `.mtx` file (default: B = A^T) |
| `--matrix-file <path>` | — | Load a pre-generated binary matrix file |
| `--M <int>` | 8 | Number of rows in matrix A (synthetic mode) |
| `--K <int>` | 8 | Shared dimension between A and B (synthetic mode) |
| `--N <int>` | 8 | Number of columns in matrix B (synthetic mode) |
| `--density-a <float>` | 0.5 | Target density for A (synthetic mode) |
| `--density-b <float>` | 0.5 | Target density for B (synthetic mode) |
| `--c-density <float>` | — | If positive, use `gen_rand_matrix` for C density control |
| `--seed <int>` | 2 | Random seed for matrix generation |
| `--tmp-dir <path>` | `csegfold/tmp/` | Output directory for stats/config JSON files |

### Examples

```bash
# SuiteSparse matrix (CSR-only, no dense intermediary)
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --mtx-file benchmarks/data/suitesparse/ca-GrQc/ca-GrQc.mtx

# Synthetic matrix
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --M 48 --K 48 --N 48 \
    --density-a 0.5 --density-b 0.5 --seed 1
```

## Testing

Run all 20 registered tests via CTest:

```bash
cd csegfold/build
ctest --output-on-failure
```

Or run individual test executables:

```bash
./test_generator          # Matrix creation, CSR format
./test_pe_mapper          # PE state and mapper modules
./test_switch             # Switch state transitions
./test_spad              # Scratchpad operations
./test_matrixLoader      # Matrix tiling and indexing
./test_e2e_cpp           # End-to-end simulation (4x4 to 128x128)
```

See [tests/README.md](tests/README.md) for the full test listing.

## Source Structure

```
csegfold/
├── src/
│   ├── main.cpp                     # CLI entry point
│   ├── modules/                     # Hardware module state & logic
│   │   ├── module.cpp               # Base module, Config, Stats
│   │   ├── pe.cpp                   # Processing Element
│   │   ├── switch.cpp               # Crossbar switch
│   │   ├── spad.cpp                 # Scratchpad memory
│   │   ├── mapper.cpp               # Virtual-to-physical PE mapping
│   │   ├── lookuptable.cpp          # IPM lookup table
│   │   ├── matrixLoader.cpp         # Matrix preprocessing & tiling
│   │   └── memoryController.cpp     # B row management
│   ├── simulator/                   # Cycle-accurate simulation
│   │   ├── simulator.cpp            # Base simulator
│   │   ├── segfoldSimulator.cpp     # SegFold simulation loop
│   │   ├── pe.cpp                   # PE simulation functions
│   │   ├── switch.cpp               # Switch simulation functions
│   │   ├── memoryController.cpp     # Memory controller simulation
│   │   └── stats.cpp                # Statistics collection
│   ├── memory/                      # Memory hierarchy
│   │   ├── MemoryBackend.cpp        # Abstract backend factory
│   │   ├── IdealBackend.cpp         # Fixed-latency backend
│   │   ├── CacheModel.cpp           # L1/L2 cache hierarchy
│   │   ├── RamulatorBackend.cpp     # Ramulator2 DRAM integration
│   │   └── csegfold_ffi.cpp         # C FFI for external simulators
│   └── matrix/
│       └── generator.cpp            # Matrix generation & operations
├── include/csegfold/                # Headers (mirrors src/ layout)
├── tests/                           # 21 test files (20 registered in CTest)
└── CMakeLists.txt
```

## Requirements

- C++20 compatible compiler (GCC 10+)
- CMake 3.15+
- Internet connection for first build (FetchContent downloads dependencies)

