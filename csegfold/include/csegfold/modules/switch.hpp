#pragma once

#include "csegfold/modules/module.hpp"
#include "csegfold/modules/mapper.hpp"
#include <vector>
#include <unordered_map>
#include <optional>

namespace csegfold {

enum class SwitchStatus {
    IDLE,
    LOAD_B,
    MOVE
};

// B Loader FIFO entry - stores B element data waiting to be loaded to switch
struct BLoaderFIFOEntry {
    int b_row;       // B matrix row (tiled)
    int b_col;       // B matrix col (tiled)
    int b_val;       // B element value
    int b_n;         // Global output column (orig_col)
    int orig_row;    // Original row for memory address calculation
    int loadB_cycle; // Cycle when queued
    int fifo_idx;    // Index in FIFO for response matching
    bool memory_ready = false;  // True when memory response has arrived
};

// B Loader FIFO - one per switch position
struct BLoaderFIFO {
    std::vector<BLoaderFIFOEntry> entries;
    int lptr = 0;  // read pointer
    int rptr = 0;  // write pointer

    bool is_empty() const { return rptr <= lptr; }
    bool is_full(int max_size) const { return (rptr - lptr) >= max_size; }
    int size() const { return rptr - lptr; }
};

// Info about B row loading for idle switch analysis
struct BRowLoadInfo {
    int b_row;                // B row index
    int start_j;              // Starting switch position
    int potential_load_space; // Consecutive IDLE/non-full-FIFO positions from start_j
    int actual_b_elements;    // Number of B elements actually loaded (may be less than potential)
    bool row_complete;        // True if entire B row was loaded, false if truncated
};

struct SwitchState {
    SwitchStatus status = SwitchStatus::IDLE;
    struct {
        std::optional<int> val;
        std::optional<int> row;
        std::optional<int> col;
        std::optional<int> n;  // Global output column index (from col_idx_b)
    } b;
    std::optional<int> loadB_cycle;
    std::optional<int> c_col;
    bool memory_ready = false;  // True when memory response has arrived (for LOAD_B state)

    void update(const SwitchState& other) {
        *this = other;
    }
};

bool sw_active(const SwitchState& sw);
bool sw_idle(const SwitchState& sw);
bool sw_has_c_col(const SwitchState& sw);
bool sw_load_done(const SwitchState& sw, int cycle);

class SwitchModule : public BaseModule {
public:
    SwitchModule();
    
    void reset_next();
    void reset_c_upd();
    void record_c_upd(int i);
    bool has_remaining_c_upd() const;
    void handle_c_upd();
    
    void init_static_c_col(class MatrixLoader* matrix);
    
    bool is_last_col(int i, int j) const;
    bool next_sw_has_c_col(int i, int j) const;
    bool next_sw_row_full(int i) const;
    
    int map_col_to_idx(int i, int b_col_dense) const;
    int map_csr_to_idx(int i, int b_col_csr) const;
    
    int num_switches() const;
    int num_active_switches() const;
    
    static SwitchState idle_sw();
    std::vector<std::unordered_map<std::string, int>> b_positions(int cycle = 0) const;
    
    static bool c_eq_b(const SwitchState& sw);
    static bool c_lt_b(const SwitchState& sw);
    static bool c_gt_b(const SwitchState& sw);
    static bool b_zero(const SwitchState& sw);
    
    bool next_sw_idle(int i, int j);
    void free_next_sw(int i, int j);
    void free_next_sw_b_val(int i, int j);
    void evict_b_rows(int i);
    
    SwitchState& get_sw(int i, int j);
    SwitchState& get_next_sw(int i, int j);
    
    void update_sw_c_col(int i, int j);
    void update_next_sw_c_col(int i, int j);
    
    int count_idle_sw(int i, int start_col, int end_col);
    int first_idle_sw(int i, int start_col, int end_col);
    void clear_loading_rows();
    int get_num_idle_sw(int i, int j = 0);
    int last_active_sw(int i);
    
    int get_start_offset(class MemoryController* controller, int r, int c_start, int i);
    int get_max_load(int i, int start_j);
    int get_available_capacity(int i, int start_j);  // Counts positions that can accept B (IDLE or FIFO available)
    
    bool has_lut() const;
    
    std::vector<std::vector<SwitchState>> switches;
    std::vector<std::vector<SwitchState>> next_switches;
    Mapper mapper;
    class LookUpTable* lut = nullptr;
    std::vector<bool> b_loaded;
    std::vector<bool> row_full;
    std::vector<int> c_upd_cnt;

    // B Loader FIFO - one per switch position
    std::vector<std::vector<BLoaderFIFO>> b_loader_fifo;

    // Track which positions received B element this cycle (input channel limit)
    std::vector<std::vector<bool>> b_position_loaded_this_cycle;

    // B Loader FIFO operations
    void init_b_loader_fifo();
    bool b_loader_fifo_empty(int i, int j) const;
    bool b_loader_fifo_full(int i, int j) const;
    // Returns the fifo_idx of the enqueued entry (for memory request tracking)
    int enqueue_b_loader_fifo(int i, int j, int b_row, int b_col, int b_val, int b_n, int orig_row, int cycle);
    bool dequeue_to_switch(int i, int j);  // Returns true if element was dequeued
    bool all_b_loader_fifos_empty() const;
    // Mark a FIFO entry as memory_ready (called when memory response arrives)
    void mark_fifo_entry_ready(int i, int j, int fifo_idx);

    // Input channel tracking (1 B element per position per cycle)
    void reset_b_position_loaded();
    bool is_b_position_loaded(int i, int j) const;
    void mark_b_position_loaded(int i, int j);

    // Per-row loading info for current cycle (populated by run_b_loader)
    std::vector<std::vector<BRowLoadInfo>> row_load_info;

    void clear_row_load_info();
    void record_row_load(int i, int b_row, int start_j, int potential_load_space, int actual_b_elements, bool row_complete);

    // Complete a B load from memory - transitions switch from LOAD_B to MOVE
    void complete_b_load(int i, int j);

    // Mark a switch as memory ready (for LOAD_B state waiting for memory response)
    void mark_switch_memory_ready(int i, int j);

    // Track FIFO entries that were dequeued before memory was ready
    // Maps (i, j, fifo_idx) -> true if this entry was dequeued to switch with memory_ready=false
    // When memory response arrives for this entry, we should complete the switch's load instead
    struct PendingFifoToSwitch {
        int i;
        int j;
        int fifo_idx;
    };
    std::vector<PendingFifoToSwitch> pending_fifo_to_switch_;

    // Record that a FIFO entry was dequeued to switch before memory was ready
    void record_pending_fifo_to_switch(int i, int j, int fifo_idx);

    // Check if a FIFO entry was dequeued before memory was ready
    // If so, removes from pending list and returns true (caller should complete the switch load)
    bool check_and_remove_pending_fifo_to_switch(int i, int j, int fifo_idx);
};

} // namespace csegfold

