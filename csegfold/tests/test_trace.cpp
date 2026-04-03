#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

using namespace csegfold;
using json = nlohmann::json;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_trace_generation() {
    std::cout << "\n=== Testing Trace Generation for 4x4 Matrix ===" << std::endl;
    
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "10000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "false"},
        {"save_trace", "true"}  // Enable trace saving
    });
    
    // Create 4x4 test matrices (all ones for dense multiplication)
    Matrix<int8_t> A(4, 4, 1);  // All ones
    Matrix<int8_t> B(4, 4, 1);  // All ones
    
    std::cout << "  Created matrices: A(4x4) with density=1.0, B(4x4) with density=1.0" << std::endl;
    std::cout << "  Expected output: C = A * B (all 4s)" << std::endl;
    
    // Print input matrices
    std::cout << "  Input matrix A:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << static_cast<int>(A(i, j));
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    SegfoldSimulator sim(A, B);
    
    test_assert(sim.cfg.save_trace == true, "save_trace is enabled");
    test_assert(sim.matrix.M == 4, "Matrix M dimension correct");
    test_assert(sim.matrix.K == 4, "Matrix K dimension correct");
    test_assert(sim.matrix.N == 4, "Matrix N dimension correct");
    
    // Check initial state
    std::cout << "  Initial state:" << std::endl;
    std::cout << "    - Active switches: " << sim.switchModule.num_active_switches() << std::endl;
    std::cout << "    - Active PEs: " << sim.peModule.num_active_pes() << std::endl;
    std::cout << "    - Trace size before run: " << sim.trace.size() << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    test_assert(sim.trace.size() > 0, "Trace contains at least one cycle");
    // Note: trace only contains main simulation cycles, not cleanup cycles
    // So trace.size() <= stats.cycle (cleanup steps don't log)
    test_assert(sim.trace.size() <= static_cast<size_t>(sim.stats.cycle), 
                "Trace size is less than or equal to total cycles (cleanup cycles not logged)");
    
    std::cout << "  Simulation completed in " << sim.stats.cycle << " cycles" << std::endl;
    std::cout << "  Trace contains " << sim.trace.size() << " cycles (main loop only, cleanup not traced)" << std::endl;
    
    // Dump trace to file
    const std::string trace_filename = "test_trace_4x4.json";
    std::cout << "  Dumping trace to " << trace_filename << "..." << std::endl;
    sim.dump_trace(trace_filename);
    
    // Verify trace file exists
    struct stat buffer;
    test_assert(stat(trace_filename.c_str(), &buffer) == 0, "Trace file was created");
    
    // Read and verify trace file content
    std::ifstream trace_file(trace_filename);
    test_assert(trace_file.is_open(), "Trace file can be opened");
    
    json trace_data;
    try {
        trace_file >> trace_data;
        test_assert(true, "Trace file contains valid JSON");
    } catch (const std::exception& e) {
        test_assert(false, std::string("Trace file JSON parsing failed: ") + e.what());
    }
    
    // Verify trace structure
    test_assert(trace_data.contains("A"), "Trace contains matrix A");
    test_assert(trace_data.contains("B"), "Trace contains matrix B");
    test_assert(trace_data.contains("trace"), "Trace contains trace array");
    
    // Verify matrices
    test_assert(trace_data["A"].is_array(), "Matrix A is an array");
    test_assert(trace_data["B"].is_array(), "Matrix B is an array");
    test_assert(trace_data["A"].size() == 4, "Matrix A has 4 rows");
    test_assert(trace_data["B"].size() == 4, "Matrix B has 4 rows");
    
    // Verify trace array
    test_assert(trace_data["trace"].is_array(), "Trace is an array");
    test_assert(trace_data["trace"].size() > 0, "Trace array is not empty");
    test_assert(trace_data["trace"].size() == sim.trace.size(), 
                "Trace array size matches simulator trace size");
    
    // Verify first trace entry
    if (trace_data["trace"].size() > 0) {
        json first_entry = trace_data["trace"][0];
        test_assert(first_entry.contains("cycle"), "First trace entry contains cycle");
        test_assert(first_entry.contains("b_positions"), "First trace entry contains b_positions");
        test_assert(first_entry.contains("num_active_pes"), "First trace entry contains num_active_pes");
        test_assert(first_entry.contains("utilization"), "First trace entry contains utilization");
        
        test_assert(first_entry["cycle"].is_number(), "Cycle is a number");
        test_assert(first_entry["b_positions"].is_array(), "b_positions is an array");
        test_assert(first_entry["num_active_pes"].is_number(), "num_active_pes is a number");
        test_assert(first_entry["utilization"].is_number(), "utilization is a number");
        
        std::cout << "  First trace entry:" << std::endl;
        std::cout << "    - Cycle: " << first_entry["cycle"] << std::endl;
        std::cout << "    - b_positions count: " << first_entry["b_positions"].size() << std::endl;
        std::cout << "    - Active PEs: " << first_entry["num_active_pes"] << std::endl;
        std::cout << "    - Utilization: " << first_entry["utilization"] << std::endl;
    }
    
    // Verify last trace entry
    if (trace_data["trace"].size() > 1) {
        json last_entry = trace_data["trace"][trace_data["trace"].size() - 1];
        // Last trace entry should be from the main loop (cleanup cycles don't log)
        // So it should be less than or equal to stats.cycle - 1
        test_assert(last_entry["cycle"].get<int>() <= sim.stats.cycle - 1, 
                    "Last trace entry cycle is from main loop (cleanup cycles not logged)");
        std::cout << "  Last trace entry:" << std::endl;
        std::cout << "    - Cycle: " << last_entry["cycle"] << " (final cycle: " << sim.stats.cycle << ")" << std::endl;
        std::cout << "    - b_positions count: " << last_entry["b_positions"].size() << std::endl;
        std::cout << "    - Active PEs: " << last_entry["num_active_pes"] << std::endl;
    }
    
    // Verify output correctness
    if (sim.success) {
        std::cout << "  Final acc_output:" << std::endl;
        for (int i = 0; i < 4; ++i) {
            std::cout << "    [";
            for (int j = 0; j < 4; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < 3) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result");
    }
    
    trace_file.close();
    std::cout << "  Trace file verification complete!" << std::endl;
    struct stat file_stat;
    if (stat(trace_filename.c_str(), &file_stat) == 0) {
        std::cout << "  Trace file: " << trace_filename << " (" 
                  << file_stat.st_size << " bytes)" << std::endl;
    }
}

void test_trace_with_different_matrices() {
    std::cout << "\n=== Testing Trace with Sparse 4x4 Matrix ===" << std::endl;
    
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "10000"},
        {"verbose", "true"},
        {"enable_tile_eviction", "false"},
        {"save_trace", "true"},
        {"is_dense", "false"}
    });
    
    // Create sparse 4x4 matrices (diagonal pattern)
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i < 3) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
    }
    
    std::cout << "  Created sparse matrices: A(4x4), B(4x4)" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.trace.size() > 0, "Trace contains at least one cycle");
    
    // Dump trace to file
    const std::string trace_filename = "test_trace_4x4_sparse.json";
    std::cout << "  Dumping trace to " << trace_filename << "..." << std::endl;
    sim.dump_trace(trace_filename);
    
    // Verify trace file exists and is valid
    struct stat buffer2;
    test_assert(stat(trace_filename.c_str(), &buffer2) == 0, "Sparse trace file was created");
    
    std::ifstream trace_file(trace_filename);
    json trace_data;
    trace_file >> trace_data;
    trace_file.close();
    
    test_assert(trace_data.contains("A"), "Sparse trace contains matrix A");
    test_assert(trace_data.contains("B"), "Sparse trace contains matrix B");
    test_assert(trace_data.contains("trace"), "Sparse trace contains trace array");
    test_assert(trace_data["trace"].size() == sim.trace.size(), 
                "Sparse trace array size matches");
    
    std::cout << "  Sparse trace file verification complete!" << std::endl;
    struct stat file_stat2;
    if (stat(trace_filename.c_str(), &file_stat2) == 0) {
        std::cout << "  Trace file: " << trace_filename << " (" 
                  << file_stat2.st_size << " bytes)" << std::endl;
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Testing Trace Generation Functionality" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_trace_generation();
        test_trace_with_different_matrices();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All trace tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "\nTo animate the trace, run:" << std::endl;
        std::cout << "  python3 csegfold/tools/animate_trace.py test_trace_4x4.json --rows 4 --cols 4" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}

