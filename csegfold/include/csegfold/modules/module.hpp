#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <cmath>
#include <functional>
#include <typeinfo>
#include <iomanip>

namespace csegfold {

// Forward declarations
class Logger;

// Config struct
// NOTE: When adding new fields, also update:
//   1. Config::to_dict() in module.cpp
//   2. update_cfg() in module.cpp
struct Config {
    // Core settings
    bool is_dense = false;
    int physical_pe_row_num = 4;
    int physical_pe_col_num = 4;
    int virtual_pe_row_num = 4;
    int virtual_pe_col_num = 4;
    
    // Memory
    bool enable_multi_b_row_loading = true;
    bool enable_b_row_reordering = true;
    bool enable_dynamic_routing = true;
    bool enable_partial_b_load = true;
    int b_loader_window_size = 32;
    bool enable_b_v_contention = false;
    bool enable_dynamic_scheduling = true;
    bool enable_tile_eviction = true;
    int c_col_update_per_row = 4;
    int II = 2;  // Initiation Interval: cycles between consecutive B row loads
    int b_loader_row_limit = 16;
    bool enable_offset = false;
    bool disable_multi_b_row_per_row = true;
    
    // PE
    bool enable_sw_pe_fifo = true;
    int sw_pe_fifo_size = 16;
    bool enable_fifo_bypass = true;
    bool decouple_sw_and_pe = false;
    bool decouple_sw_and_controller = false;

    // Timing
    int num_cycles_load_a = 1;
    int num_cycles_load_b = 1;
    int num_cycles_store_c = 0;
    int num_cycles_mult_ii = 1;
    int num_cycles_memory_check = 4;

    // B Loader FIFO
    bool enable_b_loader_fifo = false;
    int b_loader_fifo_size = 16;
    bool enable_spad = true;
    int spad_load_ports_per_bank = 1;  // Number of concurrent loads per SPAD bank per cycle
    int spad_store_ports_per_bank = 1; // Number of concurrent stores per SPAD bank per cycle

    // General
    bool verbose = false;
    bool very_verbose = false;
    bool show_progress = false;
    bool save_trace = false;
    bool run_check = false;
    int max_cycle = 10000000;
    int debug_log_frequency = 100;  // Log debug messages every N cycles (0 = disabled)
    
    // LUT
    bool use_lookup_table = false;
    bool reverse_lookup_table = false;
    int max_updates_per_cycle = 1;
    bool update_on_move = true;
    bool update_with_round_robin = false;

    // Matrix
    bool enable_decompose_a_row = false;
    int num_split = 1;
    bool enable_dynamic_tiling = true;
    bool enable_a_csc = true;

    // Memory hierarchy
    bool enable_memory_hierarchy = true;
    bool use_external_memory = true;
    int memory_server_port = 12345;
    std::string memory_server_host = "127.0.0.1";
    std::string dummy_server_path = "../memory/server";
    int a_pointer_offset = 0x1000;
    int b_pointer_offset = 0x2000;
    int c_pointer_offset = 0x3000;
    bool enable_filter = true;
    bool enable_outstanding_filter = false;
    int cache_line_size = 32;

    // Memory backend
    std::string memory_backend_type = "ideal";  // "ideal" or "ramulator2"
    std::string dram_config_file = "configs/dram/DDR4_8Gb_x8_3200.yaml";

    // L1 Cache
    int l1_size_kb = 64;
    int l1_associativity = 8;
    int l1_line_size = 64;
    int l1_latency = 1;

    // L2 Cache
    int l2_size_kb = 512;
    int l2_associativity = 16;
    int l2_line_size = 64;
    int l2_latency = 10;

    // Ideal backend DRAM latency
    int ideal_dram_latency = 100;

    // Spatial
    bool enable_spatial_folding = true;
    int max_push = 1;
    int mapper_request_limit_per_cycle = 20;

    // Matrix info
    int M = 0;
    int N = 0;
    int K = 0;
    double densityA = 0.1;
    double densityB = 0.1;
    
    int random_state = 0;
    
    // Debug
    bool preprocess_only = false;
    
    // Ablation studies
    bool ablat_dynmap = false;  // If true, always store_c=true, load_c=false in is_new_c

    void reset();
    std::unordered_map<std::string, std::string> to_dict() const;
    std::string serialize() const;
};

// Stats struct
struct Stats {
    // Core metrics
    int a_reads = 0;
    int b_row_reads = 0;
    int b_reads = 0;
    int macs = 0;
    int b_direct_loads = 0;       // B elements loaded directly to switch (bypass FIFO)
    int b_fifo_enqueues = 0;      // B elements enqueued to FIFO
    int b_fifo_memory_ready = 0;  // B elements dequeued with memory_ready=true (latency hidden)
    int b_fifo_memory_wait = 0;   // B elements dequeued with memory_ready=false (still waiting)
    int fifo_bypass_count = 0;    // FIFO bypasses (empty FIFO + idle PE direct write)
    int c_updates = 0;
    
    // LUT (LookUp Table) statistics
    int lut_req = 0;      // Total lookup requests
    int lut_upd = 0;      // Total mapping updates
    int max_lut_req = 0;  // Max requests in a single cycle
    int max_lut_upd = 0;  // Max updates in a single cycle
    int round_robin_upd = 0;
    int lut_hits = 0;
    int lut_miss = 0;
    
    // Performance
    int cycle = 0;
    double avg_util = 0.0;
    double max_util = 0.0;
    double avg_b_rows = 0.0;
    double avg_b_diff = 0.0;
    double avg_b_elements_on_switch = 0.0;
    double avg_pes_waiting_spad = 0.0;
    double avg_pes_fifo_empty_stall = 0.0;
    double avg_pes_fifo_blocked_stall = 0.0;
    int sum_pes_fifo_empty_stall = 0;      // Running sum for on-the-fly average
    int sum_pes_fifo_blocked_stall = 0;    // Running sum for on-the-fly average

    // Data
    std::unordered_map<std::string, std::string> metadata;
    double prof_cost = 0.0;
    bool success = false;
    int folding = 0;
    
    // Case 1: MOVE state stalls (sum counters)
    int sw_move_stall_by_fifo = 0;     // c_eq_b but PE-FIFO full
    int sw_move_stall_by_network = 0;  // c_lt_b but next switch not idle

    // Case 2: IDLE state analysis (sum counters)
    int sw_idle_not_in_range = 0;      // Outside consecutive range from first B element
    int sw_idle_no_b_element = 0;      // In range but no B elements available
    int sw_idle_b_row_conflict = 0;    // In range, B exists, but row conflict

    // Average per cycle (calculated at end)
    double avg_sw_move_stall_by_fifo = 0.0;
    double avg_sw_move_stall_by_network = 0.0;
    double avg_sw_idle_not_in_range = 0.0;
    double avg_sw_idle_no_b_element = 0.0;
    double avg_sw_idle_b_row_conflict = 0.0;

    // Traces
    std::vector<double> trace_b_rows;
    std::vector<int> trace_b_elements_on_switch;
    std::vector<int> trace_pes_waiting_spad;
    std::vector<int> trace_pes_fifo_empty_stall;
    std::vector<int> trace_pes_fifo_blocked_stall;
    std::unordered_map<std::string, double> simulator_runtime_breakdown;
    
    // Transfers
    int ideal_a = 0;
    int ideal_b = 0;
    int ideal_c = 0;
    int tile_a = 0;
    int tile_b = 0;
    int tile_c = 0;
    
    // Memory Backend
    int a_cache_loads = 0;
    int b_cache_loads = 0;
    int c_cache_loads = 0;
    int a_mem_loads = 0;
    int b_mem_loads = 0;
    int c_mem_loads = 0;

    // Memory Backend Statistics
    uint64_t l1_hits = 0;
    uint64_t l1_misses = 0;
    uint64_t l2_hits = 0;
    uint64_t l2_misses = 0;
    uint64_t dram_accesses = 0;
    double avg_memory_latency = 0.0;
    
    // SPAD
    int spad_load_hits = 0;
    int spad_load_misses = 0;
    int spad_stores = 0;
    
    // Matrix
    int a_nnz = 0;
    int b_nnz = 0;
    int c_nnz = 0;
    
    // Network
    int num_push = 0;
    int total_push = 0;

    void reset();
    std::unordered_map<std::string, std::string> to_dict() const;
    std::unordered_map<std::string, std::string> filter() const;
    std::string serialize(bool include_traces = false) const;
};

// Logger class
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
public:
    Logger(const std::string& name, bool verbose = false, bool very_verbose = false);
    
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warning(const std::string& msg);
    void error(const std::string& msg);

private:
    std::string name_;
    LogLevel level_;
    
    void log(LogLevel level, const std::string& msg);
};

// BaseModule class
class BaseModule {
public:
    BaseModule();
    virtual ~BaseModule() = default;
    
    Config& cfg;
    Stats& stats;
    std::unique_ptr<Logger> log;
    
    int vrows() const;
    int vcols() const;
    int prows() const;
    int pcols() const;
    int num_pes() const;
    int cycle() const;
    bool verbose() const;
    bool debug() const;

protected:
    void setup_logger();
};

// Global config and stats instances
extern Config config_;
extern Stats stats_;

// Utility functions
void update_cfg(const std::unordered_map<std::string, std::string>& kwargs);
void load_cfg(const std::string& path);
void reset();
void update_b_window_size(double density);

// Helper functions for iteration
template<typename T>
void iter_mod(const std::vector<std::vector<T>>& m, 
              std::function<void(int, int, const T&)> func, 
              bool rev = false) {
    for (size_t i = 0; i < m.size(); ++i) {
        if (rev) {
            for (int j = static_cast<int>(m[i].size()) - 1; j >= 0; --j) {
                func(i, j, m[i][j]);
            }
        } else {
            for (size_t j = 0; j < m[i].size(); ++j) {
                func(i, j, m[i][j]);
            }
        }
    }
}

template<typename T>
std::vector<std::vector<T>> init_arr(int r, int c, const T& m) {
    return std::vector<std::vector<T>>(r, std::vector<T>(c, m));
}

} // namespace csegfold

