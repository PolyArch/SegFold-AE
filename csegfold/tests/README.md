# Unit Tests for csegfold

## Running Tests

```bash
# From project root
ctest --test-dir build

# With detailed output on failures
ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R TestSparse4x4
```

## Test Files

| Test File | Description |
|-----------|-------------|
| `test_generator.cpp` | Matrix creation, indexing, multiplication, and CSR format conversion |
| `test_pe.cpp` | PE state management and status transitions (IDLE, LOAD, MAC) |
| `test_mapper.cpp` | Virtual-to-physical PE mapping and eviction operations |
| `test_switch.cpp` | Switch state transitions and C column comparisons |
| `test_spad.cpp` | Scratchpad store/load operations and hit/miss tracking |
| `test_matrixLoader.cpp` | Matrix tiling, dense/group tiling, and CSR indexing |
| `test_memoryController.cpp` | Memory controller creation and B row management |
| `test_simulator_memoryController.cpp` | B loader and memory interface simulation functions |
| `test_simulator_fifo_pe.cpp` | FIFO-to-PE data transfer and `is_new_c` logic |
| `test_switch_simulator.cpp` | Switch simulation functions and state transitions |
| `test_sparse_4x4.cpp` | Quick sanity check with 4x4 sparse matrix multiplication |
| `test_e2e_cpp.cpp` | Comprehensive end-to-end tests (4x4 to 128x128, dense/sparse) |
| `test_compare_python.cpp` | Compare C++ simulator output against Python reference |
| `test_compare_tiling.cpp` | Verify tiling consistency between implementations |
| `test_trace.cpp` | Trace generation and JSON output validation |
| `test_stats.cpp` | Statistics collection and serialization |
| `test_suitesparse.cpp` | End-to-end tests with SuiteSparse matrices |
| `test_ablation.cpp` | Ablation study configurations (e.g., `ablat_dynmap`) |
| `test_perf_profile.cpp` | Performance profiling with configurable parameters |
| `test_b_loader_fifo.cpp` | B loader FIFO behavior and switch-PE data flow |

## Adding New Tests

1. Create `tests/test_<module>.cpp`
2. Use `test_assert(condition, "test name")` for assertions
3. Add to `CMakeLists.txt`:
```cmake
add_executable(test_<module>
    tests/test_<module>.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
)
target_compile_features(test_<module> PUBLIC cxx_std_20)
add_test(NAME Test<Module> COMMAND test_<module>)
```
