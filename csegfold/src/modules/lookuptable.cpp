#include "csegfold/modules/lookuptable.hpp"
#include <algorithm>
#include <cassert>

namespace csegfold {

LookUpTable::LookUpTable() : BaseModule() {
    lookup_tables.resize(prows());
    c_col_snapshots.resize(prows());
    update_queue.resize(prows());
}

std::optional<int> LookUpTable::lookup(int i, int b_col_dense) {
    if (i < 0 || i >= prows()) {
        return std::nullopt;
    }
    
    cycle_requests++;
    auto it = lookup_tables[i].find(b_col_dense);
    if (it != lookup_tables[i].end()) {
        return it->second;
    }
    return std::nullopt;
}

int LookUpTable::max_updates_per_cycle() const {
    return cfg.max_updates_per_cycle;
}

void LookUpTable::clear_row(int i) {
    lookup_tables[i].clear();
    c_col_snapshots[i].clear();
    update_queue[i].clear();
}

void LookUpTable::clear() {
    for (int i = 0; i < prows(); ++i) {
        clear_row(i);
    }
}

void LookUpTable::send_update_request(int i, int b_col_dense, int virtual_pe_col_idx) {
    cycle_updates++;
    update_queue[i].push_back({b_col_dense, virtual_pe_col_idx});
}

void LookUpTable::update_mapping_from_queue() {
    for (int i = 0; i < prows(); ++i) {
        int n_updates = std::min(max_updates_per_cycle(), static_cast<int>(update_queue[i].size()));
        for (int _ = 0; _ < n_updates; ++_) {
            auto pair = update_queue[i].front();
            int b_col_dense = pair.first;
            int virtual_pe_col_idx = pair.second;
            update_queue[i].erase(update_queue[i].begin());
            lookup_tables[i][b_col_dense] = virtual_pe_col_idx;
        }
    }
}

void LookUpTable::update() {
    update_stats();
    cycle_requests = 0;
    cycle_updates = 0;
}

void LookUpTable::update_stats() {
    total_requests += cycle_requests;
    total_updates += cycle_updates;
    max_requests = std::max(max_requests, cycle_requests);
    max_updates = std::max(max_updates, cycle_updates);
    
    stats.lut_req = total_requests;
    stats.lut_upd = total_updates;
    stats.max_lut_req = max_requests;
    stats.max_lut_upd = max_updates;
    
    cycle_requests = 0;
    cycle_updates = 0;
}

ReverseLookUpTable::ReverseLookUpTable() : LookUpTable() {
    last_updated_sw = std::vector<std::optional<int>>(prows(), std::nullopt);
}

std::optional<int> ReverseLookUpTable::lookup(int i, int b_col_dense) {
    if (i < 0 || i >= prows() || lookup_tables[i].empty()) {
        return std::nullopt;
    }
    
    cycle_requests++;
    std::unordered_map<int, int> candidates;
    for (const auto& [k, v] : lookup_tables[i]) {
        if (v <= b_col_dense) {
            candidates[k] = v;
        }
    }
    
    if (candidates.empty()) {
        return std::nullopt;
    }
    
    auto max_it = std::max_element(candidates.begin(), candidates.end(),
                                   [](const auto& a, const auto& b) { return a.first < b.first; });
    return max_it->first;
}

void ReverseLookUpTable::update_mapping_from_queue() {
    if (cfg.update_with_round_robin) {
        update_mapping_from_queue_round_robin();
    } else {
        for (int i = 0; i < prows(); ++i) {
            int n_updates = std::min(max_updates_per_cycle(), static_cast<int>(update_queue[i].size()));
            for (int _ = 0; _ < n_updates; ++_) {
                auto pair = update_queue[i].front();
                int b_col_dense = pair.first;
                int virtual_pe_col_idx = pair.second;
                update_queue[i].erase(update_queue[i].begin());
                lookup_tables[i][virtual_pe_col_idx] = b_col_dense;
            }
        }
    }
}

void ReverseLookUpTable::update_mapping_from_queue_round_robin() {
    for (int i = 0; i < prows(); ++i) {
        auto sorted_queue = update_queue[i];
        std::sort(sorted_queue.begin(), sorted_queue.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        update_queue[i].clear();
        
        if (last_updated_sw[i].has_value() && !sorted_queue.empty()) {
            int last = last_updated_sw[i].value();
            size_t split_idx = 0;
            for (size_t idx = 0; idx < sorted_queue.size(); ++idx) {
                if (sorted_queue[idx].second > last) {
                    split_idx = idx;
                    round_robin_updates++;
                    break;
                }
            }
            std::rotate(sorted_queue.begin(), sorted_queue.begin() + split_idx, sorted_queue.end());
        }
        
        int n_updates = std::min(max_updates_per_cycle(), static_cast<int>(sorted_queue.size()));
        for (int _ = 0; _ < n_updates; ++_) {
            auto pair = sorted_queue.front();
            int b_col_dense = pair.first;
            int virtual_pe_col_idx = pair.second;
            sorted_queue.erase(sorted_queue.begin());
            lookup_tables[i][virtual_pe_col_idx] = b_col_dense;
            last_updated_sw[i] = virtual_pe_col_idx;
        }
    }
}

void ReverseLookUpTable::update_stats() {
    LookUpTable::update_stats();
    stats.round_robin_upd = round_robin_updates;
}

void run_lookup_tables(LookUpTable* lookupTableModule) {
    lookupTableModule->update_mapping_from_queue();
    lookupTableModule->update();
}

} // namespace csegfold

