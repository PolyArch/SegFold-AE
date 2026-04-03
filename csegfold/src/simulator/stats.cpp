#include "csegfold/simulator/simulator.hpp"
#include "csegfold/simulator/switch.hpp"
#include "csegfold/modules/pe.hpp"
#include "csegfold/modules/spad.hpp"
#include <algorithm>
#include <sstream>

namespace csegfold {

// =============================================================================
// Simulator member functions for statistics recording and finalization
// =============================================================================

void Simulator::record_utilization() {
    double util = static_cast<double>(peModule.num_active_pes()) / static_cast<double>(peModule.num_pes());
    stats.avg_util += util;
    stats.max_util = std::max(stats.max_util, util);
}

void Simulator::record_b_elements_on_switch() {
    // Track the number of B elements loaded onto the switch this cycle
    // This value is set by run_b_loader() based on issued_b_loads
    if (cfg.save_stats_trace) {
        stats.trace_b_elements_on_switch.push_back(current_cycle_b_loads);
    }
    // Always update running sum for average computation
    stats.avg_b_elements_on_switch += current_cycle_b_loads;
    stats.b_loads_per_cycle_hist[current_cycle_b_loads]++;
}

void Simulator::record_pes_waiting_spad() {
    // Count PEs that are waiting for SPAD read/write
    int count = 0;
    for (int i = 0; i < vrows(); ++i) {
        int row_len = switchModule.mapper.get_effective_row_length(i);
        for (int j = 0; j < row_len; ++j) {
            const auto& next_pe = peModule.next_pe[i][j];
            // A PE is waiting for SPAD if it's in LOAD status and needs to store/load C
            if (next_pe.status == PEStatus::LOAD) {
                bool store_c = peModule.store_c[i][j];
                bool load_c = peModule.load_c[i][j];
                // If PE needs to store or load from SPAD
                if ((store_c && !spadModule.is_valid_store(i)) ||
                    (load_c && !spadModule.is_valid_load(i))) {
                    count++;
                }
            }
        }
    }
    if (cfg.save_stats_trace) {
        stats.trace_pes_waiting_spad.push_back(count);
    }
    // Always update running sum for average computation
    stats.avg_pes_waiting_spad += count;
}

void Simulator::record_pes_fifo_empty_stall() {
    int count = 0;
    for (int i = 0; i < vrows(); ++i) {
        int row_len = switchModule.mapper.get_effective_row_length(i);
        for (int j = 0; j < row_len; ++j) {
            const auto& next_pe = peModule.next_pe[i][j];
            if (next_pe.status == PEStatus::IDLE) {
                if (!cfg.enable_sw_pe_fifo || fifo[i][j].rptr <= fifo[i][j].lptr) {
                    count++;
                }
            }
        }
    }
    stats.sum_pes_fifo_empty_stall += count;  // Running sum
    if (cfg.save_trace) {
        stats.trace_pes_fifo_empty_stall.push_back(count);
    }
}

void Simulator::record_pes_fifo_blocked_stall() {
    int count = 0;
    if (cfg.enable_sw_pe_fifo) {
        for (int i = 0; i < vrows(); ++i) {
            int row_len = switchModule.mapper.get_effective_row_length(i);
            for (int j = 0; j < row_len; ++j) {
                const auto& next_pe = peModule.next_pe[i][j];
                if (next_pe.status == PEStatus::IDLE && fifo[i][j].rptr > fifo[i][j].lptr) {
                    count++;
                }
            }
        }
    }
    stats.sum_pes_fifo_blocked_stall += count;  // Running sum
    if (cfg.save_trace) {
        stats.trace_pes_fifo_blocked_stall.push_back(count);
    }
}

void Simulator::final_utilization() {
    stats.avg_util = cycle() > 0 ? stats.avg_util / cycle() : 0;
}

void Simulator::final_sw_stall_stats() {
    if (cycle() > 0) {
        stats.avg_sw_move_stall_by_fifo = static_cast<double>(stats.sw_move_stall_by_fifo) / cycle();
        stats.avg_sw_move_stall_by_network = static_cast<double>(stats.sw_move_stall_by_network) / cycle();
        stats.avg_sw_idle_not_in_range = static_cast<double>(stats.sw_idle_not_in_range) / cycle();
        stats.avg_sw_idle_no_b_element = static_cast<double>(stats.sw_idle_no_b_element) / cycle();
        stats.avg_sw_idle_b_row_conflict = static_cast<double>(stats.sw_idle_b_row_conflict) / cycle();
    }
}

void Simulator::final_b_rows() {
    if (stats.macs > 0 && !stats.trace_b_rows.empty()) {
        double sum_rows = 0;
        for (double val : stats.trace_b_rows) {
            sum_rows += val;  // Simplified - full implementation would handle pairs
        }
        stats.avg_b_rows = sum_rows / stats.macs;
        stats.avg_b_diff = 0;  // Simplified
    } else {
        stats.avg_b_rows = 0;
        stats.avg_b_diff = 0;
    }
}

void Simulator::final_b_elements_on_switch() {
    if (cycle() > 0) {
        if (!stats.trace_b_elements_on_switch.empty()) {
            // Recompute from trace (overwrites running sum)
            double sum = 0;
            stats.b_loads_per_cycle_hist.clear();
            for (int val : stats.trace_b_elements_on_switch) {
                sum += val;
                stats.b_loads_per_cycle_hist[val]++;
            }
            stats.avg_b_elements_on_switch = sum / cycle();
        } else {
            // Use running sum accumulated in record_b_elements_on_switch
            stats.avg_b_elements_on_switch = stats.avg_b_elements_on_switch / cycle();
        }
    } else {
        stats.avg_b_elements_on_switch = 0;
    }
}

void Simulator::final_pes_waiting_spad() {
    if (cycle() > 0) {
        if (!stats.trace_pes_waiting_spad.empty()) {
            // Recompute from trace (overwrites running sum)
            double sum = 0;
            for (int val : stats.trace_pes_waiting_spad) {
                sum += val;
            }
            stats.avg_pes_waiting_spad = sum / cycle();
        } else {
            // Use running sum accumulated in record_pes_waiting_spad
            stats.avg_pes_waiting_spad = stats.avg_pes_waiting_spad / cycle();
        }
    } else {
        stats.avg_pes_waiting_spad = 0;
    }
}

void Simulator::final_pes_fifo_empty_stall() {
    if (cycle() > 0) {
        stats.avg_pes_fifo_empty_stall = static_cast<double>(stats.sum_pes_fifo_empty_stall) / cycle();
    } else {
        stats.avg_pes_fifo_empty_stall = 0;
    }
}

void Simulator::final_pes_fifo_blocked_stall() {
    if (cycle() > 0) {
        stats.avg_pes_fifo_blocked_stall = static_cast<double>(stats.sum_pes_fifo_blocked_stall) / cycle();
    } else {
        stats.avg_pes_fifo_blocked_stall = 0;
    }
}

// =============================================================================
// Free function for switch idle analysis
// =============================================================================

void analyze_idle_switches(Simulator* simulator, SwitchModule* switchModule) {
    // Skip analysis if all B loading is finished
    if (simulator->controller.get_is_done()) {
        return;
    }
    if (switchModule->row_load_info.empty()) {
        return;
    }

    // Track B row load length stats and PE row loading count (once per cycle)
    int pe_rows_with_load = 0;
    for (int i = 0; i < switchModule->vrows(); ++i) {
        // Count total B elements loaded on this PE row this cycle
        int pe_row_b_count = 0;
        if (!switchModule->row_load_info[i].empty()) {
            pe_rows_with_load++;
        }
        for (const auto& info : switchModule->row_load_info[i]) {
            simulator->stats.b_row_load_count++;
            simulator->stats.b_row_total_potential += info.potential_load_space;
            simulator->stats.b_row_total_actual += info.actual_b_elements;
            simulator->stats.b_row_length_hist[info.actual_b_elements]++;
            pe_row_b_count += info.actual_b_elements;
            if (info.row_complete && info.actual_b_elements < info.potential_load_space) {
                simulator->stats.b_row_tail_events++;
            }
        }
        simulator->stats.b_loads_per_pe_row_hist[pe_row_b_count]++;
    }
    simulator->stats.sum_pe_rows_with_b_load += pe_rows_with_load;
    simulator->stats.pe_row_load_cycles++;

    for (int i = 0; i < switchModule->vrows(); ++i) {
        const auto& load_info = switchModule->row_load_info[i];
        int row_len = switchModule->mapper.get_effective_row_length(i);
        for (int j = 0; j < row_len; ++j) {
            if (!sw_idle(switchModule->switches[i][j]) ||
                !switchModule->next_sw_idle(i, j)) {
                continue;
            }
            if (load_info.empty()) {
                simulator->stats.sw_idle_no_b_element++;
                simulator->stats.sw_idle_no_b_row++;
                simulator->stats.sw_idle_pe_row_no_b_row++;
                continue;
            }
            bool categorized = false;
            for (const auto& info : load_info) {
                if (j >= info.start_j && j < info.start_j + info.potential_load_space) {
                    if (j >= info.start_j + info.actual_b_elements) {
                        if (info.row_complete) {
                            simulator->stats.sw_idle_no_b_element++;
                            simulator->stats.sw_idle_b_row_tail++;
                            simulator->stats.sw_idle_pe_row_b_short++;
                        } else {
                            simulator->stats.sw_idle_b_row_conflict++;
                        }
                        categorized = true;
                        break;
                    }
                }
            }

            if (!categorized) {
                simulator->stats.sw_idle_not_in_range++;
                simulator->stats.sw_idle_pe_row_not_in_range++;
            }
        }
    }
}

} // namespace csegfold
