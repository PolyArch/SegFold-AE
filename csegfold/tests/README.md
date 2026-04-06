# Tests for csegfold

## Running Tests

```bash
# Run all 20 registered tests
ctest --test-dir csegfold/build --output-on-failure

# Run a specific test by name
ctest --test-dir csegfold/build -R TestSparse4x4

# Run a test executable directly (for verbose output)
./csegfold/build/test_e2e_cpp
```

## Test Files

There are 21 test source files. 20 are registered as CTest targets; `test_mapper.cpp` is a standalone file not registered in CMakeLists.txt (its functionality is covered by `test_pe_mapper`).

### Unit Tests

| Test File | CTest Name | Executable | Description |
|-----------|------------|------------|-------------|
| `test_generator.cpp` | TestGenerator | `test_generator` | Matrix creation, indexing, multiplication, CSR format |
| `test_pe.cpp` | TestPEMapper | `test_pe_mapper` | PE state transitions (IDLE, LOAD, MAC) and mapper integration |
| `test_mapper.cpp` | — | — | Virtual-to-physical PE mapping and eviction (not registered in CTest) |
| `test_switch.cpp` | TestSwitch | `test_switch` | Switch state transitions and C column comparisons |
| `test_spad.cpp` | TestSPAD | `test_spad` | Scratchpad store/load operations and hit/miss tracking |
| `test_matrixLoader.cpp` | TestMatrixLoader | `test_matrixLoader` | Matrix tiling (dense/group), CSR indexing, offsets |
| `test_memoryController.cpp` | TestMemoryController | `test_memoryController` | Memory controller creation and B row management |

### Simulator Tests

| Test File | CTest Name | Executable | Description |
|-----------|------------|------------|-------------|
| `test_switch_simulator.cpp` | TestSwitchSimulator | `test_switch_simulator` | Switch simulation functions and state transitions |
| `test_simulator_memoryController.cpp` | TestSimulatorMemoryController | `test_simulator_memoryController` | B loader and memory interface simulation |
| `test_simulator_fifo_pe.cpp` | TestSimulatorFifoPe | `test_simulator_fifo_pe` | FIFO-to-PE data transfer and `is_new_c` logic |
| `test_memory_backend.cpp` | TestMemoryBackend | `test_memory_backend` | Memory backend (ideal and Ramulator2) |
| `test_b_loader_fifo.cpp` | TestBLoaderFifo | `test_b_loader_fifo` | B loader FIFO behavior and switch-PE data flow |

### Integration / End-to-End Tests

| Test File | CTest Name | Executable | Description |
|-----------|------------|------------|-------------|
| `test_sparse_4x4.cpp` | TestSparse4x4 | `test_sparse_4x4` | Quick sanity check with 4x4 sparse multiplication |
| `test_e2e_cpp.cpp` | TestE2ECpp | `test_e2e_cpp` | End-to-end simulation (4x4 to 128x128, dense and sparse) |
| `test_suitesparse.cpp` | TestSuiteSparse | `test_suitesparse` | End-to-end with real SuiteSparse matrices |
| `test_compare_python.cpp` | TestComparePython | `test_compare_python` | Compare C++ output against Python reference simulator |
| `test_compare_tiling.cpp` | TestCompareTiling | `test_compare_tiling` | Verify tiling consistency between implementations |

### Analysis / Profiling Tests

| Test File | CTest Name | Executable | Description |
|-----------|------------|------------|-------------|
| `test_trace.cpp` | TestTrace | `test_trace` | Trace generation and JSON output validation |
| `test_stats.cpp` | TestStats | `test_stats` | Statistics collection and serialization |
| `test_ablation.cpp` | TestAblation | `test_ablation` | Ablation study configurations |
| `test_perf_profile.cpp` | TestPerfProfile | `test_perf_profile` | Performance profiling with configurable parameters |

## Adding New Tests

1. Create `tests/test_<module>.cpp`
2. Use `test_assert(condition, "test name")` from `test_utils.hpp` for assertions
3. Add to `CMakeLists.txt`:
```cmake
add_executable(test_<module>
    tests/test_<module>.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_<module> PUBLIC cxx_std_20)
target_link_libraries(test_<module> PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_<module>)
add_test(NAME Test<Module> COMMAND test_<module>)
```

If the test needs simulation logic, also add `${SIMULATOR_SOURCES}` to the executable's source list.
