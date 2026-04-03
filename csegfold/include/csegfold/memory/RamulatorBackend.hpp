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
    int max_pending_ = 65536;  // Must be large enough to never drop requests

    // Fallback: simple DRAM timing model (used when Ramulator2 not compiled
    // in, or when Ramulator2 initialization fails at runtime)
    struct SimpleDramRequest {
        uint64_t req_id;
        uint64_t complete_cycle;
    };
    std::queue<SimpleDramRequest> simple_dram_queue_;

#ifdef CSEGFOLD_HAS_RAMULATOR2
    Ramulator::IFrontEnd* frontend_ = nullptr;
    Ramulator::IMemorySystem* memory_system_ = nullptr;

    void handle_dram_completion(uint64_t internal_req_id);

    // Retry buffer for requests rejected by Ramulator2's internal queue
    struct RetryEntry {
        uint64_t internal_id;
        uint64_t address;
        int type_id;  // Read or Write
    };
    std::queue<RetryEntry> retry_queue_;
    void drain_retry_queue();
#endif

    bool initialized_ = false;

    // MSHR / Request coalescing filter
    bool enable_filter_ = false;
    int filter_cache_line_size_ = 32;
    // Maps aligned_addr → internal_dram_req_id that was sent to DRAM
    std::unordered_map<uint64_t, uint64_t> outstanding_addr_to_dram_id_;
    // Maps internal_dram_req_id → list of ALL original MemoryRequests waiting on this DRAM fetch
    std::unordered_map<uint64_t, std::vector<MemoryRequest>> coalesced_requests_;
};

} // namespace csegfold
