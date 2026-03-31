#include "csegfold/modules/mapper.hpp"
#include <algorithm>
#include <cassert>

namespace csegfold {

const std::unordered_map<DIRECTION, std::pair<int, int>> Mapper::step = {
    {DIRECTION::LEFT, {0, -1}},
    {DIRECTION::RIGHT, {0, 1}},
    {DIRECTION::UP, {-1, 0}},
    {DIRECTION::DOWN, {1, 0}}
};

Mapper::Mapper() : BaseModule() {
    mapping = std::vector<std::vector<std::optional<std::pair<int, int>>>>(
        prows(), std::vector<std::optional<std::pair<int, int>>>(pcols(), std::nullopt));
    
    if (cfg.enable_spatial_folding) {
        priority = {DIRECTION::RIGHT, DIRECTION::DOWN, DIRECTION::LEFT, DIRECTION::UP};
    } else {
        priority = {DIRECTION::RIGHT};
    }
    is_fold = std::vector<bool>(prows(), false);
    reset_request_counter();
}

void Mapper::reset_request_counter() {
    request_counter = 0;
    request_counter_per_row = std::vector<int>(prows(), 0);
}

std::optional<std::pair<int, int>> Mapper::get_physical_coords(int vrow, int vcol) {
    auto key = std::make_pair(vrow, vcol);
    auto it = virtual_to_physical.find(key);
    if (it != virtual_to_physical.end()) {
        return std::make_optional(it->second);
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> Mapper::get_virtual_coords(int prow, int pcol) {
    if (out_of_bounds(prow, pcol)) {
        return std::nullopt;
    }
    return mapping[prow][pcol];  // Already optional
}

bool Mapper::is_mapped(int vrow, int vcol) const {
    return virtual_to_physical.find(std::make_pair(vrow, vcol)) != virtual_to_physical.end();
}

int Mapper::get_row_length(int vrow) const {
    int count = 0;
    for (int pcol = 0; pcol < pcols(); ++pcol) {
        auto pos = mapping[vrow][pcol];
        if (!pos.has_value()) {
            if (is_fold[vrow]) {
                break;
            } else {
                count++;
            }
        } else if (pos.value().first == vrow) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

int Mapper::get_j_max(int vrow) const {
    return static_cast<int>(get_row_positions(vrow).size());
}

int Mapper::get_effective_row_length(int vrow) const {
    return std::max(get_j_max(vrow), get_row_length(vrow));
}

std::optional<std::pair<int, int>> Mapper::last_pos_at_row(int vrow) {
    auto positions = get_row_positions(vrow);
    return positions.empty() ? std::nullopt : std::make_optional<std::pair<int, int>>(positions.back());
}

std::vector<std::pair<int, int>> Mapper::get_row_positions(int vrow) const {
    std::vector<std::pair<int, int>> positions;
    for (const auto& kv : virtual_to_physical) {
        if (kv.first.first == vrow) {
            positions.push_back(kv.second);
        }
    }
    std::sort(positions.begin(), positions.end(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });
    return positions;
}

bool Mapper::out_of_bounds(int i, int j) const {
    return i < 0 || i >= prows() || j < 0 || j >= pcols();
}

bool Mapper::is_occupied(int i, int j) const {
    return out_of_bounds(i, j) || mapping[i][j].has_value();
}

void Mapper::evict_b_rows(int vrow) {
    auto positions_to_remove = get_row_positions(vrow);
    for (auto pos : positions_to_remove) {
        int prow = pos.first;
        int pcol = pos.second;
        if (mapping[prow][pcol].has_value()) {
            auto v_coords = mapping[prow][pcol].value();
            virtual_to_physical.erase(v_coords);
        }
        mapping[prow][pcol] = std::nullopt;
    }
    is_fold[vrow] = false;
}

std::optional<std::pair<int, int>> Mapper::map(int vrow, int vcol) {
    if (is_mapped(vrow, vcol)) {
        return get_physical_coords(vrow, vcol);
    }
    
    if (request_counter_per_row[vrow] > cfg.mapper_request_limit_per_cycle) {
        return std::nullopt;
    }
    
    request_counter++;
    request_counter_per_row[vrow]++;
    
    auto pos = find_next_free_pos(vrow);
    if (!pos.has_value()) {
        return std::nullopt;
    }
    
    auto pos_val = pos.value();
    int prow = pos_val.first;
    int pcol = pos_val.second;
    mapping[prow][pcol] = std::make_pair(vrow, vcol);
    virtual_to_physical[std::make_pair(vrow, vcol)] = std::make_pair(prow, pcol);
    
    if (prow != vrow) {
        stats.folding++;
        is_fold[vrow] = true;
    }
    
    return std::make_optional<std::pair<int, int>>(std::make_pair(prow, pcol));
}

int Mapper::get_remaining_request_limit(int vrow) const {
    return cfg.mapper_request_limit_per_cycle - request_counter_per_row[vrow];
}

std::optional<std::pair<int, int>> Mapper::find_next_free_pos(int vrow) {
    auto last_pos = last_pos_at_row(vrow);
    int curr_row, curr_col;
    
    if (!last_pos.has_value()) {
        if (!is_occupied(vrow, 0)) {
            return std::make_optional<std::pair<int, int>>(std::make_pair(vrow, 0));
        }
        curr_row = vrow;
        curr_col = 0;
    } else {
        curr_row = last_pos.value().first;
        curr_col = last_pos.value().second;
    }
    
    for (auto dir : priority) {
        auto step_val = step.at(dir);
        int row_incr = step_val.first;
        int col_incr = step_val.second;
        int next_row = curr_row + row_incr;
        int next_col = curr_col + col_incr;
        if (next_col == 0) {
            continue;
        }
        if (!is_occupied(next_row, next_col)) {
            return std::make_optional<std::pair<int, int>>(std::make_pair(next_row, next_col));
        }
    }
    return std::nullopt;
}

} // namespace csegfold

