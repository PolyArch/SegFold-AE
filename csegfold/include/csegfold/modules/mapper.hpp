#pragma once

#include "csegfold/modules/module.hpp"
#include <vector>
#include <unordered_map>
#include <optional>
#include <tuple>

namespace csegfold {

enum class DIRECTION {
    LEFT,
    RIGHT,
    UP,
    DOWN
};

class Mapper : public BaseModule {
public:
    Mapper();
    
    std::optional<std::pair<int, int>> get_physical_coords(int vrow, int vcol);
    std::optional<std::pair<int, int>> get_virtual_coords(int prow, int pcol);
    bool is_mapped(int vrow, int vcol) const;
    
    int get_row_length(int vrow) const;
    int get_j_max(int vrow) const;
    int get_effective_row_length(int vrow) const;
    std::optional<std::pair<int, int>> last_pos_at_row(int vrow);
    
    bool out_of_bounds(int i, int j) const;
    bool is_occupied(int i, int j) const;
    
    void evict_b_rows(int vrow);
    
    std::optional<std::pair<int, int>> map(int vrow, int vcol);
    int get_remaining_request_limit(int vrow) const;
    std::optional<std::pair<int, int>> find_next_free_pos(int vrow);
    
    void reset_request_counter();
    
    std::vector<std::vector<std::optional<std::pair<int, int>>>> mapping;
    struct PairHash {
        std::size_t operator()(const std::pair<int, int>& p) const {
            return std::hash<int>{}(p.first) ^ (std::hash<int>{}(p.second) << 1);
        }
    };
    std::unordered_map<std::pair<int, int>, std::pair<int, int>, PairHash> virtual_to_physical;
    std::vector<DIRECTION> priority;
    std::vector<bool> is_fold;
    int request_counter = 0;
    std::vector<int> request_counter_per_row;
    
private:
    std::vector<std::pair<int, int>> get_row_positions(int vrow) const;
    static const std::unordered_map<DIRECTION, std::pair<int, int>> step;
};

} // namespace csegfold

