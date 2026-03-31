#pragma once

#include "csegfold/modules/module.hpp"
#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/memory/MemoryBackend.hpp"
#include "csegfold/memory/MemoryRequest.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <memory>

namespace csegfold {

class MemoryController : public BaseModule {
public:
    MemoryController(MatrixLoader* matrix);
    
    MatrixLoader* matrix;
    std::unordered_map<int, std::vector<std::pair<int, int>>> B_csr;
    std::unordered_map<int, std::vector<bool>> B_csr_load;
    
    std::vector<int> active_indices;
    int lptr = 0;  // Index into B_rows_to_load for next row to process
    std::vector<bool> ready_to_evict;
    
    void fill_b_loader_window();
    
    int n_completed_rows = 0;
    std::unordered_map<int, int> b_loader_limit;
    std::unordered_map<int, int> b_loader_offset;
    
    bool is_done = false;
    
    std::vector<int> B_rows_to_load;
    
    void filter_intersections();
    std::pair<bool, bool> check_b_loader_tile(int r);
    void remove_completed_rows(const std::unordered_set<int>& completed_rows);
    bool get_is_done() const;
    bool ready(class SwitchModule* switchModule = nullptr);
    int get_awaiting_b_loads() const;
    
    void start_memory_server();
    void connect_to_memory_server();
    void reset_request_id();
    void send(const std::string& msg);
    void flush_requests();
    void filter_requests();
    
    int get_a_element_pointer(int m, int k) const;
    int get_b_element_pointer(int k, int n) const;
    int get_c_element_pointer(int m, int n) const;
    void submit_load_request(int m, int k, const std::string& type, int i, int j, const std::string& dest, int c_col, int fifo_idx = -1);
    void submit_store_request(int addr, int val);

    // Memory backend interface
    void tick_memory_backend();
    std::vector<MemoryResponse> get_completed_responses();
    bool has_pending_requests() const;

    // Deferred response handling for two-pass FIFO processing
    void set_deferred_responses(std::vector<MemoryResponse>&& responses);
    std::vector<MemoryResponse> get_deferred_responses();

    int cnt;
    bool enable_memory_hierarchy;
    bool enable_filter;

private:
    int next_req_id = 0;

    // Memory backend
    std::unique_ptr<MemoryBackend> memory_backend_;
    std::unordered_map<uint64_t, MemoryRequest> pending_requests_;
    std::vector<MemoryResponse> completed_responses_;
    std::vector<MemoryResponse> deferred_responses_;

    // Helper to create memory backend config from global config
    MemoryBackendConfig create_backend_config() const;
};

} // namespace csegfold

