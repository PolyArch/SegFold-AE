#pragma once

#include "csegfold/modules/module.hpp"
#include <vector>
#include <unordered_map>
#include <optional>
#include <tuple>

namespace csegfold {

class SPADModule : public BaseModule {
public:
    SPADModule();
    
    // Returns (success, c_data)
    std::pair<bool, std::optional<std::unordered_map<int, std::tuple<int, int, int>>>> 
    load(int i, int c_col);
    
    bool store(int i, int c_col, const std::unordered_map<int, std::tuple<int, int, int>>& c);
    
    void reset_valid();
    void clear_row(int i);
    void clear();
    
    bool is_valid_load(int i) const { 
        return i >= 0 && i < static_cast<int>(available_load_ports.size()) && available_load_ports[i] > 0; 
    }
    bool is_valid_store(int i) const { 
        return i >= 0 && i < static_cast<int>(available_store_ports.size()) && available_store_ports[i] > 0; 
    }

private:
    std::vector<std::unordered_map<int, std::unordered_map<int, std::tuple<int, int, int>>>> lut;
    std::vector<int> available_load_ports;
    std::vector<int> available_store_ports;
};

void run_spad(SPADModule* spadModule);

} // namespace csegfold

