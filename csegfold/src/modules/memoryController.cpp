#include "csegfold/modules/memoryController.hpp"
#include "csegfold/modules/switch.hpp"
#include <algorithm>
#include <unordered_set>
#include <sstream>

namespace csegfold {

MemoryController::MemoryController(MatrixLoader* matrix) : BaseModule(), matrix(matrix), enable_memory_hierarchy(false), enable_filter(false) {
    B_csr = matrix->B_csr;
    B_csr_load = matrix->B_csr_load;
    filter_intersections();
    active_indices.clear();
    lptr = 0;
    ready_to_evict = std::vector<bool>(prows(), false);
    fill_b_loader_window();

    // Initialize per-PE-row tile tracking for tile pipelining
    pe_row_tile_id = std::vector<int>(prows(), 0);
    if (cfg.enable_tile_pipeline && !active_indices.empty()) {
        int first_tile = active_indices[0] / matrix->K;
        for (int i = 0; i < prows(); ++i) {
            pe_row_tile_id[i] = first_tile;
        }
    }

    n_completed_rows = 0;
    
    for (const auto& [r, l] : B_csr) {
        b_loader_limit[r] = static_cast<int>(l.size());
    }
    
    if (cfg.enable_partial_b_load) {
        for (const auto& [r, _] : B_csr) {
            b_loader_offset[r] = 0;
        }
    }
    
    enable_memory_hierarchy = cfg.enable_memory_hierarchy;
    enable_filter = cfg.enable_filter;
    
    if (enable_memory_hierarchy) {
        reset_request_id();
        start_memory_server();
        connect_to_memory_server();
    }
    
    cnt = cfg.II;
    is_done = get_is_done();
}

void MemoryController::filter_intersections() {
    B_rows_to_load.clear();
    std::vector<std::pair<int, int>> rows_with_demand;  // (row_id, demand_count)

    b_row_demand_hist.clear();
    for (const auto& [r, l] : B_csr) {
        if (l.empty()) continue;
        if (!cfg.enable_filter_intersection) {
            // No filtering: load all non-empty B rows, demand = prows (all PE rows)
            rows_with_demand.push_back({r, prows()});
            b_row_demand_hist[prows()]++;
            continue;
        }
        int demand = 0;
        for (int i = 0; i < prows(); ++i) {
            if (matrix->intersect_bc(r, i)) demand++;
        }
        b_row_demand_hist[demand]++;
        if (demand > 0) {
            rows_with_demand.push_back({r, demand});
        }
    }

    if (cfg.b_row_scheduling == "demand") {
        // Sort by tile block (preserve tile eviction order), then demand desc within block
        int K = matrix->K;
        std::sort(rows_with_demand.begin(), rows_with_demand.end(),
            [K](const auto& a, const auto& b) {
                int ba = a.first / K, bb = b.first / K;
                if (ba != bb) return ba < bb;
                return a.second > b.second;
            });
    } else {
        // Default: sort by row ID
        std::sort(rows_with_demand.begin(), rows_with_demand.end());
    }

    for (const auto& [r, d] : rows_with_demand) {
        B_rows_to_load.push_back(r);
    }

    if (cfg.verbose) {
        std::ostringstream oss;
        oss << "[filter_intersections] Found " << B_rows_to_load.size()
            << " B rows with intersections (out of " << B_csr.size() << " total B rows)"
            << ", scheduling=" << cfg.b_row_scheduling;
        log->info(oss.str());
    }
    // Always log for debugging
    {
        std::ostringstream oss;
        oss << "[filter_intersections] B_csr has " << B_csr.size() << " rows, B_rows_to_load has " << B_rows_to_load.size() << " rows";
        log->info(oss.str());
    }
}

std::pair<bool, bool> MemoryController::check_b_loader_tile(int r) {
    if (!cfg.enable_tile_eviction) {
        return {true, false};
    } else {
        int max_r = -1;
        for (int idx : active_indices) {
            if (idx > max_r) {
                max_r = idx;
            }
        }
        if (max_r == -1) {
            return {true, true};
        } else {
            if (matrix->b_is_same_block(r, max_r)) {
                return {true, false};
            } else if (cfg.enable_tile_pipeline) {
                // Allow next tile with eviction, limited to 2-tile straddling
                std::unordered_set<int> tile_blocks;
                for (int idx : active_indices) {
                    tile_blocks.insert(idx / matrix->K);
                }
                if (tile_blocks.size() < 2) {
                    return {true, true};
                }
                return {false, false};
            } else {
                return {false, false};
            }
        }
    }
}

void MemoryController::fill_b_loader_window() {
    while (static_cast<int>(active_indices.size()) < cfg.b_loader_window_size && 
           lptr < static_cast<int>(B_rows_to_load.size())) {
        int r = B_rows_to_load[lptr];
        auto [is_append, is_evict] = check_b_loader_tile(r);
        
        if (is_evict) {
            for (int i = 0; i < prows(); ++i) {
                ready_to_evict[i] = true;
            }
        }
        if (is_append) {
            lptr++;
            active_indices.push_back(r);
        } else {
            break;
        }
    }
}

void MemoryController::remove_completed_rows(const std::unordered_set<int>& completed_rows) {
    if (!completed_rows.empty() && debug()) {
        std::ostringstream oss;
        oss << "[remove_completed_rows] Removing " << completed_rows.size() << " completed rows: ";
        for (int r : completed_rows) {
            oss << r << " ";
        }
        log->debug(oss.str());
    }
    n_completed_rows += static_cast<int>(completed_rows.size());
    active_indices.erase(
        std::remove_if(active_indices.begin(), active_indices.end(),
            [&completed_rows](int r) { return completed_rows.find(r) != completed_rows.end(); }),
        active_indices.end()
    );

    fill_b_loader_window();
}

bool MemoryController::get_is_done() const {
    return active_indices.empty() && lptr >= static_cast<int>(B_rows_to_load.size());
}

bool MemoryController::ready(SwitchModule* switchModule) {
    if (switchModule && switchModule->has_remaining_c_upd()) {
        if (cnt > 0) {
            cnt--;
        }
        return false;
    }
    
    if (cnt > 0) {
        cnt--;
        return false;
    }
    
    cnt = cfg.II;
    return true;
}

int MemoryController::get_awaiting_b_loads() const {
    int _awaiting_b_loads = 0;
    for (int i = 0; i < prows(); ++i) {
        int awaiting_b_loads_at_row = 0;
        for (int r : active_indices) {
            int c_start = cfg.enable_partial_b_load ? b_loader_offset.at(r) : 0;
            int c_end = b_loader_limit.at(r);
            if (!matrix->intersect_bc(r, i)) {
                continue;
            }
            awaiting_b_loads_at_row = std::max(awaiting_b_loads_at_row, c_end - c_start);
        }
        _awaiting_b_loads += awaiting_b_loads_at_row;
    }
    return _awaiting_b_loads;
}

MemoryBackendConfig MemoryController::create_backend_config() const {
    MemoryBackendConfig backend_cfg;
    backend_cfg.type = cfg.memory_backend_type;
    backend_cfg.dram_config_file = cfg.dram_config_file;
    backend_cfg.l1_size_kb = cfg.l1_size_kb;
    backend_cfg.l1_associativity = cfg.l1_associativity;
    backend_cfg.l1_line_size = cfg.l1_line_size;
    backend_cfg.l1_latency = cfg.l1_latency;
    backend_cfg.l2_size_kb = cfg.l2_size_kb;
    backend_cfg.l2_associativity = cfg.l2_associativity;
    backend_cfg.l2_line_size = cfg.l2_line_size;
    backend_cfg.l2_latency = cfg.l2_latency;
    backend_cfg.ideal_latency = cfg.ideal_dram_latency;
    backend_cfg.dram_latency = cfg.ideal_dram_latency;
    backend_cfg.enable_filter = cfg.enable_filter;
    backend_cfg.enable_outstanding_filter = cfg.enable_outstanding_filter;
    backend_cfg.filter_cache_line_size = cfg.cache_line_size;
    return backend_cfg;
}

void MemoryController::start_memory_server() {
    if (!enable_memory_hierarchy) {
        return;
    }

    // Initialize memory backend
    auto backend_cfg = create_backend_config();
    memory_backend_ = MemoryBackend::create(backend_cfg);

    if (cfg.verbose) {
        std::ostringstream oss;
        oss << "[MemoryController] Initialized memory backend: " << cfg.memory_backend_type;
        log->info(oss.str());
    }
}

void MemoryController::connect_to_memory_server() {
    // Memory hierarchy simulation not yet implemented
}

void MemoryController::reset_request_id() {
    if (!enable_memory_hierarchy) {
        return;
    }
    next_req_id = 0;
}

void MemoryController::send(const std::string& /* msg */) {
    // Memory hierarchy simulation not yet implemented
}

void MemoryController::flush_requests() {
    if (!enable_memory_hierarchy) {
        return;
    }
}

void MemoryController::filter_requests() {
    if (!enable_memory_hierarchy) {
        return;
    }
}

int MemoryController::get_a_element_pointer(int m, int k) const {
    int offset = matrix->A_nnz_offset_get(
        cfg.enable_a_csc ? k : m,
        cfg.enable_a_csc ? m : k
    );
    return cfg.a_pointer_offset + offset * cfg.element_size;
}

int MemoryController::get_b_element_pointer(int k, int n) const {
    int offset = matrix->B_nnz_offset_get(k, n);
    return cfg.b_pointer_offset + offset * cfg.element_size;
}

int MemoryController::get_c_element_pointer(int m, int n) const {
    int64_t key = static_cast<int64_t>(m) * matrix->N + n;
    auto it = matrix->C_nnz_offset.find(key);
    int offset = (it != matrix->C_nnz_offset.end()) ? it->second : 0;
    return cfg.c_pointer_offset + offset * cfg.element_size;
}

void MemoryController::submit_load_request(int m, int k, const std::string& type,
                                           int i, int j, const std::string& dest, int c_col, int fifo_idx) {
    if (!enable_memory_hierarchy || !memory_backend_) {
        return;
    }

    // Determine address based on type
    uint64_t address = 0;
    MatrixType matrix_type = MatrixType::A;

    if (type == "a" || type == "A") {
        address = get_a_element_pointer(m, k);
        matrix_type = MatrixType::A;
    } else if (type == "b" || type == "B") {
        address = get_b_element_pointer(m, k);  // B(k,n): m=orig_row=k-dim, k=orig_col=n-dim
        matrix_type = MatrixType::B;
    } else if (type == "c" || type == "C") {
        address = get_c_element_pointer(m, k);
        matrix_type = MatrixType::C;
    }

    MemoryRequest req;
    req.req_id = next_req_id++;
    req.address = address;
    req.type = MemoryRequestType::LOAD;
    req.matrix = matrix_type;
    req.data = 0;
    req.pe_row = i;
    req.pe_col = j;
    req.c_col = c_col;
    req.dest = dest;
    req.fifo_idx = fifo_idx;
    req.submit_cycle = static_cast<uint64_t>(cycle());

    assert(memory_backend_->can_accept() &&
           "Memory backend cannot accept request - increase max_pending_ or add backpressure");
    memory_backend_->submit_request(req);
    pending_requests_[req.req_id] = req;
}

void MemoryController::submit_store_request(int addr, int val) {
    if (!enable_memory_hierarchy || !memory_backend_) {
        return;
    }

    MemoryRequest req;
    req.req_id = next_req_id++;
    req.address = static_cast<uint64_t>(addr);
    req.type = MemoryRequestType::STORE;
    req.matrix = MatrixType::C;
    req.data = val;
    req.pe_row = 0;
    req.pe_col = 0;
    req.c_col = 0;
    req.dest = "";
    req.submit_cycle = static_cast<uint64_t>(cycle());

    if (memory_backend_->can_accept()) {
        memory_backend_->submit_request(req);
        pending_requests_[req.req_id] = req;
    }
}

void MemoryController::tick_memory_backend() {
    if (!enable_memory_hierarchy || !memory_backend_) {
        return;
    }

    auto responses = memory_backend_->tick();

    // Process completed responses and store them for later retrieval
    for (const auto& resp : responses) {
        pending_requests_.erase(resp.req_id);
        completed_responses_.push_back(resp);

        // Update statistics based on matrix type
        if (resp.cache_hit) {
            switch (resp.matrix) {
                case MatrixType::A: stats.a_cache_loads++; break;
                case MatrixType::B: stats.b_cache_loads++; break;
                case MatrixType::C: stats.c_cache_loads++; break;
            }
        } else {
            switch (resp.matrix) {
                case MatrixType::A: stats.a_mem_loads++; break;
                case MatrixType::B: stats.b_mem_loads++; break;
                case MatrixType::C: stats.c_mem_loads++; break;
            }
        }
    }

    // Update memory stats from backend
    const auto& mem_stats = memory_backend_->get_stats();
    stats.l1_hits = mem_stats.l1_hits;
    stats.l1_misses = mem_stats.l1_misses;
    stats.l2_hits = mem_stats.l2_hits;
    stats.l2_misses = mem_stats.l2_misses;
    stats.dram_accesses = mem_stats.dram_accesses;
    stats.filter_coalesced = mem_stats.filter_coalesced;
    stats.avg_memory_latency = mem_stats.avg_memory_latency();
}

std::vector<MemoryResponse> MemoryController::get_completed_responses() {
    if (!enable_memory_hierarchy || !memory_backend_) {
        return {};
    }
    // Return and clear the completed responses
    std::vector<MemoryResponse> result = std::move(completed_responses_);
    completed_responses_.clear();
    return result;
}

bool MemoryController::has_pending_requests() const {
    return !pending_requests_.empty();
}

bool MemoryController::memory_can_accept() const {
    if (!enable_memory_hierarchy || !memory_backend_) {
        return true;
    }
    return memory_backend_->can_accept();
}

void MemoryController::set_deferred_responses(std::vector<MemoryResponse>&& responses) {
    deferred_responses_ = std::move(responses);
}

std::vector<MemoryResponse> MemoryController::get_deferred_responses() {
    std::vector<MemoryResponse> result = std::move(deferred_responses_);
    deferred_responses_.clear();
    return result;
}

} // namespace csegfold

