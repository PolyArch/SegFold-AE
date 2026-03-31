#include <iostream>
#include <cassert>
#include "csegfold/memory/MemoryBackend.hpp"
#include "csegfold/memory/CacheModel.hpp"
#include "csegfold/memory/IdealBackend.hpp"

using namespace csegfold;

void test_cache_model() {
    std::cout << "Testing CacheModel..." << std::endl;

    CacheConfig l1_config{64, 8, 64, 1};   // 64KB, 8-way, 64B lines, 1 cycle latency
    CacheConfig l2_config{512, 16, 64, 10}; // 512KB, 16-way, 64B lines, 10 cycle latency

    CacheModel cache(l1_config, l2_config);

    // First access should be a miss
    auto result1 = cache.access(0x1000);
    assert(!result1.hit);
    assert(result1.cache_level == 0);
    std::cout << "  First access: miss (expected)" << std::endl;

    // Fill from DRAM
    cache.fill_from_dram(0x1000);

    // Second access should be an L1 hit
    auto result2 = cache.access(0x1000);
    assert(result2.hit);
    assert(result2.cache_level == 1);
    assert(result2.latency == 1);
    std::cout << "  Second access: L1 hit (expected)" << std::endl;

    // Access same cache line (different offset)
    auto result3 = cache.access(0x1010);  // Same line as 0x1000
    assert(result3.hit);
    assert(result3.cache_level == 1);
    std::cout << "  Same line access: L1 hit (expected)" << std::endl;

    // Access different address
    auto result4 = cache.access(0x2000);
    assert(!result4.hit);
    std::cout << "  Different address: miss (expected)" << std::endl;

    std::cout << "  L1 stats: " << cache.get_l1_stats().hits << " hits, "
              << cache.get_l1_stats().misses << " misses" << std::endl;
    std::cout << "  L2 stats: " << cache.get_l2_stats().hits << " hits, "
              << cache.get_l2_stats().misses << " misses" << std::endl;

    std::cout << "CacheModel tests passed!" << std::endl;
}

void test_ideal_backend() {
    std::cout << "\nTesting IdealBackend..." << std::endl;

    MemoryBackendConfig config;
    config.type = "ideal";
    config.l1_size_kb = 64;
    config.l1_associativity = 8;
    config.l1_line_size = 64;
    config.l1_latency = 1;
    config.l2_size_kb = 512;
    config.l2_associativity = 16;
    config.l2_line_size = 64;
    config.l2_latency = 10;
    config.ideal_latency = 100;

    auto backend = MemoryBackend::create(config);
    assert(backend != nullptr);

    // Submit a load request
    MemoryRequest req;
    req.req_id = 1;
    req.address = 0x1000;
    req.type = MemoryRequestType::LOAD;
    req.matrix = MatrixType::A;
    req.data = 0;
    req.pe_row = 0;
    req.pe_col = 0;
    req.c_col = 0;
    req.dest = "pe";
    req.submit_cycle = 0;

    bool accepted = backend->submit_request(req);
    assert(accepted);
    std::cout << "  Request 1 submitted" << std::endl;

    // First request should be a cache miss, need to tick for (1 + 10 + 100) cycles
    int ticks = 0;
    std::vector<MemoryResponse> responses;
    while (responses.empty() && ticks < 200) {
        responses = backend->tick();
        ticks++;
    }

    assert(!responses.empty());
    assert(responses[0].req_id == 1);
    assert(!responses[0].cache_hit);
    std::cout << "  Request 1 completed after " << ticks << " ticks (cache miss)" << std::endl;

    // Submit another request to same address - should be L1 hit now
    MemoryRequest req2;
    req2.req_id = 2;
    req2.address = 0x1000;
    req2.type = MemoryRequestType::LOAD;
    req2.matrix = MatrixType::A;
    req2.data = 0;
    req2.pe_row = 0;
    req2.pe_col = 0;
    req2.c_col = 0;
    req2.dest = "pe";
    req2.submit_cycle = backend->get_cycle();

    accepted = backend->submit_request(req2);
    assert(accepted);
    std::cout << "  Request 2 submitted" << std::endl;

    // Should complete in 1 cycle (L1 hit)
    ticks = 0;
    responses.clear();
    while (responses.empty() && ticks < 10) {
        responses = backend->tick();
        ticks++;
    }

    assert(!responses.empty());
    assert(responses[0].req_id == 2);
    assert(responses[0].cache_hit);
    assert(responses[0].cache_level == 1);
    std::cout << "  Request 2 completed after " << ticks << " ticks (L1 hit)" << std::endl;

    // Check stats
    const auto& stats = backend->get_stats();
    std::cout << "  Final stats: L1 hits=" << stats.l1_hits
              << ", L1 misses=" << stats.l1_misses
              << ", L2 hits=" << stats.l2_hits
              << ", L2 misses=" << stats.l2_misses
              << ", DRAM accesses=" << stats.dram_accesses << std::endl;

    assert(stats.l1_hits == 1);
    assert(stats.l1_misses == 1);
    assert(stats.dram_accesses == 1);

    std::cout << "IdealBackend tests passed!" << std::endl;
}

void test_backend_factory() {
    std::cout << "\nTesting backend factory..." << std::endl;

    MemoryBackendConfig config;
    config.type = "ideal";
    auto backend1 = MemoryBackend::create(config);
    assert(backend1 != nullptr);
    std::cout << "  Created ideal backend" << std::endl;

    config.type = "ramulator2";
    auto backend2 = MemoryBackend::create(config);
    assert(backend2 != nullptr);
    std::cout << "  Created ramulator2 backend (fallback mode without DRAM config)" << std::endl;

    config.type = "unknown";
    auto backend3 = MemoryBackend::create(config);
    assert(backend3 != nullptr);
    std::cout << "  Created fallback backend for unknown type" << std::endl;

    std::cout << "Backend factory tests passed!" << std::endl;
}

int main() {
    std::cout << "=== Memory Backend Tests ===" << std::endl;

    try {
        test_cache_model();
        test_ideal_backend();
        test_backend_factory();

        std::cout << "\n=== All tests passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
