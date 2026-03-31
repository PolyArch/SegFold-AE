#pragma once

#include "csegfold/modules/module.hpp"
#include <vector>
#include <unordered_map>
#include <optional>

namespace csegfold {

enum class PEStatus {
    IDLE,
    LOAD,
    MAC
};

struct PEState {
    std::optional<int> a;  // A value
    std::optional<int> a_m;  // A row index (m)
    std::optional<int> a_k;  // A column index (k)
    struct {
        std::optional<int> val;
        std::optional<int> row;
        std::optional<int> col;
        std::optional<int> n;  // Original output column index (from col_idx_b)
    } b;
    struct {
        int val = 0;
        std::optional<int> m;
        std::optional<int> n;
        std::optional<int> last_k;
        std::optional<int> c_col;
    } c;
    std::optional<int> loadA_cycle;
    std::optional<int> storeC_cycle;
    std::optional<int> mac_cycle;
    PEStatus status = PEStatus::IDLE;
    
    void update(const PEState& other) {
        *this = other;
    }
};

bool pe_active(const PEState& pe);
bool pe_load_done(const PEState& pe, int cycle);
bool pe_mac_done(const PEState& pe, int cycle);
bool pe_stall_c(const PEState& pe, int cycle);

class PEModule : public BaseModule {
public:
    PEModule();
    
    void reset_next();
    
    static PEState idle_pe();
    
    int num_pes() const;
    int num_active_pes() const;
    
    std::vector<std::unordered_map<std::string, int>> b_positions(int cycle = 0) const;
    
    bool next_pe_idle(int i, int j);
    void free_next_pe(int i, int j);
    void free_next_b_val(int i, int j);
    
    int map_col_to_idx(int i, int _j, int c_col) const;
    bool valid_c(int i, int j) const;
    void clear_row(int i);
    void push(int i, int j);
    int log_active_c() const;
    void update_c(int i, int j);

    std::vector<std::vector<PEState>> pe;
    std::vector<std::vector<PEState>> next_pe;
    std::vector<std::vector<bool>> valid_a;
    std::vector<std::vector<bool>> store_c;
    std::vector<std::vector<bool>> load_c;
};

} // namespace csegfold

