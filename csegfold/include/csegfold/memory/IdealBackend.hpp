#pragma once

#include "csegfold/memory/MemoryBackend.hpp"
#include "csegfold/memory/CacheModel.hpp"
#include <queue>
#include <memory>

namespace csegfold {

// Fixed-latency memory backend for testing.
// Uses cache model but simulates DRAM with fixed latency.
class IdealBackend : public MemoryBackend {
public:
    IdealBackend();
    ~IdealBackend() override = default;

    bool submit_request(const MemoryRequest& req) override;
    std::vector<MemoryResponse> tick() override;
    bool can_accept() const override;
    void configure(const MemoryBackendConfig& config) override;
    const MemoryStats& get_stats() const override { return stats_; }
    void reset_stats() override { stats_.reset(); }
    uint64_t get_cycle() const override { return current_cycle_; }

private:
    struct PendingRequest {
        MemoryRequest request;
        uint64_t complete_cycle;
        bool cache_hit;
        int cache_level;
    };

    std::unique_ptr<CacheModel> cache_;
    std::queue<PendingRequest> pending_requests_;
    int max_pending_ = 1024;  // Maximum outstanding requests (increased to avoid dropping)
};

} // namespace csegfold
