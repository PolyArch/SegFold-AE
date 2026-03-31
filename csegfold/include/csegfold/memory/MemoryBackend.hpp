#pragma once

#include "csegfold/memory/MemoryRequest.hpp"
#include <memory>
#include <vector>
#include <string>

namespace csegfold {

struct MemoryBackendConfig {
    // Backend type
    std::string type = "ideal";  // "ideal" or "ramulator2"
    std::string dram_config_file;

    // L1 Cache parameters
    int l1_size_kb = 64;
    int l1_associativity = 8;
    int l1_line_size = 64;
    int l1_latency = 1;

    // L2 Cache parameters
    int l2_size_kb = 512;
    int l2_associativity = 16;
    int l2_line_size = 64;
    int l2_latency = 10;

    // Ideal backend parameters
    int ideal_latency = 100;  // Fixed latency for ideal backend

    // DRAM parameters (used when ramulator2 not available)
    int dram_latency = 100;
};

struct MemoryStats {
    uint64_t l1_hits = 0;
    uint64_t l1_misses = 0;
    uint64_t l2_hits = 0;
    uint64_t l2_misses = 0;
    uint64_t dram_accesses = 0;
    uint64_t total_latency = 0;
    uint64_t total_requests = 0;

    double avg_memory_latency() const {
        return total_requests > 0 ?
               static_cast<double>(total_latency) / total_requests : 0.0;
    }

    void reset() {
        l1_hits = 0;
        l1_misses = 0;
        l2_hits = 0;
        l2_misses = 0;
        dram_accesses = 0;
        total_latency = 0;
        total_requests = 0;
    }
};

class MemoryBackend {
public:
    virtual ~MemoryBackend() = default;

    // Submit a memory request. Returns true if accepted, false if queue full.
    virtual bool submit_request(const MemoryRequest& req) = 0;

    // Advance simulation by one cycle. Returns completed responses.
    virtual std::vector<MemoryResponse> tick() = 0;

    // Check if backend can accept more requests
    virtual bool can_accept() const = 0;

    // Configure the backend
    virtual void configure(const MemoryBackendConfig& config) = 0;

    // Get current statistics
    virtual const MemoryStats& get_stats() const = 0;

    // Reset statistics
    virtual void reset_stats() = 0;

    // Get current cycle
    virtual uint64_t get_cycle() const = 0;

    // Factory method to create backends
    static std::unique_ptr<MemoryBackend> create(const MemoryBackendConfig& config);

protected:
    MemoryBackendConfig config_;
    MemoryStats stats_;
    uint64_t current_cycle_ = 0;
};

} // namespace csegfold
