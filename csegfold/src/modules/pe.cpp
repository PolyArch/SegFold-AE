#include "csegfold/modules/pe.hpp"
#include "csegfold/modules/module.hpp"
#include <stdexcept>
#include <sstream>

namespace csegfold {

bool pe_active(const PEState& pe) {
    return pe.status != PEStatus::IDLE && 
           pe.b.val.has_value() && pe.b.val.value() != 0 && 
           pe.a.has_value() && pe.a.value() != 0;
}

bool pe_load_done(const PEState& pe, int cycle) {
    if (pe.status != PEStatus::LOAD || !pe.loadA_cycle.has_value()) {
        return false;
    }
    return (cycle - pe.loadA_cycle.value()) >= config_.num_cycles_load_a;
}

bool pe_mac_done(const PEState& pe, int cycle) {
    if (pe.status != PEStatus::MAC || !pe.mac_cycle.has_value()) {
        return false;
    }
    return (cycle - pe.mac_cycle.value()) >= config_.num_cycles_mult_ii;
}

bool pe_stall_c(const PEState& pe, int cycle) {
    if (!pe.storeC_cycle.has_value()) {
        return false;
    }
    return (cycle - pe.storeC_cycle.value()) < config_.num_cycles_store_c;
}

PEState PEModule::idle_pe() {
    PEState state;
    state.a = std::nullopt;
    state.a_m = std::nullopt;
    state.a_k = std::nullopt;
    state.b.val = std::nullopt;
    state.b.row = std::nullopt;
    state.b.col = std::nullopt;
    state.c.val = 0;
    state.c.m = std::nullopt;
    state.c.n = std::nullopt;
    state.c.last_k = std::nullopt;
    state.c.c_col = std::nullopt;
    state.loadA_cycle = std::nullopt;
    state.storeC_cycle = std::nullopt;
    state.mac_cycle = std::nullopt;
    state.status = PEStatus::IDLE;
    return state;
}

PEModule::PEModule() : BaseModule() {
    pe = init_arr(vrows(), vcols(), idle_pe());
    next_pe = init_arr(vrows(), vcols(), idle_pe());
    valid_a = std::vector<std::vector<bool>>(vrows(), std::vector<bool>(vcols(), false));
    store_c = std::vector<std::vector<bool>>(vrows(), std::vector<bool>(vcols(), false));
    load_c = std::vector<std::vector<bool>>(vrows(), std::vector<bool>(vcols(), false));
}

void PEModule::reset_next() {
    for (size_t i = 0; i < pe.size(); ++i) {
        for (size_t j = 0; j < pe[i].size(); ++j) {
            next_pe[i][j].update(idle_pe());
        }
    }
}

int PEModule::num_pes() const {
    return prows() * pcols();  // Use physical PE count for utilization calculation
}

int PEModule::num_active_pes() const {
    int count = 0;
    for (const auto& row : pe) {
        for (const auto& pe_state : row) {
            if (pe_active(pe_state)) {
                count++;
            }
        }
    }
    return count;
}

std::vector<std::unordered_map<std::string, int>> PEModule::b_positions(int cycle) const {
    std::vector<std::unordered_map<std::string, int>> positions;
    for (size_t i = 0; i < pe.size(); ++i) {
        for (size_t j = 0; j < pe[i].size(); ++j) {
            const auto& pe_state = pe[i][j];
            if (pe_active(pe_state) && pe_state.b.val.has_value()) {
                int remaining = 0;
                if (pe_state.status == PEStatus::LOAD && pe_state.loadA_cycle.has_value()) {
                    remaining = cfg.num_cycles_load_a - (cycle - pe_state.loadA_cycle.value());
                } else if (pe_state.status == PEStatus::MAC && pe_state.mac_cycle.has_value()) {
                    remaining = cfg.num_cycles_mult_ii - (cycle - pe_state.mac_cycle.value());
                }
                std::unordered_map<std::string, int> pos;
                pos["pos"] = 0; // "pe"
                pos["pe_row"] = static_cast<int>(i);
                pos["pe_col"] = static_cast<int>(j);
                pos["b_row"] = pe_state.b.row.value_or(-1);
                pos["b_col"] = pe_state.b.col.value_or(-1);
                pos["c_row"] = pe_state.a_m.value_or(-1);
                pos["c_col"] = pe_state.b.n.value_or(-1);
                pos["value"] = pe_state.b.val.value_or(0);
                pos["status"] = static_cast<int>(pe_state.status);
                pos["remaining"] = remaining;
                positions.push_back(pos);
            }
        }
    }
    return positions;
}

bool PEModule::next_pe_idle(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return next_pe[i][j].status == PEStatus::IDLE;
}

void PEModule::free_next_pe(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    next_pe[i][j].update(idle_pe());
}

void PEModule::free_next_b_val(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    auto idle = idle_pe();
    next_pe[i][j].a = idle.a;
    next_pe[i][j].b = idle.b;
    next_pe[i][j].loadA_cycle = idle.loadA_cycle;
    next_pe[i][j].mac_cycle = idle.mac_cycle;
    next_pe[i][j].status = idle.status;
}

int PEModule::map_col_to_idx(int i, int _j, int c_col) const {
    for (int j = _j; j < vcols(); ++j) {
        if (next_pe[i][j].b.col.has_value() && next_pe[i][j].b.col.value() == c_col) {
            return j;
        }
    }
    throw std::runtime_error("Column not found in PE row");
}

bool PEModule::valid_c(int i, int j) const {
    return !store_c[i][j] && !load_c[i][j];
}

void PEModule::clear_row(int i) {
    for (int j = 0; j < vcols(); ++j) {
        free_next_pe(i, j);
    }
}

void PEModule::push(int i, int j) {
    if (j + 1 < vcols()) {
        next_pe[i][j + 1] = next_pe[i][j];
        valid_a[i][j + 1] = valid_a[i][j];
        load_c[i][j + 1] = load_c[i][j];
        store_c[i][j + 1] = store_c[i][j];
        free_next_pe(i, j);
        valid_a[i][j] = false;
        load_c[i][j] = false;
        store_c[i][j] = false;
    }
}

int PEModule::log_active_c() const {
    int active_c = 0;
    for (const auto& row : next_pe) {
        for (const auto& pe_state : row) {
            if (pe_state.c.c_col.has_value()) {
                active_c++;
            }
        }
    }
    return active_c;
}

void PEModule::update_c(int i, int j) {
    auto& next_pe_state = next_pe[i][j];
    next_pe_state.c.m = next_pe_state.a_m;  // Use a_m (row index), not a (value)
    // Use original output column index (b.n) if available and valid, otherwise fall back to b.col
    if (next_pe_state.b.n.has_value() && next_pe_state.b.n.value() >= 0) {
        next_pe_state.c.n = next_pe_state.b.n.value();
    } else {
        next_pe_state.c.n = next_pe_state.b.col;  // Fallback to tiled column index
    }
    next_pe_state.c.c_col = next_pe_state.b.col;
    next_pe_state.c.last_k = next_pe_state.b.row;
}

} // namespace csegfold

