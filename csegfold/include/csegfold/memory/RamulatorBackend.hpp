#pragma once

#include "csegfold/memory/MemoryBackend.hpp"
#include "csegfold/memory/CacheModel.hpp"
#include <memory>
#include <unordered_map>
#include <queue>

#ifdef CSEGFOLD_HAS_RAMULATOR2
// Forward declarations for Ramulator2 types
namespace Ramulator {
class IFrontEnd;
class IMemorySystem;
}
#endif

namespace csegfold {

// Memory backend using Ramulator2 for DRAM timing simulation.
// Integrates with CacheModel for L1/L2 simulation.
class RamulatorBackend : public MemoryBackend {
public:
    RamulatorBackend();
    ~RamulatorBackend() override;

    bool submit_request(const MemoryRequest& req) override;
    std::vector<MemoryResponse> tick() override;
    bool can_accept() const override;
    void configure(const MemoryBackendConfig& config) override;
    const MemoryStats& get_stats() const override { return stats_; }
    void reset_stats() override;
    uint64_t get_cycle() const override { return current_cycle_; }

private:
    struct PendingDramRequest {
        MemoryRequest request;
        uint64_t submit_cycle;
        int cache_latency;  // L1 + L2 miss latency before DRAM
    };

    std::unique_ptr<CacheModel> cache_;
    std::unordered_map<uint64_t, PendingDramRequest> pending_dram_requests_;
    std::queue<MemoryResponse> completed_responses_;
    uint64_t next_dram_req_id_ = 0;
    int max_pending_ = 64;

#ifdef CSEGFOLD_HAS_RAMULATOR2
    std::unique_ptr<Ramulator::IFrontEnd> frontend_;
    std::unique_ptr<Ramulator::IMemorySystem> memory_system_;

    void handle_dram_completion(uint64_t internal_req_id);
#else
    // Fallback: simple DRAM timing model when Ramulator2 not available
    struct SimpleDramRequest {
        uint64_t req_id;
        uint64_t complete_cycle;
    };
    std::queue<SimpleDramRequest> simple_dram_queue_;
#endif

    bool initialized_ = false;
};

} // namespace csegfold
