#pragma once

#include <cstdint>
#include <vector>
#include <list>
#include <unordered_map>

namespace csegfold {

struct CacheConfig {
    int size_kb;
    int associativity;
    int line_size;
    int latency_cycles;
};

struct CacheStats {
    uint64_t hits = 0;
    uint64_t misses = 0;

    double hit_rate() const {
        uint64_t total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }

    void reset() {
        hits = 0;
        misses = 0;
    }
};

class CacheLevel {
public:
    CacheLevel(const CacheConfig& config);

    // Access the cache. Returns true on hit, false on miss.
    bool access(uint64_t address);

    // Insert a line into the cache (called on miss from lower level)
    void insert(uint64_t address);

    // Get the tag for an address
    uint64_t get_tag(uint64_t address) const;

    // Get the set index for an address
    uint64_t get_set_index(uint64_t address) const;

    // Get cache statistics
    const CacheStats& get_stats() const { return stats_; }

    // Reset statistics
    void reset_stats() { stats_.reset(); }

    // Get latency
    int get_latency() const { return config_.latency_cycles; }

    // Get line size
    int get_line_size() const { return config_.line_size; }

private:
    CacheConfig config_;
    CacheStats stats_;

    int num_sets_;
    int offset_bits_;
    int index_bits_;

    // Cache storage: set index -> list of (tag, valid) pairs (LRU order, MRU at front)
    std::vector<std::list<uint64_t>> sets_;
    // For O(1) lookup within sets
    std::vector<std::unordered_map<uint64_t, std::list<uint64_t>::iterator>> set_maps_;
};

class CacheModel {
public:
    CacheModel(const CacheConfig& l1_config, const CacheConfig& l2_config);

    // Access memory through cache hierarchy.
    // Returns: {hit, cache_level, latency}
    // cache_level: 1 = L1 hit, 2 = L2 hit, 0 = miss (DRAM access needed)
    struct AccessResult {
        bool hit;
        int cache_level;
        int latency;
    };
    AccessResult access(uint64_t address);

    // Notify cache that data arrived from DRAM (insert into both L1 and L2)
    void fill_from_dram(uint64_t address);

    // Get combined statistics
    const CacheStats& get_l1_stats() const { return l1_.get_stats(); }
    const CacheStats& get_l2_stats() const { return l2_.get_stats(); }

    // Reset all statistics
    void reset_stats();

    // Get latencies
    int get_l1_latency() const { return l1_.get_latency(); }
    int get_l2_latency() const { return l2_.get_latency(); }

private:
    CacheLevel l1_;
    CacheLevel l2_;
};

} // namespace csegfold
