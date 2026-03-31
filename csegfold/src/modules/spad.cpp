#include "csegfold/modules/spad.hpp"
#include <cassert>

namespace csegfold {

SPADModule::SPADModule() : BaseModule() {
    lut.resize(vrows());
    reset_valid();
}

std::pair<bool, std::optional<std::unordered_map<int, std::tuple<int, int, int>>>> 
SPADModule::load(int i, int c_col) {
    if (i < 0 || i >= vrows() || available_load_ports[i] <= 0) {
        return {false, std::nullopt};
    }
    
    available_load_ports[i]--;
    
    auto it = lut[i].find(c_col);
    if (it != lut[i].end()) {
        auto c = it->second;
        lut[i].erase(it);
        stats.spad_load_hits++;
        return {true, c};
    } else {
        stats.spad_load_misses++;
        return {true, std::nullopt};
    }
}

bool SPADModule::store(int i, int c_col, const std::unordered_map<int, std::tuple<int, int, int>>& c) {
    if (i < 0 || i >= vrows() || available_store_ports[i] <= 0) {
        return false;
    }
    
    available_store_ports[i]--;
    
    lut[i][c_col] = c;
    stats.spad_stores++;
    return true;
}

void SPADModule::reset_valid() {
    available_load_ports = std::vector<int>(vrows(), cfg.spad_load_ports_per_bank);
    available_store_ports = std::vector<int>(vrows(), cfg.spad_store_ports_per_bank);
}

void SPADModule::clear_row(int i) {
    lut[i].clear();
    assert(lut[i].empty());
}

void SPADModule::clear() {
    for (int i = 0; i < vrows(); ++i) {
        clear_row(i);
    }
}

void run_spad(SPADModule* spadModule) {
    spadModule->reset_valid();
}

} // namespace csegfold

