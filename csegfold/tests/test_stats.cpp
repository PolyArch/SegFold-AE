#include "csegfold/modules/module.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/matrix/generator.hpp"
#include "test_utils.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstdio>

using namespace csegfold;
using namespace test_utils;

void test_new_statistics() {
    TEST_SECTION("Testing New Statistics");
    
    // Get temporary directory
    std::string tmp_dir = get_tmp_dir();
    
    // Reset and configure (matching test_compare_python.cpp pattern)
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},      // 4x4 PE array
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "50000"},
        {"verbose", "false"},
        {"enable_spad", "true"},
        {"save_trace", "true"},            // Enable trace for animation
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"}
    });
    
    // Create 8x8 dense matrices
    Matrix<int8_t> A(8, 8, 1);  // All ones for simplicity
    Matrix<int8_t> B(8, 8, 1);  // All ones for simplicity
    
    std::cout << "Creating simulator with 8x8 dense matrices and 4x4 PEs..." << std::endl;
    SegfoldSimulator sim(A, B);
    
    std::cout << "Running simulation..." << std::endl;
    sim.run();
    
    std::cout << "Simulation completed in " << sim.stats.cycle << " cycles" << std::endl;
    
    // Test 1: Check that trace_b_elements_on_switch is populated
    test_assert(!sim.stats.trace_b_elements_on_switch.empty(), 
                "trace_b_elements_on_switch is populated");
    
    std::cout << "  trace_b_elements_on_switch size: " 
              << sim.stats.trace_b_elements_on_switch.size() << std::endl;
    
    // Test 2: Check that trace_pes_waiting_spad is populated
    test_assert(!sim.stats.trace_pes_waiting_spad.empty(), 
                "trace_pes_waiting_spad is populated");
    
    std::cout << "  trace_pes_waiting_spad size: " 
              << sim.stats.trace_pes_waiting_spad.size() << std::endl;
    
    // Test 3: Check that averages are calculated (should be >= 0)
    test_assert(sim.stats.avg_b_elements_on_switch >= 0, 
                "avg_b_elements_on_switch is calculated");
    
    std::cout << "  avg_b_elements_on_switch: " 
              << sim.stats.avg_b_elements_on_switch << std::endl;
    
    test_assert(sim.stats.avg_pes_waiting_spad >= 0, 
                "avg_pes_waiting_spad is calculated");
    
    std::cout << "  avg_pes_waiting_spad: " 
              << sim.stats.avg_pes_waiting_spad << std::endl;
    
    // Print matrix NNZ counts
    std::cout << "  a_nnz: " << sim.stats.a_nnz << std::endl;
    std::cout << "  b_nnz: " << sim.stats.b_nnz << std::endl;
    std::cout << "  c_nnz: " << sim.stats.c_nnz << std::endl;
    
    // Test 4: Check trace sizes match cycle count
    test_assert(static_cast<int>(sim.stats.trace_b_elements_on_switch.size()) == sim.stats.cycle,
                "trace_b_elements_on_switch size matches cycle count");
    
    test_assert(static_cast<int>(sim.stats.trace_pes_waiting_spad.size()) == sim.stats.cycle,
                "trace_pes_waiting_spad size matches cycle count");
    
    // Test 5: Save stats to temporary file (matching test_compare_python.cpp naming)
    std::string temp_file = tmp_dir + "/segfold_stats_cpp_8_dense.json";
    std::cout << "\nSaving stats to " << temp_file << "..." << std::endl;
    sim.dump_stats(temp_file, false);
    
    // Verify file exists
    std::ifstream file(temp_file);
    test_assert(file.good(), "Stats file was created");
    
    size_t file_size = 0;
    if (file.good()) {
        // Read and display file size
        file.seekg(0, std::ios::end);
        file_size = file.tellg();
        std::cout << "  Stats file size: " << file_size << " bytes" << std::endl;
        file.close();
        
        // Read and verify the stats contain our new fields
        std::ifstream stats_file(temp_file);
        std::string content((std::istreambuf_iterator<char>(stats_file)),
                           std::istreambuf_iterator<char>());
        stats_file.close();
        
        bool has_b_elements = content.find("avg_b_elements_on_switch") != std::string::npos;
        bool has_pes_waiting = content.find("avg_pes_waiting_spad") != std::string::npos;
        
        test_assert(has_b_elements, "Stats file contains avg_b_elements_on_switch");
        test_assert(has_pes_waiting, "Stats file contains avg_pes_waiting_spad");
        
        std::cout << "\nStats file content preview (first 500 chars):" << std::endl;
        std::cout << content.substr(0, std::min(size_t(500), content.size())) << "..." << std::endl;
    }
    
    // Test 6: Save stats with traces to a different file
    std::string temp_file_with_traces = tmp_dir + "/segfold_stats_cpp_8_dense_with_traces.json";
    std::cout << "\nSaving stats with traces to " << temp_file_with_traces << "..." << std::endl;
    sim.dump_stats(temp_file_with_traces, true);
    
    std::ifstream trace_file(temp_file_with_traces);
    test_assert(trace_file.good(), "Stats file with traces was created");
    
    if (trace_file.good()) {
        trace_file.seekg(0, std::ios::end);
        size_t trace_file_size = trace_file.tellg();
        std::cout << "  Stats file with traces size: " << trace_file_size << " bytes" << std::endl;
        trace_file.close();
        
        // Verify traces file is larger (contains trace data)
        test_assert(trace_file_size > file_size, 
                    "Stats file with traces is larger than without");
    }
    
    // Test 7: Save config file for animation (matching test_compare_python.cpp naming)
    std::string config_file = tmp_dir + "/segfold_config_cpp_8_dense.json";
    std::cout << "\nSaving config to " << config_file << "..." << std::endl;
    sim.dump_config(config_file);
    test_assert(file_exists(config_file), "Config file was created");
    
    // Test 8: Save trace file for animation (matching test_compare_python.cpp naming)
    std::string trace_anim_file = tmp_dir + "/segfold_trace_cpp_8_dense.json";
    std::cout << "Saving trace for animation to " << trace_anim_file << "..." << std::endl;
    sim.dump_trace(trace_anim_file);
    test_assert(file_exists(trace_anim_file), "Trace file was created");
    
    std::cout << "\n=== All Statistics Tests Passed ===" << std::endl;
}

int main() {
    try {
        std::string workspace_root = get_workspace_root();
        std::string tmp_dir = workspace_root + "/tmp";

        test_new_statistics();

        std::cout << "\n✅ All tests passed successfully!" << std::endl;
        std::cout << "\nTemporary files created in " << tmp_dir << ":" << std::endl;
        std::cout << "  Basic test (8x8 dense matrices, 4x4 PEs):" << std::endl;
        std::cout << "    - segfold_stats_cpp_8_dense.json" << std::endl;
        std::cout << "    - segfold_stats_cpp_8_dense_with_traces.json" << std::endl;
        std::cout << "    - segfold_config_cpp_8_dense.json" << std::endl;
        std::cout << "    - segfold_trace_cpp_8_dense.json" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
