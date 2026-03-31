#include "csegfold/memory/RamulatorBackend.hpp"
#include <iostream>

#ifdef CSEGFOLD_HAS_RAMULATOR2
#include "ramulator2/src/frontend/frontend.h"
#include "ramulator2/src/memory_system/memory_system.h"
#include <yaml-cpp/yaml.h>
#endif

namespace csegfold {

RamulatorBackend::RamulatorBackend() {
    // Configuration will be set by configure()
}

RamulatorBackend::~RamulatorBackend() = default;

void RamulatorBackend::configure(const MemoryBackendConfig& config) {
    config_ = config;

    // Initialize cache model
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

#ifdef CSEGFOLD_HAS_RAMULATOR2
    // Initialize Ramulator2 with DRAM config file
    if (!config.dram_config_file.empty()) {
        try {
            YAML::Node dram_config = YAML::LoadFile(config.dram_config_file);

            // Create memory system from config
            // Note: This is a simplified initialization. The actual Ramulator2 API
            // may vary depending on the version.
            frontend_ = Ramulator::IFrontEnd::create_frontend(dram_config);
            memory_system_ = Ramulator::IMemorySystem::create_memory_system(dram_config);

            if (frontend_ && memory_system_) {
                frontend_->connect_memory_system(memory_system_.get());
                initialized_ = true;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to initialize Ramulator2: " << e.what() << std::endl;
            std::cerr << "Falling back to simple DRAM model." << std::endl;
        }
    }
#endif

    if (!initialized_) {
        // Fallback mode - will use simple DRAM timing
        std::cerr << "Using simple DRAM timing model (latency="
                  << config.dram_latency << " cycles)" << std::endl;
    }
}

void RamulatorBackend::reset_stats() {
    stats_.reset();
    if (cache_) {
        cache_->reset_stats();
    }
}

bool RamulatorBackend::can_accept() const {
    return static_cast<int>(pending_dram_requests_.size()) < max_pending_;
}

bool RamulatorBackend::submit_request(const MemoryRequest& req) {
    if (!can_accept()) {
        return false;
    }

    // Check cache for loads
    if (req.type == MemoryRequestType::LOAD) {
        auto result = cache_->access(req.address);

        if (result.hit) {
            // Cache hit - create immediate response
            MemoryResponse response{
                req.req_id,
                static_cast<uint64_t>(result.latency),
                req.data,
                true,
                result.cache_level,
                req.pe_row,
                req.pe_col,
                req.c_col,
                req.dest,
                req.matrix,
                req.fifo_idx
            };

            if (result.cache_level == 1) {
                stats_.l1_hits++;
            } else {
                stats_.l2_hits++;
            }

            stats_.total_latency += result.latency;
            stats_.total_requests++;

            completed_responses_.push(response);
            return true;
        }

        // Cache miss - need DRAM access
        stats_.l1_misses++;
        stats_.l2_misses++;
    }

    // Submit to DRAM
    uint64_t internal_id = next_dram_req_id_++;
    int cache_latency = cache_->get_l1_latency() + cache_->get_l2_latency();

    PendingDramRequest pending{
        req,
        current_cycle_,
        cache_latency
    };
    pending_dram_requests_[internal_id] = pending;

#ifdef CSEGFOLD_HAS_RAMULATOR2
    if (initialized_) {
        // Submit to Ramulator2
        bool is_write = (req.type == MemoryRequestType::STORE);
        frontend_->receive_request(
            req.address,
            is_write,
            [this, internal_id](bool) {
                this->handle_dram_completion(internal_id);
            }
        );
    } else {
#endif
        // Simple DRAM model
        SimpleDramRequest simple{
            internal_id,
            current_cycle_ + config_.dram_latency
        };
        simple_dram_queue_.push(simple);
#ifdef CSEGFOLD_HAS_RAMULATOR2
    }
#endif

    stats_.dram_accesses++;
    return true;
}

#ifdef CSEGFOLD_HAS_RAMULATOR2
void RamulatorBackend::handle_dram_completion(uint64_t internal_req_id) {
    auto it = pending_dram_requests_.find(internal_req_id);
    if (it == pending_dram_requests_.end()) {
        return;
    }

    const auto& pending = it->second;
    const auto& req = pending.request;

    uint64_t total_latency = current_cycle_ - pending.submit_cycle + pending.cache_latency;

    MemoryResponse response{
        req.req_id,
        total_latency,
        req.data,
        false,  // Not a cache hit
        0,      // DRAM level
        req.pe_row,
        req.pe_col,
        req.c_col,
        req.dest,
        req.matrix,
        req.fifo_idx
    };

    // Fill cache on load completion
    if (req.type == MemoryRequestType::LOAD) {
        cache_->fill_from_dram(req.address);
    }

    stats_.total_latency += total_latency;
    stats_.total_requests++;

    completed_responses_.push(response);
    pending_dram_requests_.erase(it);
}
#endif

std::vector<MemoryResponse> RamulatorBackend::tick() {
    std::vector<MemoryResponse> completed;

#ifdef CSEGFOLD_HAS_RAMULATOR2
    if (initialized_) {
        // Tick Ramulator2
        memory_system_->tick();
    } else {
#endif
        // Simple DRAM model - check for completions
        while (!simple_dram_queue_.empty() &&
               simple_dram_queue_.front().complete_cycle <= current_cycle_) {
            uint64_t internal_id = simple_dram_queue_.front().req_id;
            simple_dram_queue_.pop();

            auto it = pending_dram_requests_.find(internal_id);
            if (it != pending_dram_requests_.end()) {
                const auto& pending = it->second;
                const auto& req = pending.request;

                uint64_t total_latency = current_cycle_ - pending.submit_cycle + pending.cache_latency;

                MemoryResponse response{
                    req.req_id,
                    total_latency,
                    req.data,
                    false,
                    0,
                    req.pe_row,
                    req.pe_col,
                    req.c_col,
                    req.dest,
                    req.matrix,
                    req.fifo_idx
                };

                if (req.type == MemoryRequestType::LOAD) {
                    cache_->fill_from_dram(req.address);
                }

                stats_.total_latency += total_latency;
                stats_.total_requests++;

                completed_responses_.push(response);
                pending_dram_requests_.erase(it);
            }
        }
#ifdef CSEGFOLD_HAS_RAMULATOR2
    }
#endif

    // Collect all completed responses
    while (!completed_responses_.empty()) {
        completed.push_back(completed_responses_.front());
        completed_responses_.pop();
    }

    current_cycle_++;
    return completed;
}

} // namespace csegfold
