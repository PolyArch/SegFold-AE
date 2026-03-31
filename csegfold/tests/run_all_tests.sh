#!/bin/bash

# Run all unit tests for csegfold

cd "$(dirname "$0")/../build" || exit 1

echo "========================================"
echo "  Running All csegfold Unit Tests"
echo "========================================"
echo ""

FAILED=0
PASSED=0

run_test() {
    local test_name=$1
    local test_exe=$2
    
    echo "Running $test_name..."
    if ./$test_exe; then
        echo "✓ $test_name PASSED"
        ((PASSED++))
    else
        echo "✗ $test_name FAILED"
        ((FAILED++))
    fi
    echo ""
}

# Run all tests
run_test "Matrix Generator Tests" "test_generator"
run_test "PE & Mapper Tests" "test_pe_mapper"
run_test "Switch Tests" "test_switch"
run_test "SPAD Tests" "test_spad"
run_test "MatrixLoader Tests" "test_matrixLoader"
run_test "MemoryController Tests" "test_memoryController"
run_test "Switch Simulator Tests" "test_switch_simulator"
run_test "Simulator MemoryController Tests" "test_simulator_memoryController"
run_test "Simulator FIFO and PE Tests" "test_simulator_fifo_pe"
run_test "C++ End-to-End Simulation Tests" "test_e2e_cpp"
run_test "C++ vs Python Cycle Comparison Tests" "test_compare_python"
run_test "Trace Generation Tests" "test_trace"

echo "========================================"
echo "  Test Summary"
echo "========================================"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "✓ All tests passed!"
    exit 0
else
    echo "✗ Some tests failed"
    exit 1
fi

