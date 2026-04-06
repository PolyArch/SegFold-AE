#include <iostream>
#include <chrono>
#include <iomanip>
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "test_utils.hpp"

using namespace csegfold;

class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    std::string name;
    
public:
    Timer(const std::string& n) : name(n) {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    ~Timer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << std::setw(40) << std::left << name << ": " 
                  << std::setw(8) << std::right << duration.count() << " ms" << std::endl;
    }
};

int main() {
    SETUP_SIGNALS();
    
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "Performance Profiling Test\n";
    std::cout << "Matrix: 256x256, dA=0.1, dB=0.1\n";
    std::cout << "========================================\n\n";
    
    int size = 256;
    double densityA = 0.1;
    double densityB = 0.1;
    int random_state = 42;
    
    // Load config from YAML file
    std::string config_file = test_utils::get_workspace_root() + "/csegfold/tests/test_perf_config.yaml";
    std::cout << "Loading config from: " << config_file << "\n";
    
    try {
        reset();
        load_cfg(config_file);
        
        std::cout << "Config loaded successfully\n";
        std::cout << "  Physical PEs: " << config_.physical_pe_row_num << "x" << config_.physical_pe_col_num << "\n";
        std::cout << "  Enable SPAD: " << (config_.enable_spad ? "true" : "false") << "\n";
        std::cout << "  Enable Memory Hierarchy: " << (config_.enable_memory_hierarchy ? "true" : "false") << "\n\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << "\n";
        return 1;
    }
    
    Matrix<int8_t> A, B;
    Matrix<int> C_expected;
    
    // Phase 1: Matrix Generation
    {
        Timer t("1. Matrix Generation");
        MatrixParams params;
        params.M = size;
        params.K = size;
        params.N = size;
        params.density_a = densityA;
        params.density_b = densityB;
        params.random_state = random_state;
        
        auto result = gen_uniform_matrix(params);
        A = std::get<0>(result);
        B = std::get<1>(result);
        C_expected = std::get<2>(result);
    }
    
    std::cout << "  A: " << A.rows() << "x" << A.cols() << " (nnz=" << A.nnz() << ")\n";
    std::cout << "  B: " << B.rows() << "x" << B.cols() << " (nnz=" << B.nnz() << ")\n";
    std::cout << "  C: " << C_expected.rows() << "x" << C_expected.cols() << " (nnz=" << C_expected.nnz() << ")\n\n";
    
    SegfoldSimulator* sim = nullptr;
    
    // Phase 2: Simulator Construction (includes matrix loading, CSR conversion, tiling)
    {
        Timer t("2. Simulator Construction + Preprocessing");
        sim = new SegfoldSimulator(A, B);
    }
    
    // Phase 3: Simulation Execution
    {
        Timer t("3. Simulation Execution");
        sim->run();
    }
    
    std::cout << "\nSimulation completed in " << sim->stats.cycle << " cycles\n";
    
    // Access stats directly from simulator
    Stats& stats = sim->stats;
    
    std::cout << "\nStats:\n";
    std::cout << "  Utilization: " << stats.avg_util << "\n";
    std::cout << "  SPAD Load Hits: " << stats.spad_load_hits << "\n";
    std::cout << "  SPAD Stores: " << stats.spad_stores << "\n";
    std::cout << "  B Elements on Switch: " << stats.avg_b_elements_on_switch << "\n";
    std::cout << "  PEs Waiting SPAD: " << stats.avg_pes_waiting_spad << "\n";
    
    // Phase 4: Output Verification
    std::cout << "\n========================================\n";
    std::cout << "Output Verification\n";
    std::cout << "========================================\n\n";
    
    bool output_correct = sim->check_output(C_expected);
    if (output_correct) {
        std::cout << "✓ Output verification PASSED\n";
        std::cout << "  Simulator output matches expected result\n";
    } else {
        std::cout << "✗ Output verification FAILED\n";
        std::cerr << "ERROR: Simulator output does not match expected result!\n";
        delete sim;
        return 1;
    }
    
    // Phase 5: With Trace Enabled (separate run)
    std::cout << "\n========================================\n";
    std::cout << "Testing with Trace Enabled\n";
    std::cout << "========================================\n\n";
    
    // Reload config and enable trace
    reset();
    load_cfg(config_file);
    update_cfg({{"save_trace", "true"}});
    
    SegfoldSimulator* sim_with_trace = nullptr;
    
    {
        Timer t("5. Simulation with Trace");
        sim_with_trace = new SegfoldSimulator(A, B);
        sim_with_trace->run();
    }
    
    Stats& stats_with_trace = sim_with_trace->stats;
    std::cout << "Trace size: " << stats_with_trace.trace_b_elements_on_switch.size() << " cycles\n";
    
    // Phase 6: Stats Serialization
    {
        Timer t("6. Stats Serialization (no trace)");
        std::string stats_json = stats.serialize(false);
        (void)stats_json;
    }
    
    {
        Timer t("7. Stats Serialization (with trace)");
        std::string stats_json = stats_with_trace.serialize(true);
        (void)stats_json;
    }
    
    delete sim;
    delete sim_with_trace;
    
    std::cout << "\n========================================\n";
    std::cout << "Performance Profiling Complete\n";
    std::cout << "========================================\n\n";
    
    return 0;
}
