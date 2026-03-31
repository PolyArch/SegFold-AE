#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace csegfold {

enum class MemoryRequestType {
    LOAD,
    STORE
};

enum class MatrixType {
    A,
    B,
    C
};

struct MemoryRequest {
    uint64_t req_id;
    uint64_t address;
    MemoryRequestType type;
    MatrixType matrix;
    int data;

    // Source/destination info for routing completed requests
    int pe_row;
    int pe_col;
    int c_col;
    std::string dest;  // "switch", "pe", or "b_loader_fifo"
    int fifo_idx = -1; // FIFO entry index for b_loader_fifo dest (direct indexing)

    // Callback for completion notification (optional)
    std::function<void(const struct MemoryResponse&)> callback;

    // Cycle when request was submitted
    uint64_t submit_cycle;
};

struct MemoryResponse {
    uint64_t req_id;
    uint64_t latency;
    int data;
    bool cache_hit;
    int cache_level;  // 0 = DRAM, 1 = L1, 2 = L2

    // Original request info for routing
    int pe_row;
    int pe_col;
    int c_col;
    std::string dest;
    MatrixType matrix;
    int fifo_idx = -1; // FIFO entry index for b_loader_fifo dest
};

} // namespace csegfold
