# csegfold

C++ implementation of SegFold sparse matrix multiplication simulator.

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Running

```bash
./csegfold [options]
```

Options:
- `--M <int>`: Number of rows in matrix A/C (default: 128)
- `--K <int>`: Shared dimension between A and B (default: 128)
- `--N <int>`: Number of columns in matrix B/C (default: 128)
- `--density-a <float>`: Target density for A (default: 0.05)
- `--density-b <float>`: Target density for B (default: 0.05)
- `--prow <int>`: Tile height (rows) (default: 16)
- `--pcol <int>`: Tile width (columns) (default: 16)
- `--seed <int>`: Random seed (default: 2)

## Testing

Run unit tests to verify module correctness:

```bash
cd build
ctest
```

Or run individual test suites:
```bash
./test_generator    # Matrix operations (60 tests)
./test_pe_mapper    # PE and Mapper modules (30 tests)
./test_switch       # Switch module (37 tests)
./test_spad         # SPAD module (19 tests)
```

See [TESTING.md](TESTING.md) for detailed test documentation.

**Current Status:** ✓ All 154 tests passing

## Implementation Status

The C++ implementation includes all core modules with proper structure. Some functions have stub implementations that need completion. See [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) for details on which functions need additional work.

**Verified Working:**
- Matrix generation and operations
- PE state management
- Switch state management
- SPAD memory operations
- Mapper virtual-to-physical mapping
- **MatrixLoader (NEW!)** - Index matrices, offsets, tiling

**Needs Implementation:**
- Memory hierarchy interface (MemoryController)
- Full simulation step logic (run_switches, run_b_loader, etc.)

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.15 or higher

