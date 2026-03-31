#include "csegfold/memory/IdealBackend.hpp"

namespace csegfold {

IdealBackend::IdealBackend() {
    // Default configuration will be set by configure()
}

void IdealBackend::configure(const MemoryBackendConfig& config) {
    config_ = config;

    CacheConfig l1_config{
        config.l1_size_kb,
        config.l1_associativity,
        config.l1_line_size,
        config.l1_latency
    };

    CacheConfig l2_config{
        config.l2_size_kb,
        config.l2_associativity,
        config.l2_line_size,
        config.l2_latency
    };

    cache_ = std::make_unique<CacheModel>(l1_config, l2_config);
}

bool IdealBackend::can_accept() const {
    return static_cast<int>(pending_requests_.size()) < max_pending_;
}

bool IdealBackend::submit_request(const MemoryRequest& req) {
    if (!can_accept()) {
        return false;
    }

    // Check cache for loads
    int latency = 0;
    bool cache_hit = false;
    int cache_level = 0;

    if (req.type == MemoryRequestType::LOAD) {
        auto result = cache_->access(req.address);
        cache_hit = result.hit;
        cache_level = result.cache_level;
        latency = result.latency;

        if (!cache_hit) {
            // DRAM access needed
            latency += config_.ideal_latency;
        }
    } else {
        // Store: write-through, just add DRAM latency
        latency = config_.ideal_latency;
    }

    PendingRequest pending{
        req,
        current_cycle_ + latency,
        cache_hit,
        cache_level
    };

    pending_requests_.push(pending);

    // Update stats
    if (req.type == MemoryRequestType::LOAD) {
        if (cache_level == 1) {
            stats_.l1_hits++;
        } else if (cache_level == 2) {
            stats_.l2_hits++;
        } else {
            stats_.l1_misses++;
            stats_.l2_misses++;
            stats_.dram_accesses++;
        }
    } else {
        stats_.dram_accesses++;
    }

    return true;
}

std::vector<MemoryResponse> IdealBackend::tick() {
    std::vector<MemoryResponse> completed;

    while (!pending_requests_.empty() &&
           pending_requests_.front().complete_cycle <= current_cycle_) {
        const auto& pending = pending_requests_.front();
        const auto& req = pending.request;

        MemoryResponse response{
            req.req_id,
            current_cycle_ - req.submit_cycle,
            req.data,
            pending.cache_hit,
            pending.cache_level,
            req.pe_row,
            req.pe_col,
            req.c_col,
            req.dest,
            req.matrix,
            req.fifo_idx
        };

        // Fill cache on DRAM completion for loads
        if (req.type == MemoryRequestType::LOAD && !pending.cache_hit) {
            cache_->fill_from_dram(req.address);
        }

        // Update latency stats
        stats_.total_latency += response.latency;
        stats_.total_requests++;

        // Invoke callback if set
        if (req.callback) {
            req.callback(response);
        }

        completed.push_back(response);
        pending_requests_.pop();
    }

    current_cycle_++;
    return completed;
}

} // namespace csegfold
