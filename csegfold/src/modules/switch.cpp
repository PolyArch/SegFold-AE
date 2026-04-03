#include "csegfold/modules/switch.hpp"
#include "csegfold/modules/lookuptable.hpp"
#include "csegfold/modules/memoryController.hpp"
#include <cassert>
#include <algorithm>

namespace csegfold {

bool sw_active(const SwitchState& sw) {
    return sw.status != SwitchStatus::IDLE && 
           sw.b.val.has_value() && sw.b.val.value() != 0;
}

bool sw_idle(const SwitchState& sw) {
    return sw.status == SwitchStatus::IDLE;
}

bool sw_has_c_col(const SwitchState& sw) {
    return sw.c_col.has_value();
}

bool sw_load_done(const SwitchState& sw, int cycle) {
    if (sw.status != SwitchStatus::LOAD_B || !sw.loadB_cycle.has_value()) {
        return false;
    }
    return (cycle - sw.loadB_cycle.value()) >= config_.num_cycles_load_b;
}

SwitchModule::SwitchModule() : BaseModule() {
    switches = init_arr(vrows(), vcols(), idle_sw());
    next_switches = init_arr(vrows(), vcols(), idle_sw());
    b_loaded = std::vector<bool>(vrows(), false);
    row_full = std::vector<bool>(vrows(), false);
    reset_c_upd();
    init_b_loader_fifo();
}

SwitchState SwitchModule::idle_sw() {
    SwitchState sw;
    sw.status = SwitchStatus::IDLE;
    sw.b.val = std::nullopt;
    sw.b.row = std::nullopt;
    sw.b.col = std::nullopt;
    sw.b.n = std::nullopt;
    sw.loadB_cycle = std::nullopt;
    sw.c_col = std::nullopt;
    sw.memory_ready = false;
    return sw;
}

void SwitchModule::reset_next() {
    for (size_t i = 0; i < switches.size(); ++i) {
        for (size_t j = 0; j < switches[i].size(); ++j) {
            next_switches[i][j].update(idle_sw());
        }
    }
}

void SwitchModule::reset_c_upd() {
    c_upd_cnt = std::vector<int>(vrows(), 0);
}

void SwitchModule::record_c_upd(int i) {
    assert(0 <= i && i < vrows());
    c_upd_cnt[i]++;
}

bool SwitchModule::has_remaining_c_upd() const {
    return std::any_of(c_upd_cnt.begin(), c_upd_cnt.end(), [](int cnt) { return cnt > 0; });
}

void SwitchModule::handle_c_upd() {
    for (int i = 0; i < vrows(); ++i) {
        c_upd_cnt[i] = std::max(0, c_upd_cnt[i] - cfg.c_col_update_per_row);
    }
}

int SwitchModule::num_switches() const {
    return prows() * pcols();  // Use physical switch count for utilization calculation
}

int SwitchModule::num_active_switches() const {
    int count = 0;
    for (const auto& row : switches) {
        for (const auto& sw : row) {
            if (sw_active(sw)) {
                count++;
            }
        }
    }
    return count;
}

bool SwitchModule::next_sw_idle(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return next_switches[i][j].status == SwitchStatus::IDLE;
}

void SwitchModule::free_next_sw(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    next_switches[i][j].update(idle_sw());
    if (next_switches[i][j].c_col.has_value()) {
        next_switches[i][j].c_col = std::nullopt;
    }
}

void SwitchModule::free_next_sw_b_val(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    auto saved_c_col = next_switches[i][j].c_col;
    next_switches[i][j].update(idle_sw());
    next_switches[i][j].c_col = saved_c_col;
}

bool SwitchModule::c_eq_b(const SwitchState& sw) {
    assert(sw.status == SwitchStatus::MOVE);
    return sw.b.col.has_value() && sw.c_col.has_value() && 
           sw.b.col.value() == sw.c_col.value();
}

bool SwitchModule::c_lt_b(const SwitchState& sw) {
    assert(sw.status == SwitchStatus::MOVE);
    return sw.b.col.has_value() && sw.c_col.has_value() && 
           sw.b.col.value() > sw.c_col.value();
}

bool SwitchModule::c_gt_b(const SwitchState& sw) {
    return sw.b.col.has_value() && sw.c_col.has_value() && 
           sw.b.col.value() < sw.c_col.value();
}

bool SwitchModule::b_zero(const SwitchState& sw) {
    return !sw.b.val.has_value() || sw.b.val.value() == 0;
}

bool SwitchModule::has_lut() const {
    return cfg.use_lookup_table && lut != nullptr;
}

int SwitchModule::last_active_sw(int i) {
    for (int j = static_cast<int>(switches[i].size()) - 1; j >= 0; --j) {
        if (sw_has_c_col(switches[i][j])) {
            return j;
        }
    }
    return -1;
}

SwitchState& SwitchModule::get_sw(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return switches[i][j];
}

SwitchState& SwitchModule::get_next_sw(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return next_switches[i][j];
}

void SwitchModule::update_sw_c_col(int i, int j) {
    record_c_upd(i);
    auto& sw = get_sw(i, j);
    assert(!sw.c_col.has_value() || sw.c_col.value() != sw.b.col.value());
    sw.c_col = sw.b.col;
    if (has_lut()) {
        int mapped_col_idx = std::min(j, mapper.get_row_length(i) - 1);
        lut->send_update_request(i, sw.c_col.value(), mapped_col_idx);
    }
}

void SwitchModule::update_next_sw_c_col(int i, int j) {
    stats.c_updates++;
    record_c_upd(i);
    auto& sw = switches[i][j];
    assert(!sw.c_col.has_value() || sw.c_col.value() != sw.b.col.value());
    auto& next_sw = get_next_sw(i, j);
    next_sw.c_col = sw.b.col;
    if (has_lut()) {
        int mapped_col_idx = std::min(j, mapper.get_row_length(i) - 1);
        lut->send_update_request(i, next_sw.c_col.value(), mapped_col_idx);
    }
}

int SwitchModule::map_col_to_idx(int i, int b_col_dense) const {
    if (has_lut()) {
        auto result = lut->lookup(i, b_col_dense);
        if (result.has_value()) {
            stats.lut_hits++;
            assert(result.value() < mapper.get_row_length(i));
            return result.value();
        } else {
            stats.lut_miss++;
        }
    }
    
    int row_len = mapper.get_row_length(i);
    int left = 0, right = row_len - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        auto col = next_switches[i][mid].c_col;
        if (!col.has_value() || col.value() >= b_col_dense) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    return std::min(left, row_len - 1);
}

int SwitchModule::map_csr_to_idx(int i, int b_col_csr) const {
    int row_len = mapper.get_row_length(i);
    return b_col_csr < row_len ? b_col_csr : row_len - 1;
}

void SwitchModule::clear_loading_rows() {
    b_loaded = std::vector<bool>(vrows(), false);
}

int SwitchModule::get_num_idle_sw(int i, int j) {
    int count = 0;
    int row_len = mapper.get_row_length(i);
    for (int _j = j; _j < row_len; ++_j) {
        if (sw_idle(next_switches[i][_j])) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

bool SwitchModule::is_last_col(int i, int j) const {
    return j == mapper.get_row_length(i) - 1;
}

bool SwitchModule::next_sw_has_c_col(int i, int j) const {
    return next_switches[i][j].c_col.has_value();
}

bool SwitchModule::next_sw_row_full(int i) const {
    int row_len = mapper.get_row_length(i);
    for (int j = 0; j < row_len; ++j) {
        if (!next_sw_has_c_col(i, j)) {
            return false;
        }
    }
    return true;
}

void SwitchModule::evict_b_rows(int i) {
    assert(cfg.enable_tile_eviction);
    for (int j = 0; j < vcols(); ++j) {
        free_next_sw(i, j);
    }
    row_full[i] = false;
}

void SwitchModule::init_static_c_col(class MatrixLoader* /* matrix */) {
    // Static column assignment not yet implemented (dynamic routing is used instead)
}

int SwitchModule::get_start_offset(MemoryController* controller, int r, int c_start, int i) {
    int row_len = mapper.get_row_length(i);
    c_start = std::min(c_start, row_len - 1);
    
    if (controller->cfg.enable_dynamic_routing) {
        if (controller->B_csr.find(r) != controller->B_csr.end() && 
            c_start < static_cast<int>(controller->B_csr.at(r).size())) {
            int c_col = controller->B_csr.at(r)[c_start].first;
            int mapped_idx = map_col_to_idx(i, c_col);
            return std::max(mapped_idx, c_start);
        }
        return c_start;
    } else if (controller->cfg.enable_offset) {
        return map_csr_to_idx(i, c_start);
    } else {
        return 0;
    }
}

int SwitchModule::get_available_capacity(int i, int start_j) {
    // When FIFO is enabled, count consecutive positions that can accept B elements
    // A position can accept B if:
    //   - NOT already loaded this cycle (input channel limit), AND
    //   - Switch is IDLE and FIFO is empty (direct load), OR FIFO is not full (queue to FIFO)
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        // FIFO disabled - fall back to counting IDLE switches
        return get_num_idle_sw(i, start_j);
    }

    int count = 0;
    int row_len = mapper.get_row_length(i);
    for (int j = start_j; j < row_len; ++j) {
        // Input channel limit: only 1 B element per position per cycle
        if (is_b_position_loaded(i, j)) {
            break;  // Position already received B element this cycle
        }

        bool can_accept = false;

        // Check if switch can accept (either direct or via FIFO)
        if (sw_idle(next_switches[i][j]) && b_loader_fifo_empty(i, j)) {
            // Direct load path
            can_accept = true;
        } else if (!b_loader_fifo_full(i, j)) {
            // FIFO has capacity
            can_accept = true;
        }

        if (can_accept) {
            count++;
        } else {
            // Stop at first position that can't accept
            break;
        }
    }
    return count;
}

int SwitchModule::get_max_load(int i, int start_j) {
    // Use available capacity (considers FIFO) when FIFO is enabled
    int available = get_available_capacity(i, start_j);
    int j_max = mapper.get_j_max(i);
    int num_mapper_requests = mapper.get_remaining_request_limit(i);
    return std::min(available, j_max + num_mapper_requests - start_j);
}

std::vector<std::unordered_map<std::string, int>> SwitchModule::b_positions(int cycle) const {
    std::vector<std::unordered_map<std::string, int>> positions;
    for (size_t i = 0; i < switches.size(); ++i) {
        for (size_t j = 0; j < switches[i].size(); ++j) {
            const auto& sw_state = switches[i][j];
            if (sw_active(sw_state) && sw_state.b.val.has_value()) {
                int remaining = 0;
                if (sw_state.status == SwitchStatus::LOAD_B && sw_state.loadB_cycle.has_value()) {
                    remaining = cfg.num_cycles_load_b - (cycle - sw_state.loadB_cycle.value());
                }
                std::unordered_map<std::string, int> pos;
                pos["pos"] = 1; // "switch"
                pos["sw_row"] = static_cast<int>(i);
                pos["sw_col"] = static_cast<int>(j);
                pos["b_row"] = sw_state.b.row.value_or(-1);
                pos["b_col"] = sw_state.b.col.value_or(-1);
                pos["value"] = sw_state.b.val.value_or(0);
                pos["c_col"] = sw_state.c_col.value_or(-1);
                pos["status"] = static_cast<int>(sw_state.status);
                pos["remaining"] = remaining;
                positions.push_back(pos);
            } else {
                // Idle switch with c_col
                std::unordered_map<std::string, int> pos;
                pos["pos"] = 2; // "idle_switch"
                pos["sw_row"] = static_cast<int>(i);
                pos["sw_col"] = static_cast<int>(j);
                pos["c_col"] = sw_state.c_col.value_or(-1);
                positions.push_back(pos);
            }
        }
    }
    return positions;
}

// B Loader FIFO implementation
void SwitchModule::init_b_loader_fifo() {
    if (cfg.enable_b_loader_fifo && cfg.b_loader_fifo_size > 0) {
        b_loader_fifo = std::vector<std::vector<BLoaderFIFO>>(
            vrows(), std::vector<BLoaderFIFO>(vcols()));
    }
}

bool SwitchModule::b_loader_fifo_empty(int i, int j) const {
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        return true;
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return b_loader_fifo[i][j].is_empty();
}

bool SwitchModule::b_loader_fifo_full(int i, int j) const {
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        return false;  // "Never full" when disabled
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return b_loader_fifo[i][j].is_full(cfg.b_loader_fifo_size);
}

int SwitchModule::enqueue_b_loader_fifo(int i, int j, int b_row, int b_col, int b_val, int b_n, int orig_row, int cycle) {
    assert(cfg.enable_b_loader_fifo && cfg.b_loader_fifo_size > 0);
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    assert(!b_loader_fifo_full(i, j));

    auto& fifo = b_loader_fifo[i][j];
    int fifo_idx = static_cast<int>(fifo.entries.size());

    BLoaderFIFOEntry entry;
    entry.b_row = b_row;
    entry.b_col = b_col;
    entry.b_val = b_val;
    entry.b_n = b_n;
    entry.orig_row = orig_row;
    entry.loadB_cycle = cycle;
    entry.fifo_idx = fifo_idx;
    entry.memory_ready = false;

    fifo.entries.push_back(entry);
    fifo.rptr++;

    return fifo_idx;
}

bool SwitchModule::dequeue_to_switch(int i, int j) {
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        return false;
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());

    auto& fifo = b_loader_fifo[i][j];
    if (fifo.is_empty()) {
        return false;
    }

    // Get the entry at lptr
    const auto& entry = fifo.entries[fifo.lptr];

    // Load to next_switch
    auto& next_switch = next_switches[i][j];
    next_switch.b.row = entry.b_row;
    next_switch.b.col = entry.b_col;
    next_switch.b.val = entry.b_val;
    next_switch.b.n = entry.b_n;

    // If memory is already ready (response arrived while in FIFO), go directly to MOVE
    // Otherwise, go to LOAD_B and wait for memory response
    if (entry.memory_ready) {
        next_switch.status = SwitchStatus::MOVE;
        next_switch.loadB_cycle = std::nullopt;  // Clear loadB_cycle like complete_b_load() does
    } else {
        next_switch.status = SwitchStatus::LOAD_B;
        next_switch.loadB_cycle = entry.loadB_cycle;
        // Track that this FIFO entry was dequeued before memory was ready
        // When the memory response arrives, we need to complete the switch's load
        record_pending_fifo_to_switch(i, j, entry.fifo_idx);
    }

    // Advance read pointer
    fifo.lptr++;

    return true;
}

bool SwitchModule::all_b_loader_fifos_empty() const {
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        return true;
    }
    for (int i = 0; i < vrows(); ++i) {
        for (int j = 0; j < vcols(); ++j) {
            if (!b_loader_fifo[i][j].is_empty()) {
                return false;
            }
        }
    }
    return true;
}

void SwitchModule::mark_fifo_entry_ready(int i, int j, int fifo_idx) {
    if (!cfg.enable_b_loader_fifo || cfg.b_loader_fifo_size <= 0) {
        return;
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());

    auto& fifo = b_loader_fifo[i][j];
    if (fifo_idx >= 0 && fifo_idx < static_cast<int>(fifo.entries.size())) {
        fifo.entries[fifo_idx].memory_ready = true;
    }
}

// Input channel tracking - limit 1 B element per position per cycle
void SwitchModule::reset_b_position_loaded() {
    if (b_position_loaded_this_cycle.empty()) {
        b_position_loaded_this_cycle = std::vector<std::vector<bool>>(
            vrows(), std::vector<bool>(vcols(), false));
    } else {
        for (int i = 0; i < vrows(); ++i) {
            for (int j = 0; j < vcols(); ++j) {
                b_position_loaded_this_cycle[i][j] = false;
            }
        }
    }
}

bool SwitchModule::is_b_position_loaded(int i, int j) const {
    if (b_position_loaded_this_cycle.empty()) {
        return false;
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    return b_position_loaded_this_cycle[i][j];
}

void SwitchModule::mark_b_position_loaded(int i, int j) {
    if (b_position_loaded_this_cycle.empty()) {
        b_position_loaded_this_cycle = std::vector<std::vector<bool>>(
            vrows(), std::vector<bool>(vcols(), false));
    }
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    b_position_loaded_this_cycle[i][j] = true;
}

// Row load info tracking for idle switch analysis
void SwitchModule::clear_row_load_info() {
    if (row_load_info.empty()) {
        row_load_info = std::vector<std::vector<BRowLoadInfo>>(vrows());
    } else {
        for (int i = 0; i < vrows(); ++i) {
            row_load_info[i].clear();
        }
    }
}

void SwitchModule::record_row_load(int i, int b_row, int start_j, int potential_load_space, int actual_b_elements, bool row_complete) {
    if (row_load_info.empty()) {
        row_load_info = std::vector<std::vector<BRowLoadInfo>>(vrows());
    }
    assert(0 <= i && i < vrows());
    BRowLoadInfo info;
    info.b_row = b_row;
    info.start_j = start_j;
    info.potential_load_space = potential_load_space;
    info.actual_b_elements = actual_b_elements;
    info.row_complete = row_complete;
    row_load_info[i].push_back(info);
}

void SwitchModule::complete_b_load(int i, int j) {
    auto& switch_ = switches[i][j];
    auto& next_switch = next_switches[i][j];

    if (!switch_.b.val.has_value()) {
        return;  // No B value to process
    }

    if (b_zero(switch_)) {
        free_next_sw_b_val(i, j);
        return;
    }

    next_switch.status = SwitchStatus::MOVE;
    next_switch.loadB_cycle = std::nullopt;
    next_switch.b = switch_.b;
}

void SwitchModule::mark_switch_memory_ready(int i, int j) {
    assert(0 <= i && i < vrows() && 0 <= j && j < vcols());
    switches[i][j].memory_ready = true;
}

void SwitchModule::record_pending_fifo_to_switch(int i, int j, int fifo_idx) {
    pending_fifo_to_switch_.push_back({i, j, fifo_idx});
}

bool SwitchModule::check_and_remove_pending_fifo_to_switch(int i, int j, int fifo_idx) {
    for (auto it = pending_fifo_to_switch_.begin(); it != pending_fifo_to_switch_.end(); ++it) {
        if (it->i == i && it->j == j && it->fifo_idx == fifo_idx) {
            pending_fifo_to_switch_.erase(it);
            return true;
        }
    }
    return false;
}

} // namespace csegfold

