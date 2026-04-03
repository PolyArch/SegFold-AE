#include "csegfold/simulator/pe.hpp"
#include "csegfold/simulator/simulator.hpp"
#include "csegfold/modules/spad.hpp"
#include <cassert>
#include <sstream>

namespace csegfold {

void to_mac(PEModule* peModule, int i, int j) {
    assert(peModule->valid_a[i][j]);
    assert(peModule->valid_c(i, j));
    auto& next_pe = peModule->next_pe[i][j];
    assert(next_pe.status == PEStatus::LOAD);

    int a_val = next_pe.a.value_or(0);
    int b_val = next_pe.b.val.value_or(0);
    int c_val = next_pe.c.val;

    next_pe.status = PEStatus::MAC;
    next_pe.mac_cycle = peModule->cycle();
    next_pe.c.val = a_val * b_val + c_val;
    peModule->valid_a[i][j] = false;
    peModule->stats.macs++;

    if (peModule->debug()) {
        std::ostringstream oss;
        oss << "[PE] Cycle " << peModule->cycle()
            << " PE[" << i << "][" << j << "] LOAD->MAC: "
            << "a=" << a_val << " b=" << b_val << " c_old=" << c_val
            << " c_new=" << next_pe.c.val
            << " m=" << (next_pe.c.m.has_value() ? std::to_string(next_pe.c.m.value()) : "none")
            << " n=" << (next_pe.c.n.has_value() ? std::to_string(next_pe.c.n.value()) : "none");
        peModule->log->debug(oss.str());
    }
}

// Handle PE in LOAD state - process C store/load and transition to MAC when ready
static void handle_pe_load_state(Simulator* simulator, PEModule* peModule, int i, int j) {
    const auto& pe = peModule->pe[i][j];
    auto& next_pe = peModule->next_pe[i][j];
    next_pe.update(pe);

    bool valid_a;
    if (!simulator->cfg.enable_memory_hierarchy || simulator->cfg.bypass_a_memory_hierarchy) {
        valid_a = pe_load_done(pe, peModule->cycle());
    } else {
        valid_a = peModule->valid_a[i][j];
    }

    bool store_c = peModule->store_c[i][j];
    bool load_c = peModule->load_c[i][j];

    if (simulator->cfg.enable_spad) {
        if (store_c) {
            bool valid_store = store_c_from_pe_to_spad(&simulator->spadModule, peModule, i, j);
            if (!valid_store) {
                return;
            }
        }

        if (load_c) {
            bool valid_load = load_c_from_spad_to_pe(&simulator->spadModule, peModule, i, j);
            if (!valid_load) {
                return;
            }
        } else {
            peModule->update_c(i, j);
        }
    } else {
        if (store_c) {
            next_pe.c = peModule->idle_pe().c;
            peModule->store_c[i][j] = false;
        }
        if (load_c) {
            int new_c_col = next_pe.b.col.value();
            next_pe.c.val = 0;
            next_pe.c.m = next_pe.a_m;
            next_pe.c.n = next_pe.b.n.value();
            next_pe.c.last_k = next_pe.b.row;
            next_pe.c.c_col = new_c_col;
            peModule->load_c[i][j] = false;
        } else {
            peModule->update_c(i, j);
        }
    }

    peModule->valid_a[i][j] = valid_a;
    bool valid_c = peModule->valid_c(i, j);

    if (valid_a && valid_c) {
        to_mac(peModule, i, j);
    }
}

// Handle PE in MAC state - complete MAC operation and update accumulator
static void handle_pe_mac_state(Simulator* simulator, PEModule* peModule, int i, int j) {
    const auto& pe = peModule->pe[i][j];
    auto& next_pe = peModule->next_pe[i][j];
    next_pe.update(pe);

    if (pe_mac_done(pe, peModule->cycle()) && !pe_stall_c(pe, peModule->cycle())) {
        int row_idx = pe.a_m.value_or(-1);
        int col_idx = pe.b.n.value_or(-1);
        int a_val = pe.a.value_or(0);
        int b_val = pe.b.val.value_or(0);
        int product = a_val * b_val;

        assert(row_idx >= 0 && row_idx < simulator->matrix.M &&
               col_idx >= 0 && col_idx < simulator->matrix.N &&
               "PE invalid acc_output indices");
        simulator->acc_output.accumulate(row_idx, col_idx, product);
        peModule->free_next_b_val(i, j);

        if (simulator->cfg.enable_sw_pe_fifo &&
            simulator->fifo[i][j].rptr > simulator->fifo[i][j].lptr) {
            simulator->pop_fifo_to_pe(i, j);
        }
    } else if (peModule->debug()) {
        std::ostringstream oss;
        oss << "[PE] Cycle " << peModule->cycle()
            << " PE[" << i << "][" << j << "] MAC not done: "
            << "mac_done=" << pe_mac_done(pe, peModule->cycle())
            << " stall_c=" << pe_stall_c(pe, peModule->cycle());
        peModule->log->debug(oss.str());
    }
}

// Handle PE in IDLE state - check for FIFO data to process
static void handle_pe_idle_state(Simulator* simulator, PEModule* peModule, int i, int j) {
    const auto& pe = peModule->pe[i][j];
    auto& next_pe = peModule->next_pe[i][j];
    next_pe.c = pe.c;

    if (simulator->cfg.enable_sw_pe_fifo &&
        simulator->fifo[i][j].rptr > simulator->fifo[i][j].lptr) {
        simulator->pop_fifo_to_pe(i, j);
    }
}

void run_pes(Simulator* simulator, PEModule* peModule) {
    int freq = peModule->cfg.debug_log_frequency;
    bool should_debug = freq > 0 && (peModule->cycle() % freq == 0);

    if (should_debug && peModule->debug()) {
        std::ostringstream oss;
        oss << "[run_pes] Cycle " << peModule->cycle()
            << " Active PEs: " << peModule->num_active_pes();
        peModule->log->debug(oss.str());
    }

    for (size_t i = 0; i < peModule->pe.size(); ++i) {
        for (size_t j = 0; j < peModule->pe[i].size(); ++j) {
            const auto& pe = peModule->pe[i][j];

            if (should_debug && pe.status != PEStatus::IDLE && peModule->debug()) {
                std::ostringstream oss;
                oss << "[run_pes] Cycle " << peModule->cycle()
                    << " PE[" << i << "][" << j << "] status="
                    << (pe.status == PEStatus::LOAD ? "LOAD" : "MAC")
                    << " a=" << (pe.a.has_value() ? std::to_string(pe.a.value()) : "none")
                    << " b=" << (pe.b.val.has_value() ? std::to_string(pe.b.val.value()) : "none");
                peModule->log->debug(oss.str());
            }

            switch (pe.status) {
                case PEStatus::LOAD:
                    handle_pe_load_state(simulator, peModule, static_cast<int>(i), static_cast<int>(j));
                    break;
                case PEStatus::MAC:
                    handle_pe_mac_state(simulator, peModule, static_cast<int>(i), static_cast<int>(j));
                    break;
                case PEStatus::IDLE:
                    handle_pe_idle_state(simulator, peModule, static_cast<int>(i), static_cast<int>(j));
                    break;
            }
        }
    }
}

bool store_c_from_pe_to_spad(SPADModule* spadModule, PEModule* peModule, int i, int j) {
    auto& next_pe = peModule->next_pe[i][j];
    if (!next_pe.c.c_col.has_value()) {
        return false;
    }
    
    int c_col = next_pe.c.c_col.value();
    
    // Convert C struct to SPAD format (unordered_map<int, tuple<int, int, int>>)
    // SPAD stores: c_col -> (val, m, n)
    std::unordered_map<int, std::tuple<int, int, int>> c_data;
    c_data[c_col] = std::make_tuple(
        next_pe.c.val,
        next_pe.c.m.value_or(0),
        next_pe.c.n.value_or(0)
    );
    
    bool valid_store = spadModule->store(i, c_col, c_data);
    if (valid_store) {
        next_pe.c = peModule->idle_pe().c;
        peModule->store_c[i][j] = false;
    }
    return valid_store;
}

bool load_c_from_spad_to_pe(SPADModule* spadModule, PEModule* peModule, int i, int j) {
    auto& next_pe = peModule->next_pe[i][j];
    if (!next_pe.b.col.has_value()) {
        return false;
    }
    
    int new_c_col = next_pe.b.col.value();
    auto [valid_load, c_map_opt] = spadModule->load(i, new_c_col);
    
    if (valid_load) {
        if (!c_map_opt.has_value()) {
            // No C value in SPAD, create new
            next_pe.c.val = 0;
            next_pe.c.m = next_pe.a_m;
            next_pe.c.n = next_pe.b.n.value();
            next_pe.c.last_k = next_pe.b.row;
            next_pe.c.c_col = new_c_col;
        } else {
            // C value found in SPAD - c_map_opt is a map: c_col -> (val, m, n)
            // The map should contain the value for new_c_col
            auto& c_map = c_map_opt.value();
            auto it = c_map.find(new_c_col);
            if (it != c_map.end()) {
                auto [c_val, c_m, c_n] = it->second;
                // Check for cache miss (m or n mismatch)
                // In Python: next_pe["a"]["m"] != c["m"] or next_pe["b"]["n"] != c["n"]
                if (next_pe.a_m.has_value() && next_pe.b.col.has_value()) {
                    if (next_pe.a_m.value() != c_m || next_pe.b.col.value() != c_n) {
                        // Cache miss
                        spadModule->stats.spad_load_misses++;
                        spadModule->stats.spad_load_hits--;
                        next_pe.c.val = 0;
                        next_pe.c.m = next_pe.a_m;
                        next_pe.c.n = next_pe.b.n.value();
                        next_pe.c.last_k = next_pe.b.row;
                        next_pe.c.c_col = new_c_col;
                    } else {
                        // Cache hit
                        next_pe.c.val = c_val;
                        next_pe.c.m = c_m;
                        next_pe.c.n = c_n;
                        next_pe.c.last_k = next_pe.b.row;
                        next_pe.c.c_col = new_c_col;
                    }
                } else {
                    next_pe.c.val = c_val;
                    next_pe.c.m = c_m;
                    next_pe.c.n = c_n;
                    next_pe.c.last_k = next_pe.b.row;
                    next_pe.c.c_col = new_c_col;
                }
            } else {
                // Column not found in SPAD data (shouldn't happen, but handle it)
                next_pe.c.val = 0;
                next_pe.c.m = next_pe.a_m;
                next_pe.c.n = next_pe.b.n.value();
                next_pe.c.last_k = next_pe.b.row;
                next_pe.c.c_col = new_c_col;
            }
        }
        peModule->load_c[i][j] = false;
    }
    return valid_load;
}

} // namespace csegfold

