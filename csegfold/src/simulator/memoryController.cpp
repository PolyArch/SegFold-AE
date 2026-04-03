#include "csegfold/simulator/memoryController.hpp"
#include "csegfold/simulator/simulator.hpp"
#include "csegfold/modules/memoryController.hpp"
#include "csegfold/modules/switch.hpp"
#include "csegfold/modules/pe.hpp"
#include <cassert>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <vector>

namespace csegfold {

// Reason codes for b_row_is_valid failure
enum class BRowSkipReason { NONE = 0, EVICTION = 1, B_LOADED = 2, NO_CAPACITY = 3 };

// Helper function: b_row_is_valid
static std::tuple<bool, int, int, BRowSkipReason> b_row_is_valid(MemoryController* controller, SwitchModule* switchModule, int r) {
    int c_start = (controller->cfg.enable_partial_b_load) ? controller->b_loader_offset[r] : 0;
    int c_end = controller->b_loader_limit[r];

    for (int i = 0; i < controller->prows(); ++i) {
        if (!controller->matrix->intersect_bc(r, i)) {
            continue;
        }
        if (controller->cfg.enable_tile_eviction && controller->ready_to_evict[i]) {
            if (controller->cfg.enable_tile_pipeline) {
                // Allow B rows from the PE row's current tile even during eviction
                int b_tile = r / controller->matrix->K;
                if (b_tile != controller->pe_row_tile_id[i]) {
                    return {false, 0, 0, BRowSkipReason::EVICTION};
                }
            } else {
                return {false, 0, 0, BRowSkipReason::EVICTION};
            }
        }
        if (switchModule->b_loaded[i] && controller->cfg.disable_multi_b_row_per_row) {
            return {false, 0, 0, BRowSkipReason::B_LOADED};
        }
        int start_j = switchModule->get_start_offset(controller, r, c_start, i);
        assert(start_j < switchModule->mapper.get_row_length(i));
        int load_length = switchModule->get_max_load(i, start_j);
        if (load_length < c_end - c_start) {
            if (controller->cfg.enable_partial_b_load) {
                c_end = std::min(c_end, load_length + c_start);
            } else {
                return {false, 0, 0, BRowSkipReason::NO_CAPACITY};
            }
        }
    }
    return {c_start != c_end, c_start, c_end, BRowSkipReason::NONE};
}

// Helper function: update_switch_with_b_data
static void update_switch_with_b_data(SwitchState& next_switch, int r, int c, int v, MemoryController* controller) {
    auto [orig_row, orig_col] = controller->matrix->B_indexed.get_original_coords(r, c);
    assert(orig_row >= 0 && orig_col >= 0 && 
           "B_indexed.get_original_coords failed - element not found in tiled matrix");
    
    next_switch.b.row = r;
    next_switch.b.col = c;
    next_switch.b.val = v;
    next_switch.b.n = orig_col;  // Store global output column index
    next_switch.status = SwitchStatus::LOAD_B;
    next_switch.loadB_cycle = controller->cycle();
}

// Load a single B element to switch or FIFO, returns true if successful
static bool load_b_element_to_switch(MemoryController* controller, SwitchModule* switchModule,
                                     int r, int c, int v, int i, int j) {
    SwitchState& next_switch = switchModule->get_next_sw(i, j);

    bool fifo_enabled = controller->cfg.enable_b_loader_fifo &&
                       controller->cfg.b_loader_fifo_size > 0;
    bool fifo_empty = !fifo_enabled || switchModule->b_loader_fifo_empty(i, j);

    // Get original coordinates for memory request (needed for both paths)
    auto [orig_row, orig_col] = controller->matrix->B_indexed.get_original_coords(r, c);

    if (fifo_empty && next_switch.status == SwitchStatus::IDLE) {
        // Direct path to switch
        update_switch_with_b_data(next_switch, r, c, v, controller);
        controller->stats.b_direct_loads++;
        switchModule->mark_b_position_loaded(i, j);

        // Submit memory request for direct path
        if (switchModule->cfg.enable_memory_hierarchy &&
            next_switch.status == SwitchStatus::LOAD_B) {
            int c_col = next_switch.b.col.value();
            controller->submit_load_request(orig_row, orig_col, "B", i, j, "switch", c_col);
        }
    } else if (fifo_enabled) {
        if (!switchModule->b_loader_fifo_full(i, j)) {
            // FIFO path - enqueue and submit memory request targeting FIFO entry
            int fifo_idx = switchModule->enqueue_b_loader_fifo(i, j, r, c, v, orig_col, orig_row, controller->cycle());
            controller->stats.b_fifo_enqueues++;
            switchModule->mark_b_position_loaded(i, j);

            // Submit memory request for FIFO path (fires at enqueue time)
            if (switchModule->cfg.enable_memory_hierarchy) {
                controller->submit_load_request(orig_row, orig_col, "B", i, j, "b_loader_fifo", orig_col, fifo_idx);
            }
        } else {
            return false;  // FIFO full - stall
        }
    } else {
        assert(next_switch.status == SwitchStatus::IDLE &&
               "Switch must be idle when FIFO is disabled");
    }

    return true;
}

// Load B elements for a single PE row
static std::tuple<bool, int> load_b_row_for_pe_row(MemoryController* controller, SwitchModule* switchModule,
                                                    int r, int c_start, int c_end, int i) {
    if (!controller->matrix->intersect_bc(r, i)) {
        return {true, 0};
    }

    if (controller->cfg.disable_multi_b_row_per_row) {
        switchModule->b_loaded[i] = true;
    }

    int start_j = switchModule->get_start_offset(controller, r, c_start, i);
    assert(start_j < switchModule->mapper.get_row_length(i));
    int load_length = switchModule->get_max_load(i, start_j);
    assert(load_length >= c_end - c_start);

    int issued_b_loads = 0;
    for (int _j = 0; _j < c_end - c_start; ++_j) {
        int b_col = c_start + _j;
        int j = start_j + _j;

        assert(controller->B_csr.find(r) != controller->B_csr.end());
        assert(b_col < static_cast<int>(controller->B_csr.at(r).size()));
        int c = controller->B_csr.at(r)[b_col].first;
        int v = controller->B_csr.at(r)[b_col].second;

        if (!load_b_element_to_switch(controller, switchModule, r, c, v, i, j)) {
            return {false, issued_b_loads};  // FIFO full - stall
        }
        issued_b_loads++;
    }

    if (issued_b_loads > 0) {
        int actual_b_elements = c_end - c_start;
        bool row_complete_for_i = (c_end >= controller->b_loader_limit[r]);
        switchModule->record_row_load(i, r, start_j, load_length, actual_b_elements, row_complete_for_i);
    }
    return {true, issued_b_loads};
}

// Helper function: load_b_row
static std::tuple<bool, int, int> load_b_row(MemoryController* controller, SwitchModule* switchModule, int r, int c_start, int c_end) {
    int issued_a_loads = 0;
    int issued_b_loads = 0;

    for (int i = 0; i < controller->prows(); ++i) {
        auto [success, row_b_loads] = load_b_row_for_pe_row(controller, switchModule, r, c_start, c_end, i);
        if (!success) {
            return {false, issued_b_loads, issued_a_loads};
        }
        if (row_b_loads > 0) {
            issued_b_loads += row_b_loads;
            issued_a_loads++;
        }
    }

    bool row_complete;
    if (!controller->cfg.enable_partial_b_load) {
        row_complete = true;
    } else {
        controller->b_loader_offset[r] = c_end;
        row_complete = c_end >= controller->b_loader_limit[r];
    }
    return {row_complete, issued_b_loads, issued_a_loads};
}

// Record B row loading statistics
static void record_b_row_stats(Simulator* simulator, const std::vector<int>& valid_b_rows, int issued_b_loads) {
    int num_valid_b_rows = static_cast<int>(valid_b_rows.size());
    double b_rows_diff = 0.0;
    if (num_valid_b_rows > 1) {
        std::vector<int> diffs;
        for (size_t i = 1; i < valid_b_rows.size(); ++i) {
            diffs.push_back(valid_b_rows[i] - valid_b_rows[i-1]);
        }
        b_rows_diff = std::accumulate(diffs.begin(), diffs.end(), 0.0) / static_cast<double>(diffs.size());
    }
    if (simulator->cfg.save_stats_trace) {
        simulator->stats.trace_b_rows.push_back(static_cast<double>(num_valid_b_rows));
        simulator->stats.trace_b_rows.push_back(b_rows_diff);
    }
    simulator->current_cycle_b_loads = issued_b_loads;
}

// Process a single B row for loading
static bool process_b_row(Simulator* simulator, MemoryController* controller, SwitchModule* switchModule,
                          int r, bool should_debug, int& issued_b_loads,
                          std::unordered_set<int>& completed_rows, std::vector<int>& valid_b_rows) {
    if (controller->cfg.enable_partial_b_load) {
        if (controller->b_loader_offset[r] >= controller->b_loader_limit[r]) {
            completed_rows.insert(r);
            return true;  // Skip but continue processing
        }
    }

    auto [load_valid, c_start, c_end, skip_reason] = b_row_is_valid(controller, switchModule, r);
    if (should_debug && controller->debug()) {
        std::ostringstream oss;
        oss << "[B_loader] Cycle " << controller->cycle()
            << " Row " << r << " valid=" << load_valid
            << " c_start=" << c_start << " c_end=" << c_end;
        controller->log->debug(oss.str());
    }

    if (!load_valid) {
        // Track skip reason
        switch (skip_reason) {
            case BRowSkipReason::EVICTION:    simulator->stats.b_row_skip_eviction++; break;
            case BRowSkipReason::B_LOADED:    simulator->stats.b_row_skip_b_loaded++; break;
            case BRowSkipReason::NO_CAPACITY: simulator->stats.b_row_skip_no_capacity++; break;
            default: break;
        }
        return simulator->cfg.enable_b_row_reordering;  // Continue if reordering enabled
    }

    if (controller->debug()) {
        std::ostringstream oss;
        oss << "[B_loader] Cycle " << controller->cycle()
            << " About to load B row " << r << " c_start=" << c_start << " c_end=" << c_end;
        controller->log->debug(oss.str());
    }

    auto [row_complete, row_b_loads, row_a_loads] = load_b_row(controller, switchModule, r, c_start, c_end);

    if (controller->debug()) {
        std::ostringstream oss;
        oss << "[B_loader] Cycle " << controller->cycle()
            << " Loaded B row " << r << " row_complete=" << row_complete
            << " issued_b_loads=" << row_b_loads << " issued_a_loads=" << row_a_loads;
        controller->log->debug(oss.str());
    }

    issued_b_loads += row_b_loads;
    if (row_complete) {
        completed_rows.insert(r);
    }
    if (row_b_loads > 0) {
        simulator->stats.b_row_reads++;
    }
    simulator->stats.a_reads += row_a_loads;
    simulator->stats.b_reads += row_b_loads;
    valid_b_rows.push_back(r);
    return true;
}

void run_b_loader(Simulator* simulator, MemoryController* controller, SwitchModule* switchModule) {
    switchModule->clear_row_load_info();

    int freq = controller->cfg.debug_log_frequency;
    bool should_debug = freq > 0 && (controller->cycle() % freq == 0);

    if (!controller->ready(switchModule)) {
        if (should_debug && controller->debug()) {
            std::ostringstream oss;
            oss << "[B_loader] Cycle " << controller->cycle()
                << " Not ready (cnt=" << controller->cnt << ", II=" << controller->cfg.II << ")";
            controller->log->debug(oss.str());
        }
        return;
    }

    if (should_debug && controller->debug()) {
        std::ostringstream oss;
        oss << "[B_loader] Cycle " << controller->cycle()
            << " Active indices: " << controller->active_indices.size()
            << " B_rows_to_load size: " << controller->B_rows_to_load.size()
            << " lptr: " << controller->lptr;
        controller->log->debug(oss.str());
        if (!controller->active_indices.empty()) {
            int r = controller->active_indices[0];
            std::ostringstream oss2;
            oss2 << "[B_loader] Debug row " << r << ": "
                 << "b_loader_limit=" << controller->b_loader_limit[r]
                 << " b_loader_offset=" << controller->b_loader_offset[r]
                 << " B_csr size=" << (controller->B_csr.find(r) != controller->B_csr.end() ? controller->B_csr.at(r).size() : 0);
            controller->log->debug(oss2.str());
        }
    }

    int issued_b_loads = 0;
    std::unordered_set<int> completed_rows;
    std::vector<int> valid_b_rows;
    int loaded_b_rows = 0;

    for (int r : controller->active_indices) {
        if (loaded_b_rows >= controller->cfg.b_loader_row_limit) {
            break;
        }

        bool should_continue = process_b_row(simulator, controller, switchModule, r, should_debug,
                                             issued_b_loads, completed_rows, valid_b_rows);
        if (!should_continue) {
            break;
        }

        if (!valid_b_rows.empty() && valid_b_rows.back() == r) {
            loaded_b_rows++;
            if (!simulator->cfg.enable_multi_b_row_loading) {
                break;
            }
        }
    }

    // Track per-cycle B loader stats
    simulator->stats.sum_b_rows_loaded_per_cycle += loaded_b_rows;
    simulator->stats.sum_active_indices_per_cycle += static_cast<int>(controller->active_indices.size());

    record_b_row_stats(simulator, valid_b_rows, issued_b_loads);
    controller->remove_completed_rows(completed_rows);
    if (switchModule->cfg.disable_multi_b_row_per_row) {
        switchModule->clear_loading_rows();
    }
}

// Process FIFO memory responses only - call at beginning of cycle before drain_b_loader_fifos
void process_fifo_memory_responses(MemoryController* controller, SwitchModule* switchModule) {
    if (!controller->enable_memory_hierarchy) {
        return;
    }

    // Tick the memory backend and collect completed responses
    controller->tick_memory_backend();
    auto responses = controller->get_completed_responses();

    // Store non-FIFO responses for later processing
    std::vector<MemoryResponse> deferred_responses;

    // Process B matrix responses immediately (both FIFO and direct path), defer others
    for (const auto& resp : responses) {
        if (resp.matrix == MatrixType::B) {
            int i = resp.pe_row;
            int j = resp.pe_col;
            if (i >= 0 && i < switchModule->vrows() &&
                j >= 0 && j < switchModule->vcols()) {
                if (resp.dest == "b_loader_fifo" && resp.fifo_idx >= 0) {
                    // FIFO path
                    // Check if this entry was already dequeued to switch (memory wasn't ready at dequeue time)
                    if (switchModule->check_and_remove_pending_fifo_to_switch(i, j, resp.fifo_idx)) {
                        // Entry was dequeued to switch before memory was ready
                        // Mark the switch as memory ready - update_switchModule_next_switches will handle the transition
                        switchModule->mark_switch_memory_ready(i, j);
                    } else {
                        // Entry is still in FIFO, mark it as memory_ready
                        switchModule->mark_fifo_entry_ready(i, j, resp.fifo_idx);
                    }
                } else if (resp.dest == "switch") {
                    // Direct path - mark switch as memory ready
                    switchModule->mark_switch_memory_ready(i, j);
                }
            }
        } else {
            deferred_responses.push_back(resp);
        }
    }

    // Store deferred responses back for run_memory_interface to process
    controller->set_deferred_responses(std::move(deferred_responses));
}

void run_memory_interface(MemoryController* controller, SwitchModule* switchModule, PEModule* peModule) {
    if (!controller->enable_memory_hierarchy) {
        return;
    }

    // Get deferred responses (or all responses if process_fifo_memory_responses wasn't called)
    auto responses = controller->get_deferred_responses();

    // Process each completed memory response
    for (const auto& resp : responses) {
        int i = resp.pe_row;
        int j = resp.pe_col;

        // B matrix responses are handled in process_fifo_memory_responses, not here
        if (resp.matrix == MatrixType::B) {
            continue;
        } else if (resp.matrix == MatrixType::A && resp.dest == "pe") {
            // A load completed - set valid_a flag for PE
            if (i >= 0 && i < peModule->prows() &&
                j >= 0 && j < peModule->pcols()) {
                peModule->valid_a[i][j] = true;
            }
        }
        // C loads/stores and FIFO responses (already processed) don't require action here
    }
}

} // namespace csegfold

