#include "csegfold/memory/CacheModel.hpp"
#include <cmath>
#include <cassert>

namespace csegfold {

CacheLevel::CacheLevel(const CacheConfig& config) : config_(config) {
    if (config.size_kb == 0) {
        // Zero-size cache: passthrough mode — all accesses miss
        num_sets_ = 0;
        offset_bits_ = 0;
        index_bits_ = 0;
        return;
    }

    // Calculate cache geometry
    int total_size_bytes = config.size_kb * 1024;
    int num_lines = total_size_bytes / config.line_size;
    num_sets_ = num_lines / config.associativity;

    // Calculate bit widths
    offset_bits_ = static_cast<int>(std::log2(config.line_size));
    index_bits_ = static_cast<int>(std::log2(num_sets_));

    // Initialize sets
    sets_.resize(num_sets_);
    set_maps_.resize(num_sets_);
}

uint64_t CacheLevel::get_tag(uint64_t address) const {
    return address >> (offset_bits_ + index_bits_);
}

uint64_t CacheLevel::get_set_index(uint64_t address) const {
    uint64_t mask = (1ULL << index_bits_) - 1;
    return (address >> offset_bits_) & mask;
}

bool CacheLevel::access(uint64_t address) {
    if (num_sets_ == 0) {
        stats_.misses++;
        return false;
    }
    uint64_t set_idx = get_set_index(address);
    uint64_t tag = get_tag(address);

    auto& set = sets_[set_idx];
    auto& map = set_maps_[set_idx];

    auto it = map.find(tag);
    if (it != map.end()) {
        // Hit - move to front (MRU)
        set.erase(it->second);
        set.push_front(tag);
        map[tag] = set.begin();
        stats_.hits++;
        return true;
    }

    // Miss
    stats_.misses++;
    return false;
}

void CacheLevel::insert(uint64_t address) {
    if (num_sets_ == 0) {
        return;  // No-op for zero-size cache
    }
    uint64_t set_idx = get_set_index(address);
    uint64_t tag = get_tag(address);

    auto& set = sets_[set_idx];
    auto& map = set_maps_[set_idx];

    // Check if already present
    if (map.find(tag) != map.end()) {
        // Already in cache, just update LRU
        set.erase(map[tag]);
        set.push_front(tag);
        map[tag] = set.begin();
        return;
    }

    // Evict if necessary
    if (static_cast<int>(set.size()) >= config_.associativity) {
        // Remove LRU (back of list)
        uint64_t victim_tag = set.back();
        set.pop_back();
        map.erase(victim_tag);
    }

    // Insert new tag at front (MRU)
    set.push_front(tag);
    map[tag] = set.begin();
}

CacheModel::CacheModel(const CacheConfig& l1_config, const CacheConfig& l2_config)
    : l1_(l1_config), l2_(l2_config) {
}

CacheModel::AccessResult CacheModel::access(uint64_t address) {
    // Try L1 first
    if (l1_.access(address)) {
        return {true, 1, l1_.get_latency()};
    }

    // L1 miss - try L2
    if (l2_.access(address)) {
        // Also insert into L1 on L2 hit
        l1_.insert(address);
        return {true, 2, l1_.get_latency() + l2_.get_latency()};
    }

    // Both miss - need DRAM
    return {false, 0, l1_.get_latency() + l2_.get_latency()};
}

void CacheModel::fill_from_dram(uint64_t address) {
    // Insert into both L2 and L1
    l2_.insert(address);
    l1_.insert(address);
}

void CacheModel::reset_stats() {
    l1_.reset_stats();
    l2_.reset_stats();
}

} // namespace csegfold
