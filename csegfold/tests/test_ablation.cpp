#include "csegfold/modules/module.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/matrix/generator.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <fstream>

using namespace csegfold;
using namespace test_utils;

void test_ablat_dynmap_disabled() {
    TEST_SECTION("Test ablat_dynmap = false (Normal Operation)");
    
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
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"},
        {"ablat_dynmap", "false"},        // Normal operation
        {"spad_load_ports_per_bank", "1"},
        {"spad_store_ports_per_bank", "1"}
    });
    
    Matrix<int8_t> A(8, 8, 1);
    Matrix<int8_t> B(8, 8, 1);
    
    SegfoldSimulator sim(A, B);
    sim.run();
    
    std::cout << "  Cycles: " << sim.stats.cycle << std::endl;
    std::cout << "  SPAD load hits: " << sim.stats.spad_load_hits << std::endl;
    std::cout << "  SPAD load misses: " << sim.stats.spad_load_misses << std::endl;
    std::cout << "  SPAD stores: " << sim.stats.spad_stores << std::endl;
    std::cout << "  Avg PEs waiting SPAD: " << sim.stats.avg_pes_waiting_spad << std::endl;
    
    TEST_ASSERT(sim.success, "Simulation completed successfully");
}

void test_ablat_dynmap_enabled() {
    TEST_SECTION("Test ablat_dynmap = true (Ablation Mode)");
    
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
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"},
        {"ablat_dynmap", "true"},         // Ablation: disable C caching
        {"spad_load_ports_per_bank", "1"},
        {"spad_store_ports_per_bank", "1"}
    });
    
    Matrix<int8_t> A(8, 8, 1);
    Matrix<int8_t> B(8, 8, 1);
    
    SegfoldSimulator sim(A, B);
    sim.run();
    
    std::cout << "  Cycles: " << sim.stats.cycle << std::endl;
    std::cout << "  SPAD load hits: " << sim.stats.spad_load_hits << std::endl;
    std::cout << "  SPAD load misses: " << sim.stats.spad_load_misses << std::endl;
    std::cout << "  SPAD stores: " << sim.stats.spad_stores << std::endl;
    std::cout << "  Avg PEs waiting SPAD: " << sim.stats.avg_pes_waiting_spad << std::endl;
    
    TEST_ASSERT(sim.stats.spad_load_hits == 0, "No SPAD loads with ablat_dynmap=true");
    TEST_ASSERT(sim.stats.spad_load_misses == 0, "No SPAD load misses with ablat_dynmap=true");
    TEST_ASSERT(sim.stats.spad_stores > 0, "SPAD stores still occur");
    TEST_ASSERT(sim.success, "Simulation completed successfully");
}

void test_ablat_dynmap_with_multiport() {
    TEST_SECTION("Test ablat_dynmap = true with 4 SPAD ports");
    
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
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"},
        {"ablat_dynmap", "true"},            // Ablation: disable C caching
        {"spad_load_ports_per_bank", "4"},   // 4 load ports
        {"spad_store_ports_per_bank", "4"}   // 4 store ports
    });
    
    Matrix<int8_t> A(8, 8, 1);
    Matrix<int8_t> B(8, 8, 1);
    
    SegfoldSimulator sim(A, B);
    sim.run();
    
    std::cout << "  Cycles: " << sim.stats.cycle << std::endl;
    std::cout << "  SPAD load hits: " << sim.stats.spad_load_hits << std::endl;
    std::cout << "  SPAD stores: " << sim.stats.spad_stores << std::endl;
    std::cout << "  Avg PEs waiting SPAD: " << sim.stats.avg_pes_waiting_spad << std::endl;
    
    TEST_ASSERT(sim.stats.spad_load_hits == 0, "No SPAD loads with ablat_dynmap=true");
    TEST_ASSERT(sim.stats.spad_stores > 0, "SPAD stores occur");
    TEST_ASSERT(sim.success, "Simulation completed successfully");
}

void test_comparison_all_configs() {
    TEST_SECTION("Comparison: All Three Configurations");
    
    Matrix<int8_t> A(8, 8, 1);
    Matrix<int8_t> B(8, 8, 1);
    
    struct ConfigResult {
        std::string name;
        int cycles;
        int spad_loads;
        int spad_stores;
        double avg_pes_waiting;
    };
    
    std::vector<ConfigResult> results;
    
    // Config 1: Ours
    {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_spad", "true"},
            {"enable_dynamic_routing", "true"},
            {"enable_dynamic_scheduling", "true"},
            {"enable_memory_hierarchy", "false"},
            {"ablat_dynmap", "false"},
            {"spad_load_ports_per_bank", "1"},
            {"spad_store_ports_per_bank", "1"},
            {"verbose", "false"}
        });
        
        SegfoldSimulator sim(A, B);
        sim.run();
        
        results.push_back({
            "Ours (Optimized)",
            sim.stats.cycle,
            sim.stats.spad_load_hits,
            sim.stats.spad_stores,
            sim.stats.avg_pes_waiting_spad
        });
    }
    
    // Config 2: No Dynamic Routing
    {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_spad", "true"},
            {"enable_dynamic_routing", "false"},
            {"enable_dynamic_scheduling", "false"},
            {"enable_memory_hierarchy", "false"},
            {"ablat_dynmap", "false"},
            {"spad_load_ports_per_bank", "1"},
            {"spad_store_ports_per_bank", "1"},
            {"verbose", "false"}
        });
        
        SegfoldSimulator sim(A, B);
        sim.run();
        
        results.push_back({
            "No Dynamic Route",
            sim.stats.cycle,
            sim.stats.spad_load_hits,
            sim.stats.spad_stores,
            sim.stats.avg_pes_waiting_spad
        });
    }
    
    // Config 3: Ablation
    {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_spad", "true"},
            {"enable_dynamic_routing", "true"},
            {"enable_dynamic_scheduling", "true"},
            {"enable_memory_hierarchy", "false"},
            {"ablat_dynmap", "true"},
            {"spad_load_ports_per_bank", "4"},
            {"spad_store_ports_per_bank", "4"},
            {"verbose", "false"}
        });
        
        SegfoldSimulator sim(A, B);
        sim.run();
        
        results.push_back({
            "Ablation (4 ports)",
            sim.stats.cycle,
            sim.stats.spad_load_hits,
            sim.stats.spad_stores,
            sim.stats.avg_pes_waiting_spad
        });
    }
    
    // Print comparison table
    std::cout << "\n  Results Comparison (8x8 dense matrix, 4x4 PEs):" << std::endl;
    std::cout << "  " << std::string(90, '-') << std::endl;
    std::cout << "  Configuration        | Cycles | SPAD Loads | SPAD Stores | Avg PEs Waiting" << std::endl;
    std::cout << "  " << std::string(90, '-') << std::endl;
    
    for (const auto& r : results) {
        printf("  %-20s | %6d | %10d | %11d | %15.2f\n",
               r.name.c_str(), r.cycles, r.spad_loads, r.spad_stores, r.avg_pes_waiting);
    }
    std::cout << "  " << std::string(90, '-') << std::endl;
    
    // Verify expected behaviors
    TEST_ASSERT(results[2].spad_loads == 0, 
                "Ablation config has 0 SPAD loads (ablat_dynmap works)");
    TEST_ASSERT(results[2].spad_stores > results[0].spad_stores,
                "Ablation config has more SPAD stores");
    
    // Save results to tmp directory
    std::string tmp_dir = get_tmp_dir();
    std::string result_file = tmp_dir + "/ablation_comparison.txt";
    std::ofstream out(result_file);
    
    out << "Ablation Study Comparison\n";
    out << "=========================\n\n";
    out << "Configuration        | Cycles | SPAD Loads | SPAD Stores | Avg PEs Waiting\n";
    out << std::string(90, '-') << "\n";
    
    for (const auto& r : results) {
        char line[256];
        snprintf(line, sizeof(line), "%-20s | %6d | %10d | %11d | %15.2f\n",
                r.name.c_str(), r.cycles, r.spad_loads, r.spad_stores, r.avg_pes_waiting);
        out << line;
    }
    
    out.close();
    std::cout << "\n  Comparison saved to: " << result_file << std::endl;
}

int main() {
    SETUP_SIGNALS();
    
    try {
        test_ablat_dynmap_disabled();
        test_ablat_dynmap_enabled();
        test_ablat_dynmap_with_multiport();
        test_comparison_all_configs();
        
        std::cout << "\n✅ All ablation tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
