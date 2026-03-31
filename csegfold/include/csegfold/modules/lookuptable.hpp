#pragma once

#include "csegfold/modules/module.hpp"
#include <vector>
#include <unordered_map>
#include <optional>

namespace csegfold {

class LookUpTable : public BaseModule {
public:
    LookUpTable();
    
    virtual std::optional<int> lookup(int i, int b_col_dense);
    int max_updates_per_cycle() const;
    
    void clear_row(int i);
    void clear();
    
    void send_update_request(int i, int b_col_dense, int virtual_pe_col_idx);
    virtual void update_mapping_from_queue();
    void update();

protected:
    virtual void update_stats();
    
    std::vector<std::unordered_map<int, int>> lookup_tables;
    std::vector<std::vector<std::pair<int, int>>> c_col_snapshots;
    std::vector<std::vector<std::pair<int, int>>> update_queue;
    
    int cycle_requests = 0;
    int cycle_updates = 0;
    int max_requests = 0;
    int max_updates = 0;
    int total_requests = 0;
    int total_updates = 0;
};

class ReverseLookUpTable : public LookUpTable {
public:
    ReverseLookUpTable();
    
    std::optional<int> lookup(int i, int b_col_dense) override;
    void update_mapping_from_queue() override;

private:
    void update_mapping_from_queue_round_robin();
    void update_stats() override;
    
    std::vector<std::optional<int>> last_updated_sw;
    int round_robin_updates = 0;
};

void run_lookup_tables(LookUpTable* lookupTableModule);

} // namespace csegfold

