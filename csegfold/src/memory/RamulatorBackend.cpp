#include "csegfold/memory/RamulatorBackend.hpp"
#include <iostream>

#ifdef CSEGFOLD_HAS_RAMULATOR2
#include "base/factory.h"
#include "base/request.h"
#include "frontend/frontend.h"
#include "memory_system/memory_system.h"
#include <yaml-cpp/yaml.h>
#endif

namespace csegfold {

RamulatorBackend::RamulatorBackend() {
    // Configuration will be set by configure()
}

RamulatorBackend::~RamulatorBackend() {
#ifdef CSEGFOLD_HAS_RAMULATOR2
    // Ramulator2 objects are owned by the Factory; we don't delete them.
    frontend_ = nullptr;
    memory_system_ = nullptr;
#endif
}

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

            // Use Factory to create frontend and memory system
            frontend_ = Ramulator::Factory::create_frontend(dram_config);
            memory_system_ = Ramulator::Factory::create_memory_system(dram_config);

            if (frontend_ && memory_system_) {
                frontend_->connect_memory_system(memory_system_);
                memory_system_->connect_frontend(frontend_);
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

    // Initialize MSHR / request coalescing filter
    enable_filter_ = config.enable_filter && config.enable_outstanding_filter;
    filter_cache_line_size_ = config.filter_cache_line_size;
    if (enable_filter_) {
        std::cerr << "MSHR request filter enabled (cache_line_size="
                  << filter_cache_line_size_ << ")" << std::endl;
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

    // --- Step 1: MSHR filter check (before cache) ---
    if (enable_filter_ && req.type == MemoryRequestType::LOAD) {
        uint64_t aligned = req.address & ~(uint64_t)(filter_cache_line_size_ - 1);
        auto it = outstanding_addr_to_dram_id_.find(aligned);
        if (it != outstanding_addr_to_dram_id_.end()) {
            // Address already has pending DRAM request — coalesce
            coalesced_requests_[it->second].push_back(req);
            stats_.filter_coalesced++;
            return true;
        }
    }

    // --- Step 2: Cache check (only reached if NOT coalesced) ---
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

    // --- Step 3: DRAM submission ---
    uint64_t internal_id = next_dram_req_id_++;
    int cache_latency = cache_->get_l1_latency() + cache_->get_l2_latency();

    PendingDramRequest pending{
        req,
        current_cycle_,
        cache_latency
    };
    pending_dram_requests_[internal_id] = pending;

    // Track in MSHR outstanding map
    if (enable_filter_) {
        uint64_t aligned = req.address & ~(uint64_t)(filter_cache_line_size_ - 1);
        outstanding_addr_to_dram_id_[aligned] = internal_id;
        coalesced_requests_[internal_id] = {req};  // Initialize with first request
    }

#ifdef CSEGFOLD_HAS_RAMULATOR2
    if (initialized_) {
        // Submit to Ramulator2 via the GEM5-style external request interface
        int type_id = (req.type == MemoryRequestType::STORE)
                      ? Ramulator::Request::Type::Write
                      : Ramulator::Request::Type::Read;
        bool accepted = frontend_->receive_external_requests(
            type_id,
            static_cast<Ramulator::Addr_t>(req.address),
            0,  // source_id
            [this, internal_id](Ramulator::Request& /* completed_req */) {
                this->handle_dram_completion(internal_id);
            }
        );
        if (!accepted) {
            // Request rejected by Ramulator2 (internal queue full).
            // Buffer for retry on next tick instead of dropping.
            retry_queue_.push({internal_id, static_cast<uint64_t>(req.address), type_id});
        }
    } else {
#else
    {
#endif
        // Simple DRAM model
        SimpleDramRequest simple{
            internal_id,
            current_cycle_ + config_.dram_latency
        };
        simple_dram_queue_.push(simple);
    }

    stats_.dram_accesses++;
    return true;
}

#ifdef CSEGFOLD_HAS_RAMULATOR2
void RamulatorBackend::drain_retry_queue() {
    int retries = static_cast<int>(retry_queue_.size());
    for (int i = 0; i < retries; ++i) {
        auto& entry = retry_queue_.front();
        uint64_t iid = entry.internal_id;
        bool accepted = frontend_->receive_external_requests(
            entry.type_id,
            static_cast<Ramulator::Addr_t>(entry.address),
            0,
            [this, iid](Ramulator::Request& /* completed_req */) {
                this->handle_dram_completion(iid);
            }
        );
        if (accepted) {
            retry_queue_.pop();
        } else {
            // Still rejected — stop retrying this tick (preserve FIFO order)
            break;
        }
    }
}

void RamulatorBackend::handle_dram_completion(uint64_t internal_req_id) {
    auto it = pending_dram_requests_.find(internal_req_id);
    if (it == pending_dram_requests_.end()) {
        return;
    }

    const auto& pending_entry = it->second;
    const auto& req = pending_entry.request;

    uint64_t total_latency = current_cycle_ - pending_entry.submit_cycle + pending_entry.cache_latency;

    // Fill cache once on load completion
    if (req.type == MemoryRequestType::LOAD) {
        cache_->fill_from_dram(req.address);
    }

    if (enable_filter_) {
        // Fan out response to ALL coalesced requests
        auto coal_it = coalesced_requests_.find(internal_req_id);
        if (coal_it != coalesced_requests_.end()) {
            for (const auto& orig_req : coal_it->second) {
                MemoryResponse response{
                    orig_req.req_id,
                    total_latency,
                    orig_req.data,
                    false,
                    0,
                    orig_req.pe_row,
                    orig_req.pe_col,
                    orig_req.c_col,
                    orig_req.dest,
                    orig_req.matrix,
                    orig_req.fifo_idx
                };
                stats_.total_latency += total_latency;
                stats_.total_requests++;
                completed_responses_.push(response);
            }
            coalesced_requests_.erase(coal_it);
        }
        // Clean outstanding address map
        uint64_t aligned = req.address & ~(uint64_t)(filter_cache_line_size_ - 1);
        outstanding_addr_to_dram_id_.erase(aligned);
    } else {
        // Original single-response path
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
        stats_.total_latency += total_latency;
        stats_.total_requests++;
        completed_responses_.push(response);
    }

    pending_dram_requests_.erase(it);
}
#endif

std::vector<MemoryResponse> RamulatorBackend::tick() {
    std::vector<MemoryResponse> completed;

#ifdef CSEGFOLD_HAS_RAMULATOR2
    if (initialized_) {
        // Retry previously rejected DRAM requests before ticking
        drain_retry_queue();
        // Tick Ramulator2
        memory_system_->tick();
    } else {
#else
    {
#endif
        // Simple DRAM model - check for completions
        while (!simple_dram_queue_.empty() &&
               simple_dram_queue_.front().complete_cycle <= current_cycle_) {
            uint64_t internal_id = simple_dram_queue_.front().req_id;
            simple_dram_queue_.pop();

            auto it = pending_dram_requests_.find(internal_id);
            if (it != pending_dram_requests_.end()) {
                const auto& pending_entry = it->second;
                const auto& req = pending_entry.request;

                uint64_t total_latency = current_cycle_ - pending_entry.submit_cycle + pending_entry.cache_latency;

                if (req.type == MemoryRequestType::LOAD) {
                    cache_->fill_from_dram(req.address);
                }

                if (enable_filter_) {
                    // Fan out response to ALL coalesced requests
                    auto coal_it = coalesced_requests_.find(internal_id);
                    if (coal_it != coalesced_requests_.end()) {
                        for (const auto& orig_req : coal_it->second) {
                            MemoryResponse response{
                                orig_req.req_id,
                                total_latency,
                                orig_req.data,
                                false,
                                0,
                                orig_req.pe_row,
                                orig_req.pe_col,
                                orig_req.c_col,
                                orig_req.dest,
                                orig_req.matrix,
                                orig_req.fifo_idx
                            };
                            stats_.total_latency += total_latency;
                            stats_.total_requests++;
                            completed_responses_.push(response);
                        }
                        coalesced_requests_.erase(coal_it);
                    }
                    uint64_t aligned = req.address & ~(uint64_t)(filter_cache_line_size_ - 1);
                    outstanding_addr_to_dram_id_.erase(aligned);
                } else {
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
                    stats_.total_latency += total_latency;
                    stats_.total_requests++;
                    completed_responses_.push(response);
                }

                pending_dram_requests_.erase(it);
            }
        }
    }

    // Collect all completed responses
    while (!completed_responses_.empty()) {
        completed.push_back(completed_responses_.front());
        completed_responses_.pop();
    }

    current_cycle_++;
    return completed;
}

} // namespace csegfold
