#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/simulator/pe.hpp"
#include "csegfold/simulator/switch.hpp"
#include "csegfold/simulator/memoryController.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace csegfold {

SegfoldSimulator::SegfoldSimulator(const Matrix<int8_t>& A, const Matrix<int8_t>& B)
    : Simulator(A, B) {
}

SegfoldSimulator::SegfoldSimulator(const CSRMatrix& A_csr, const CSRMatrix& B_csr)
    : Simulator(A_csr, B_csr) {
}

void SegfoldSimulator::step() {
    current_cycle_b_loads = 0;
    
    peModule.reset_next();
    switchModule.reset_next();

    // Process memory responses - tick once, then split FIFO vs switch/PE handling
    // FIFO responses are marked ready before drain_b_loader_fifos runs
    // Switch/PE responses are deferred until after all module updates
    if (cfg.enable_memory_hierarchy) {
        process_fifo_memory_responses(&controller, &switchModule);
    }

    run_spad(&spadModule);
    run_pes(this, &peModule);
    run_switches(this, &switchModule, &peModule);
    if (switchModule.has_lut()) {
        run_lookup_tables(lut.get());
    }
    if (cfg.enable_tile_eviction) {
        run_evictions(this, &controller, &switchModule, &peModule);
    }
    switchModule.reset_b_position_loaded();  // Reset input channel tracking for new cycle
    run_b_loader(this, &controller, &switchModule);
    // Analyze idle switches after B loading (row_load_info is now populated)
    analyze_idle_switches(this, &switchModule);
    if (cfg.enable_memory_hierarchy) {
        run_memory_interface(&controller, &switchModule, &peModule);
    }
    record_utilization();
    record_b_elements_on_switch();
    record_pes_waiting_spad();
    record_pes_fifo_empty_stall();
    record_pes_fifo_blocked_stall();
    if (cfg.save_trace) {
        log_cycle();
    }
    refresh_states();
    stats.cycle++;
}

void SegfoldSimulator::cleanup_step() {
    current_cycle_b_loads = 0;
    
    run_spad(&spadModule);
    store_c_to_spad();
    
    record_utilization();
    // record_b_elements_on_switch();
    record_pes_waiting_spad();
    // record_pes_fifo_empty_stall();
    // record_pes_fifo_blocked_stall();

    refresh_states();
    stats.cycle++;
}

void SegfoldSimulator::run() {
    if (cfg.preprocess_only) {
        return;
    }
    
    int last_progress_cycle = 0;
    const int max_stall_cycles = 5000;
    int total_rows = static_cast<int>(controller.B_rows_to_load.size());
    auto last_report = std::chrono::steady_clock::now();
    const auto report_interval = std::chrono::seconds(10);

    while (!is_done() && stats.cycle < cfg.max_cycle) {
        if (stats.cycle - last_progress_cycle > max_stall_cycles) {
            log->warning("Simulation stalled for " + std::to_string(max_stall_cycles) +
                        " cycles at cycle " + std::to_string(stats.cycle));
            success = false;
            break;
        }

        try {
            int prev_completed = controller.n_completed_rows;
            step();
            run_check();
            if (controller.n_completed_rows > prev_completed) {
                last_progress_cycle = stats.cycle;
            }

            // Periodic progress report (every 10s wall-clock)
            auto now = std::chrono::steady_clock::now();
            if (now - last_report >= report_interval) {
                double pct = total_rows > 0 ? 100.0 * controller.n_completed_rows / total_rows : 0;
                std::cerr << "[progress] cycle=" << stats.cycle
                          << "  rows=" << controller.n_completed_rows << "/" << total_rows
                          << "  (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
                last_report = now;
            }
        } catch (...) {
            log->error("Unexpected termination at cycle " + std::to_string(cycle()));
            if (cfg.save_trace) {
                log_cycle();
            }
            success = false;
            break;
        }
    }
    
    if (!is_done() && stats.cycle >= cfg.max_cycle) {
        log->warning("Simulation terminated at max_cycle limit: " + std::to_string(cfg.max_cycle));
        success = false;
    } else if (is_done()) {
        success = true;
    }
    
    while (!store_is_done() && stats.cycle < cfg.max_cycle) {
        cleanup_step();
    }
    
    if (!store_is_done()) {
        log->warning("Store did not complete");
        success = false;
    } else if (success) {
        // Only confirm success if simulation wasn't already marked failed (e.g., stall detection)
        success = true;
    }
    
    final_utilization();
    final_b_rows();
    final_b_elements_on_switch();
    final_pes_waiting_spad();
    final_pes_fifo_empty_stall();
    final_pes_fifo_blocked_stall();
    final_sw_stall_stats();

    // Copy B row demand histogram from controller to stats
    stats.b_row_demand_hist = controller.b_row_demand_hist;

    stats.c_nnz = acc_output.nnz();
}

} // namespace csegfold

