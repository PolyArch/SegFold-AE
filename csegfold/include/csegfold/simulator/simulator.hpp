#pragma once

#include "csegfold/modules/module.hpp"
#include "csegfold/modules/pe.hpp"
#include "csegfold/modules/switch.hpp"
#include "csegfold/modules/spad.hpp"
#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/modules/memoryController.hpp"
#include "csegfold/modules/lookuptable.hpp"
#include "csegfold/matrix/generator.hpp"
#include <vector>
#include <unordered_map>

namespace csegfold {

class Simulator : public BaseModule {
public:
    Simulator(const Matrix<int8_t>& A, const Matrix<int8_t>& B);
    Simulator(const CSRMatrix& A_csr, const CSRMatrix& B_csr);
    virtual ~Simulator();
    
    MatrixLoader matrix;
    PEModule peModule;
    SwitchModule switchModule;
    SPADModule spadModule;
    std::unique_ptr<LookUpTable> lut;
    MemoryController controller;
    
    bool success = true;
    
    void profile_AB();
    void record_profiling_data();
    void record_metadata_for_profiling_C();
    
    void init_fifo();
    void refresh_states();
    
    void record_utilization();
    void record_b_elements_on_switch();
    void record_pes_waiting_spad();
    void record_pes_fifo_empty_stall();
    void record_pes_fifo_blocked_stall();
    void store_c_to_spad();
    void final_utilization();
    void final_sw_stall_stats();
    void final_b_rows();
    void final_b_elements_on_switch();
    void final_pes_waiting_spad();
    void final_pes_fifo_empty_stall();
    void final_pes_fifo_blocked_stall();

    bool fifo_is_empty() const;
    void pop_fifo_to_pe(int i, int j);
    void bypass_fifo_to_pe(int i, int j, const std::unordered_map<std::string, int>& pe_update);
    bool fifo_not_full(int i, int j) const;
    
    bool is_done() const;
    bool store_is_done() const;
    
    bool check_output(const Matrix<int8_t>& cpu_output) const;
    bool check_output(const Matrix<int>& cpu_output) const;
    
    void log_cycle();
    void dump_trace(const std::string& filename = "trace.json");
    void dump_config(const std::string& filename = "config.json");
    void dump_stats(const std::string& filename = "stats.json", bool include_traces = false);
    void dump_state(const std::string& file_path, const std::string& format = "json", bool include_traces = false);
    std::string serialize_state(const std::string& format = "json", bool include_traces = false) const;
    void run_check();
    
    SparseAccumulator acc_output;
    std::unordered_map<std::string, std::string> snapshot;
    std::vector<std::unordered_map<std::string, std::string>> trace;
    
    int a_row_idx_w = 0;
    int a_col_idx_w = 0;
    int b_row_idx_w = 0;
    int b_col_idx_w = 0;

    int current_cycle_b_loads = 0;

    struct FIFOEntry {
        std::vector<std::unordered_map<std::string, int>> pe_update;
        int lptr = 0;  // read pointer (left)
        int rptr = 0;  // write pointer (right)
    };
    std::vector<std::vector<FIFOEntry>> fifo;
    
    std::pair<int, int> metadata_for_profiling_C() const;
    
    // Helper functions for PE operations
    static void load_a_from_fifo_to_pe(int i, int j, MemoryController* controller, PEModule* peModule, 
                                       const std::pair<int, int>& orig_idx_a, 
                                       const std::unordered_map<std::string, int>& b);
    static std::pair<bool, bool> is_new_c(PEModule* peModule, int i, int j, MatrixLoader* matrix);
};

} // namespace csegfold

