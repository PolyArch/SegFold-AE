cmake_minimum_required(VERSION 3.15)
project(csegfold VERSION 0.1.0 LANGUAGES CXX)

# Generate compile_commands.json for language servers (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch yaml-cpp for YAML configuration loading
find_package(yaml-cpp QUIET)
if(NOT TARGET yaml-cpp)
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# Fetch indicators for progress bars
include(FetchContent)
FetchContent_Declare(
    indicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators.git
    GIT_TAG v2.3
)
FetchContent_MakeAvailable(indicators)

# Option to enable Ramulator2 DRAM simulation
option(ENABLE_RAMULATOR2 "Enable Ramulator2 DRAM simulation" OFF)

if(ENABLE_RAMULATOR2)
    # Patch ramulator2:
    # 1. Build as STATIC (upstream builds SHARED by default)
    # 2. Fix CMAKE_SOURCE_DIR -> PROJECT_SOURCE_DIR (wrong when used as subproject)
    FetchContent_Declare(
        ramulator2
        GIT_REPOSITORY https://github.com/CMU-SAFARI/ramulator2.git
        GIT_TAG main
        PATCH_COMMAND sed -i
            -e "s/add_library(ramulator SHARED)/add_library(ramulator STATIC)/"
            -e "s|\\$\\{CMAKE_SOURCE_DIR\\}|\\$\\{PROJECT_SOURCE_DIR\\}|g"
            CMakeLists.txt
    )
    FetchContent_MakeAvailable(ramulator2)

    # Also add ramulator2's src/ to include path for csegfold sources
    include_directories(${ramulator2_SOURCE_DIR}/src)

    add_compile_definitions(CSEGFOLD_HAS_RAMULATOR2)
    message(STATUS "Ramulator2 DRAM simulation enabled")
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
# Include indicators (header-only library)
# indicators is header-only, so we need to add its include directory
if(indicators_SOURCE_DIR)
    include_directories(${indicators_SOURCE_DIR}/include)
endif()

# Source files
set(MODULES_SOURCES
    src/modules/module.cpp
    src/modules/pe.cpp
    src/modules/switch.cpp
    src/modules/spad.cpp
    src/modules/mapper.cpp
    src/modules/lookuptable.cpp
    src/modules/matrixLoader.cpp
    src/modules/memoryController.cpp
)

set(SIMULATOR_SOURCES
    src/simulator/simulator.cpp
    src/simulator/segfoldSimulator.cpp
    src/simulator/pe.cpp
    src/simulator/switch.cpp
    src/simulator/stats.cpp
    src/simulator/memoryController.cpp
)

set(MATRIX_SOURCES
    src/matrix/generator.cpp
)

set(MEMORY_SOURCES
    src/memory/MemoryBackend.cpp
    src/memory/CacheModel.cpp
    src/memory/IdealBackend.cpp
    src/memory/RamulatorBackend.cpp
)

set(SOURCES
    ${MODULES_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
    src/main.cpp
)

# Static library exposing only the memory/DRAM backend via C FFI.
# Used by external simulators (e.g., spada-sim) that need Ramulator2 timing
# but have their own cache hierarchy.
add_library(csegfold_mem STATIC
    src/memory/MemoryBackend.cpp
    src/memory/CacheModel.cpp
    src/memory/IdealBackend.cpp
    src/memory/RamulatorBackend.cpp
    src/memory/csegfold_ffi.cpp
)
target_include_directories(csegfold_mem PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(csegfold_mem PUBLIC cxx_std_20)
target_link_libraries(csegfold_mem PUBLIC yaml-cpp)
if(ENABLE_RAMULATOR2)
    # Ramulator2 uses static self-registration (factory pattern).
    # --whole-archive prevents the linker from stripping "unused" object
    # files that contain the REGISTER_DEF macros.
    target_link_libraries(csegfold_mem PUBLIC
        -Wl,--whole-archive ramulator -Wl,--no-whole-archive)
endif()
install(TARGETS csegfold_mem ARCHIVE DESTINATION lib)

# Create executable
add_executable(csegfold ${SOURCES})

# Target properties
target_compile_features(csegfold PUBLIC cxx_std_20)
target_link_libraries(csegfold PUBLIC yaml-cpp)
if(ENABLE_RAMULATOR2)
    target_link_libraries(csegfold PUBLIC
        -Wl,--whole-archive ramulator -Wl,--no-whole-archive)
endif()

# Helper: link ramulator to any target that compiles MEMORY_SOURCES
# (RamulatorBackend.cpp includes ramulator2 headers which need spdlog)
function(link_ramulator_if_enabled target)
    if(ENABLE_RAMULATOR2)
        target_link_libraries(${target} PUBLIC
            -Wl,--whole-archive ramulator -Wl,--no-whole-archive)
    endif()
endfunction()

# Installation
install(TARGETS csegfold
    RUNTIME DESTINATION bin
)

# Ablation study runner executable
add_executable(ablation_runner
    src/runner/ablation_runner.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(ablation_runner PUBLIC cxx_std_20)
target_link_libraries(ablation_runner PUBLIC yaml-cpp)
link_ramulator_if_enabled(ablation_runner)
install(TARGETS ablation_runner
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Enable testing
enable_testing()

# Test executable for generator
add_executable(test_generator
    tests/test_generator.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_generator PUBLIC cxx_std_20)
target_link_libraries(test_generator PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_generator)
add_test(NAME TestGenerator COMMAND test_generator)

# Test executable for PE and Mapper
add_executable(test_pe_mapper
    tests/test_pe.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_pe_mapper PUBLIC cxx_std_20)
target_link_libraries(test_pe_mapper PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_pe_mapper)
add_test(NAME TestPEMapper COMMAND test_pe_mapper)

# Test executable for Switch
add_executable(test_switch
    tests/test_switch.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_switch PUBLIC cxx_std_20)
target_link_libraries(test_switch PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_switch)
add_test(NAME TestSwitch COMMAND test_switch)

# Test executable for SPAD
add_executable(test_spad
    tests/test_spad.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_spad PUBLIC cxx_std_20)
target_link_libraries(test_spad PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_spad)
add_test(NAME TestSPAD COMMAND test_spad)

# Test executable for MatrixLoader
add_executable(test_matrixLoader
    tests/test_matrixLoader.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_matrixLoader PUBLIC cxx_std_20)
target_link_libraries(test_matrixLoader PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_matrixLoader)
add_test(NAME TestMatrixLoader COMMAND test_matrixLoader)

# Test executable for MemoryController
add_executable(test_memoryController
    tests/test_memoryController.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_memoryController PUBLIC cxx_std_20)
target_link_libraries(test_memoryController PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_memoryController)
add_test(NAME TestMemoryController COMMAND test_memoryController)

# Test executable for Switch Simulator
add_executable(test_switch_simulator
    tests/test_switch_simulator.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_switch_simulator PUBLIC cxx_std_20)
target_link_libraries(test_switch_simulator PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_switch_simulator)
add_test(NAME TestSwitchSimulator COMMAND test_switch_simulator)

# Test executable for Simulator MemoryController
add_executable(test_simulator_memoryController
    tests/test_simulator_memoryController.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_simulator_memoryController PUBLIC cxx_std_20)
target_link_libraries(test_simulator_memoryController PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_simulator_memoryController)
add_test(NAME TestSimulatorMemoryController COMMAND test_simulator_memoryController)

# Test executable for Simulator FIFO and PE
add_executable(test_simulator_fifo_pe
    tests/test_simulator_fifo_pe.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_simulator_fifo_pe PUBLIC cxx_std_20)
target_link_libraries(test_simulator_fifo_pe PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_simulator_fifo_pe)
add_test(NAME TestSimulatorFifoPe COMMAND test_simulator_fifo_pe)

# Test executable for C++ End-to-End Simulation
add_executable(test_e2e_cpp
    tests/test_e2e_cpp.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_e2e_cpp PUBLIC cxx_std_20)
target_link_libraries(test_e2e_cpp PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_e2e_cpp)
add_test(NAME TestE2ECpp COMMAND test_e2e_cpp)

# Test executable for C++ vs Python Cycle Comparison
add_executable(test_compare_python
    tests/test_compare_python.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_compare_python PUBLIC cxx_std_20)
target_link_libraries(test_compare_python PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_compare_python)
add_test(NAME TestComparePython COMMAND test_compare_python)

# Test executable for Trace Generation
add_executable(test_trace
    tests/test_trace.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_trace PUBLIC cxx_std_20)
target_link_libraries(test_trace PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_trace)
add_test(NAME TestTrace COMMAND test_trace)

# Test executable for Sparse 4x4 Matrix Functionality
add_executable(test_sparse_4x4
    tests/test_sparse_4x4.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_sparse_4x4 PUBLIC cxx_std_20)
target_link_libraries(test_sparse_4x4 PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_sparse_4x4)
add_test(NAME TestSparse4x4 COMMAND test_sparse_4x4)

# Test executable for comparing tiling output
add_executable(test_compare_tiling
    tests/test_compare_tiling.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_compare_tiling PUBLIC cxx_std_20)
target_link_libraries(test_compare_tiling PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_compare_tiling)
add_test(NAME TestCompareTiling COMMAND test_compare_tiling)

# Test executable for SuiteSparse
add_executable(test_suitesparse
    tests/test_suitesparse.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_suitesparse PUBLIC cxx_std_20)
target_link_libraries(test_suitesparse PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_suitesparse)
add_test(NAME TestSuiteSparse COMMAND test_suitesparse)

# Test executable for Statistics
add_executable(test_stats
    tests/test_stats.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_stats PUBLIC cxx_std_20)
target_link_libraries(test_stats PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_stats)
add_test(NAME TestStats COMMAND test_stats)

# Test executable for Ablation Studies
add_executable(test_ablation
    tests/test_ablation.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_ablation PUBLIC cxx_std_20)
target_link_libraries(test_ablation PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_ablation)
add_test(NAME TestAblation COMMAND test_ablation)

# Test executable for Performance Profiling
add_executable(test_perf_profile
    tests/test_perf_profile.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_perf_profile PUBLIC cxx_std_20)
target_link_libraries(test_perf_profile PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_perf_profile)
add_test(NAME TestPerfProfile COMMAND test_perf_profile)

# Test executable for B Loader FIFO
add_executable(test_b_loader_fifo
    tests/test_b_loader_fifo.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${SIMULATOR_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_b_loader_fifo PUBLIC cxx_std_20)
target_link_libraries(test_b_loader_fifo PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_b_loader_fifo)
add_test(NAME TestBLoaderFifo COMMAND test_b_loader_fifo)

# Test executable for Memory Backend
add_executable(test_memory_backend
    tests/test_memory_backend.cpp
    ${MODULES_SOURCES}
    ${MATRIX_SOURCES}
    ${MEMORY_SOURCES}
)
target_compile_features(test_memory_backend PUBLIC cxx_std_20)
target_link_libraries(test_memory_backend PUBLIC yaml-cpp)
link_ramulator_if_enabled(test_memory_backend)
add_test(NAME TestMemoryBackend COMMAND test_memory_backend)
