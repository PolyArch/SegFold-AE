/*
 * C FFI implementation wrapping SegFold's MemoryBackend for external consumers.
 *
 * Creates a backend with L1/L2 caches bypassed (size_kb=0) so only the
 * DRAM timing layer (Ramulator2 or fallback) is active.
 */

#include "csegfold/memory/csegfold_ffi.h"
#include "csegfold/memory/MemoryBackend.hpp"

#include <memory>
#include <cstring>
#include <unordered_map>
#include <vector>

struct DramBackendHandle {
    std::unique_ptr<csegfold::MemoryBackend> backend;
    // Map from external req_id -> tag (so we can echo it back in responses)
    std::unordered_map<uint64_t, uint64_t> req_tags;
};

extern "C" {

DramBackendHandle* dram_backend_create(const DramBackendConfig* config) {
    if (!config) return nullptr;

    csegfold::MemoryBackendConfig cfg;

    // Set backend type
    if (config->backend_type) {
        cfg.type = config->backend_type;
    } else {
        cfg.type = "ramulator2";
    }

    // Set DRAM config file
    if (config->dram_config_file) {
        cfg.dram_config_file = config->dram_config_file;
    }

    // Bypass L1/L2 caches — spada-sim has its own fiber cache
    cfg.l1_size_kb = 0;
    cfg.l1_associativity = 1;
    cfg.l1_line_size = 64;
    cfg.l1_latency = 0;

    cfg.l2_size_kb = 0;
    cfg.l2_associativity = 1;
    cfg.l2_line_size = 64;
    cfg.l2_latency = 0;

    // Fallback latency for ideal/simple DRAM model
    cfg.ideal_latency = config->fallback_latency > 0 ? config->fallback_latency : 100;
    cfg.dram_latency = cfg.ideal_latency;

    auto handle = new (std::nothrow) DramBackendHandle();
    if (!handle) return nullptr;

    handle->backend = csegfold::MemoryBackend::create(cfg);
    if (!handle->backend) {
        delete handle;
        return nullptr;
    }

    return handle;
}

void dram_backend_destroy(DramBackendHandle* handle) {
    delete handle;
}

int dram_submit_request(DramBackendHandle* handle, const DramRequest* req) {
    if (!handle || !req) return 0;

    csegfold::MemoryRequest mem_req{};
    mem_req.req_id = req->req_id;
    mem_req.address = req->address;
    mem_req.type = req->is_write ? csegfold::MemoryRequestType::STORE
                                 : csegfold::MemoryRequestType::LOAD;
    mem_req.matrix = csegfold::MatrixType::B;  // Default — not relevant for timing
    mem_req.data = 0;
    mem_req.pe_row = req->pe_id;
    mem_req.pe_col = 0;
    mem_req.c_col = 0;
    mem_req.dest = "pe";
    mem_req.submit_cycle = handle->backend->get_cycle();

    // Store the tag for echoing in response
    handle->req_tags[req->req_id] = req->tag;

    return handle->backend->submit_request(mem_req) ? 1 : 0;
}

int dram_tick(DramBackendHandle* handle, DramResponse* responses, int max_responses) {
    if (!handle) return 0;

    auto completed = handle->backend->tick();

    int count = 0;
    for (const auto& resp : completed) {
        if (count >= max_responses) break;

        responses[count].req_id = resp.req_id;
        responses[count].latency = resp.latency;

        // Look up and echo the tag
        auto it = handle->req_tags.find(resp.req_id);
        if (it != handle->req_tags.end()) {
            responses[count].tag = it->second;
            handle->req_tags.erase(it);
        } else {
            responses[count].tag = 0;
        }

        count++;
    }

    return count;
}

int dram_can_accept(const DramBackendHandle* handle) {
    if (!handle) return 0;
    return handle->backend->can_accept() ? 1 : 0;
}

uint64_t dram_get_cycle(const DramBackendHandle* handle) {
    if (!handle) return 0;
    return handle->backend->get_cycle();
}

} // extern "C"
