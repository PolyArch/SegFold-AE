#include "csegfold/simulator/switch.hpp"
#include "csegfold/simulator/simulator.hpp"
#include "csegfold/modules/pe.hpp"
#include "csegfold/modules/spad.hpp"
#include <cassert>
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <iostream>

namespace csegfold {

static void send_b_to_switch(SwitchModule* switchModule, int i, int j) {
    auto& switch_ = switchModule->switches[i][j];
    auto& next_switch = switchModule->next_switches[i][j];
    
    assert(switch_.b.val.has_value() && "Switch must have B value to send to adjacent switch");
    
    if (SwitchModule::b_zero(switch_)) {
        switchModule->free_next_sw_b_val(i, j);
        return;
    }
    
    next_switch.status = SwitchStatus::MOVE;
    next_switch.loadB_cycle = std::nullopt;
    next_switch.b = switch_.b;
}

static void send_b_to_fifo(Simulator* simulator, SwitchModule* switchModule, int i, int j, PEModule* peModule) {
    assert(simulator->cfg.enable_sw_pe_fifo && "SW-PE FIFO is not enabled");
    
    auto& pe = peModule->pe[i][j];
    auto& switch_ = switchModule->switches[i][j];
    auto& next_switch = switchModule->next_switches[i][j];
    
    assert(switch_.b.val.has_value() && "Switch must have B value to send to PE");
    
    if (simulator->debug()) {
        std::ostringstream oss;
        oss << "[send_b_to_fifo] Cycle " << simulator->cycle() 
            << " Switch[" << i << "][" << j << "] sending to FIFO: "
            << "b_val=" << switch_.b.val.value() 
            << " b_row=" << (switch_.b.row.has_value() ? std::to_string(switch_.b.row.value()) : "none")
            << " b_col=" << (switch_.b.col.has_value() ? std::to_string(switch_.b.col.value()) : "none");
        simulator->log->debug(oss.str());
    }
    
    auto saved_c_col = next_switch.c_col;
    next_switch.update(SwitchModule::idle_sw());
    next_switch.c_col = saved_c_col;
    
    std::unordered_map<std::string, int> next_pe_update;
    
    next_pe_update["b_val"] = switch_.b.val.value();
    next_pe_update["b_row"] = switch_.b.row.value();
    next_pe_update["b_col"] = switch_.b.col.value();
    
    // Get original A indices first
    auto orig_idx_a = simulator->matrix.get_original_index_a(i, switch_.b.row.value());
    if (orig_idx_a.first < 0 || orig_idx_a.second < 0) {
        std::cerr << "get_original_index_a failed - no A element found at (pe_row=" << i 
                  << ", b_row=" << switch_.b.row.value() << ") in tiled A_indexed" << std::endl;
        assert(false && "get_original_index_a failed - see stderr for details");
    }
    int a_val = simulator->matrix.A_orig(orig_idx_a.first, orig_idx_a.second);
    next_pe_update["a_val"] = a_val;
    next_pe_update["a_m"] = orig_idx_a.first;
    next_pe_update["a_k"] = orig_idx_a.second;
    
    // Get original B column index (output column n)
    // Use switch_.b.n which contains the global output column index from col_idx_b
    // This is set in update_switch_with_b_data from col_idx_b[r][c]
    if (switch_.b.n.has_value()) {
        next_pe_update["b_n"] = switch_.b.n.value();  // Use global output column index
    } else {
        // Fallback to b.col if n is not set (should not happen in normal operation)
        next_pe_update["b_n"] = switch_.b.col.value();
    }
    
    // Log for rows 4-7 debugging
    if (orig_idx_a.first >= 4 || switch_.b.col.value() >= 4) {
        std::ostringstream oss;
        oss << "[send_b_to_fifo] Cycle " << simulator->cycle() 
            << " Switch[" << i << "][" << j << "] PE row " << i
            << " -> orig A row=" << orig_idx_a.first << " orig A col=" << orig_idx_a.second
            << " B row=" << switch_.b.row.value() << " B col=" << switch_.b.col.value()
            << " output_col=" << next_pe_update["b_n"];
        simulator->log->debug(oss.str());
    }
    
    if (pe.b.row.has_value()) {
        if (simulator->matrix.b_is_same_block(switch_.b.row.value(), pe.b.row.value())) {
            next_pe_update["storeC_cycle"] = simulator->stats.cycle;
        }
    }

    // Bypass: if FIFO is empty and next_pe is idle, write directly to next_pe
    if (simulator->cfg.enable_fifo_bypass &&
        simulator->fifo[i][j].rptr <= simulator->fifo[i][j].lptr &&
        peModule->next_pe[i][j].status == PEStatus::IDLE) {
        simulator->bypass_fifo_to_pe(i, j, next_pe_update);
        simulator->stats.fifo_bypass_count++;
        return;
    }

    // Normal path: enqueue to FIFO
    simulator->fifo[i][j].pe_update.push_back(next_pe_update);
    simulator->fifo[i][j].rptr++;
}

static void move_b_to_adjacent_switch(SwitchModule* switchModule, int i, int j) {
    auto& switch_ = switchModule->switches[i][j];
    auto& next_switch = switchModule->next_switches[i][j];
    auto& right_switch = switchModule->next_switches[i][j + 1];
    
    assert(right_switch.status == SwitchStatus::IDLE && "Right switch must be idle to move B");
    
    auto saved_c_col = next_switch.c_col;
    next_switch.update(SwitchModule::idle_sw());
    next_switch.c_col = saved_c_col;
    right_switch.b = switch_.b;
    right_switch.status = SwitchStatus::MOVE;
}

static void push_boxes_to_right(Simulator* simulator, int i, int j) {
    simulator->stats.num_push++;
    
    int start_j = j;
    for (int k = j + 1; k < simulator->switchModule.vcols(); ++k) {
        if (!simulator->switchModule.next_sw_has_c_col(i, k)) {
            start_j = k;
            break;
        }
    }
    
    for (int k = start_j; k > j; --k) {
        if (k < static_cast<int>(simulator->switchModule.switches[i].size()) - 1) {
            auto& right_switch = simulator->switchModule.switches[i][k + 1];
            auto& switch_ = simulator->switchModule.switches[i][k];
            
            if (switch_.c_col.has_value()) {
                right_switch.c_col = switch_.c_col;
                if (simulator->switchModule.has_lut() && simulator->cfg.update_on_move) {
                    int mapped_col_idx = std::min(k + 1, simulator->switchModule.mapper.get_row_length(i) - 1);
                    simulator->switchModule.lut->send_update_request(i, switch_.c_col.value(), mapped_col_idx);
                }
                switch_.c_col = std::nullopt;
            }
        }
    }
}

static void update_switchModule_c_col(Simulator* simulator, SwitchModule* switchModule) {
    for (int i = 0; i < switchModule->vrows(); ++i) {
        int num_pushes = 0;
        for (int j = 0; j < switchModule->vcols(); ++j) {
            auto& switch_ = switchModule->switches[i][j];
            
            if (switch_.status == SwitchStatus::LOAD_B || switch_.status == SwitchStatus::MOVE) {
                if (!switch_.c_col.has_value()) {
                    switchModule->update_sw_c_col(i, j);
                } else if (SwitchModule::c_gt_b(switch_)) {
                    if (switchModule->row_full[i]) {
                        switchModule->update_sw_c_col(i, j);
                        simulator->stats.c_updates++;
                        continue;
                    }
                    int last_j = switchModule->last_active_sw(i);
                    assert(last_j >= 0 && "Row has no active switch");
                    
                    if (last_j + 1 < switchModule->vcols()) {
                        if (num_pushes >= switchModule->cfg.max_push) {
                            break;
                        }
                        push_boxes_to_right(simulator, i, j);
                        num_pushes++;
                    } else {
                        switchModule->row_full[i] = true;
                        simulator->stats.c_updates++;
                    }
                    switchModule->update_sw_c_col(i, j);
                }
            }
        }
    }
}

static void update_switchModule_next_switches(Simulator* simulator, SwitchModule* switchModule,
                                              PEModule* peModule) {
    for (int i = switchModule->vrows() - 1; i >= 0; --i) {
        for (int j = switchModule->vcols() - 1; j >= 0; --j) {
            auto& switch_ = switchModule->switches[i][j];
            auto& next_switch = switchModule->next_switches[i][j];
            
            switch (switch_.status) {
                case SwitchStatus::LOAD_B: {
                    next_switch.update(switch_);
                    if (simulator->cfg.enable_memory_hierarchy) {
                        // Check if memory response has arrived
                        if (switch_.memory_ready) {
                            // Memory is ready, transition to MOVE
                            send_b_to_switch(switchModule, i, j);
                        }
                        continue;
                    }
                    if (sw_load_done(switch_, simulator->stats.cycle)) {
                        send_b_to_switch(switchModule, i, j);
                    }
                    break;
                }
                case SwitchStatus::MOVE: {
                    next_switch.update(switch_);
                    if (!switch_.c_col.has_value()) {
                        continue;
                    }
                    if (SwitchModule::c_eq_b(switch_)) {
                        if (simulator->cfg.decouple_sw_and_pe) {
                            switchModule->free_next_sw(i, j);
                        } else if (simulator->cfg.enable_sw_pe_fifo) {
                            if (simulator->fifo_not_full(i, j)) {
                                send_b_to_fifo(simulator, switchModule, i, j, peModule);
                            } else {
                                simulator->stats.sw_move_stall_by_fifo++;
                            }
                        }
                    } else if (SwitchModule::c_lt_b(switch_)) {
                        if (switchModule->row_full[i]) {
                            int last_j = switchModule->last_active_sw(i);
                            assert(last_j >= 0 && "Row has no active switch");
                            if (j < last_j) {
                                if (switchModule->next_sw_idle(i, j + 1)) {
                                    move_b_to_adjacent_switch(switchModule, i, j);
                                } else {
                                    simulator->stats.sw_move_stall_by_network++;
                                }
                            } else {
                                switchModule->update_next_sw_c_col(i, j);
                            }
                            continue;
                        }
                        if (j + 1 < switchModule->vcols()) {
                            if (switchModule->next_sw_idle(i, j + 1)) {
                                move_b_to_adjacent_switch(switchModule, i, j);
                            } else {
                                simulator->stats.sw_move_stall_by_network++;
                            }
                        } else {
                            switchModule->row_full[i] = true;
                            switchModule->update_next_sw_c_col(i, j);
                        }
                    }
                    break;
                }
                case SwitchStatus::IDLE: {
                    next_switch.update(switch_);
                    break;
                }
            }
        }
    }
}

// Drain B loader FIFOs to idle switches
static void drain_b_loader_fifos(Simulator* simulator, SwitchModule* switchModule) {
    if (!switchModule->cfg.enable_b_loader_fifo ||
        switchModule->cfg.b_loader_fifo_size <= 0) {
        return;
    }

    for (int i = 0; i < switchModule->vrows(); ++i) {
        for (int j = 0; j < switchModule->vcols(); ++j) {
            // Check if switch is IDLE and FIFO has entries
            if (switchModule->next_sw_idle(i, j) &&
                !switchModule->b_loader_fifo_empty(i, j)) {
                // Dequeue from FIFO to switch
                if (switchModule->dequeue_to_switch(i, j)) {
                    // Track whether memory was ready (went to MOVE) or still waiting (went to LOAD_B)
                    auto& next_sw = switchModule->get_next_sw(i, j);
                    if (next_sw.status == SwitchStatus::MOVE) {
                        simulator->stats.b_fifo_memory_ready++;
                    } else if (next_sw.status == SwitchStatus::LOAD_B) {
                        simulator->stats.b_fifo_memory_wait++;
                    }
                }
            }
        }
    }
}

void run_switches(Simulator* simulator, SwitchModule* switchModule, PEModule* peModule) {
    update_switchModule_c_col(simulator, switchModule);
    update_switchModule_next_switches(simulator, switchModule, peModule);
    // Drain B loader FIFOs to idle switches after state updates
    drain_b_loader_fifos(simulator, switchModule);
    switchModule->handle_c_upd();
    switchModule->mapper.reset_request_counter();
}

void run_evictions(Simulator* simulator, MemoryController* controller,
                   SwitchModule* switchModule, PEModule* peModule) {
    if (!simulator->cfg.enable_tile_eviction) {
        return;
    }
    
    for (int i = 0; i < switchModule->vrows(); ++i) {
        if (!controller->ready_to_evict[i]) {
            continue;
        }
        
        // Check if all switches and PEs are idle, and FIFO is empty (Python: lines 295-297)
        bool ready_to_evict = true;
        for (int j = 0; j < switchModule->vcols(); ++j) {
            if (!switchModule->next_sw_idle(i, j) || 
                !peModule->next_pe_idle(i, j) || 
                simulator->fifo[i][j].rptr > simulator->fifo[i][j].lptr) {
                ready_to_evict = false;
                if (simulator->debug()) {
                    std::ostringstream oss;
                    oss << "[run_evictions] Cycle " << simulator->cycle() 
                        << " PE row " << i << " not ready: "
                        << "sw_idle=" << switchModule->next_sw_idle(i, j)
                        << " pe_idle=" << peModule->next_pe_idle(i, j)
                        << " fifo_empty=" << (simulator->fifo[i][j].rptr <= simulator->fifo[i][j].lptr);
                    simulator->log->debug(oss.str());
                }
                break;
            }
        }
        
        if (ready_to_evict) {
            if (simulator->debug()) {
                std::ostringstream oss;
                oss << "[run_evictions] Cycle " << simulator->cycle() 
                    << " Evicting B rows for PE row " << i;
                simulator->log->debug(oss.str());
            }
            switchModule->evict_b_rows(i);
            switchModule->mapper.evict_b_rows(i);
            controller->ready_to_evict[i] = false;
            if (switchModule->has_lut()) {
                switchModule->lut->clear_row(i);
            }
            // After eviction, try to fill the loader window with the next tile
            controller->fill_b_loader_window();
        }
    }
}

} // namespace csegfold
