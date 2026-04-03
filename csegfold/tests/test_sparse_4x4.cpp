#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_sparse_4x4_functionality() {
    std::cout << "\n=== Testing Sparse 4x4 Matrix Functionality ===" << std::endl;
    
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "10000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "false"},
        {"is_dense", "false"}
    });
    
    // Create sparse 4x4 matrices (diagonal pattern with some off-diagonal elements)
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    // Fill A with diagonal + upper diagonal pattern
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        if (i < 3) {
            A(i, i+1) = 1;
        }
    }
    
    // Fill B with diagonal + lower diagonal pattern
    for (int i = 0; i < 4; ++i) {
        B(i, i) = 1;
        if (i > 0) {
            B(i, i-1) = 1;
        }
    }
    
    std::cout << "  Created sparse matrices:" << std::endl;
    std::cout << "  Input matrix A:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << static_cast<int>(A(i, j));
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    std::cout << "  Input matrix B:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << static_cast<int>(B(i, j));
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    // Compute expected output
    Matrix<int8_t> expected = A * B;
    std::cout << "  Expected output (A * B):" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << static_cast<int>(expected(i, j));
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    SegfoldSimulator sim(A, B);
    
    test_assert(sim.matrix.M == 4, "Matrix M dimension correct");
    test_assert(sim.matrix.K == 4, "Matrix K dimension correct");
    test_assert(sim.matrix.N == 4, "Matrix N dimension correct");
    
    // Print tiled matrices and indices
    std::cout << "\n  === Tiled Matrices Information ===" << std::endl;
    std::cout << "  Number of tiles: " << sim.matrix.tiles.size() << std::endl;
    
    // Print tile boundaries
    for (size_t tile_id = 0; tile_id < sim.matrix.tiles.size(); ++tile_id) {
        auto [m_start, m_end, n_start, n_end] = sim.matrix.tiles[tile_id];
        std::cout << "  Tile " << tile_id << ": A[" << m_start << ":" << m_end 
                  << ", :] x B[:, " << n_start << ":" << n_end << "]" << std::endl;
    }
    
    // Print tiled A matrix with indices
    std::cout << "\n  Tiled A_indexed matrix (shape: " << sim.matrix.A_indexed.rows_ 
              << "x" << sim.matrix.A_indexed.cols_ << ", nnz: " 
              << sim.matrix.A_indexed.nnz() << "):" << std::endl;
    for (int i = 0; i < sim.matrix.A_indexed.rows_; ++i) {
        int start = sim.matrix.A_indexed.indptr_[i];
        int end = sim.matrix.A_indexed.indptr_[i + 1];
        if (start < end) {
            std::cout << "    Row " << i << ": ";
            for (int idx = start; idx < end; ++idx) {
                int tiled_col = sim.matrix.A_indexed.indices_[idx];
                int8_t val = sim.matrix.A_indexed.data_[idx];
                int32_t orig_row = sim.matrix.A_indexed.orig_row_[idx];
                int32_t orig_col = sim.matrix.A_indexed.orig_col_[idx];
                std::cout << "col[" << tiled_col << "]=" << static_cast<int>(val) 
                          << " (orig[" << orig_row << "," << orig_col << "])";
                if (idx < end - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
    }
    
    // Print tiled B matrix with indices
    std::cout << "\n  Tiled B_indexed matrix (shape: " << sim.matrix.B_indexed.rows_ 
              << "x" << sim.matrix.B_indexed.cols_ << ", nnz: " 
              << sim.matrix.B_indexed.nnz() << "):" << std::endl;
    for (int i = 0; i < sim.matrix.B_indexed.rows_; ++i) {
        int start = sim.matrix.B_indexed.indptr_[i];
        int end = sim.matrix.B_indexed.indptr_[i + 1];
        if (start < end) {
            std::cout << "    Row " << i << ": ";
            for (int idx = start; idx < end; ++idx) {
                int tiled_col = sim.matrix.B_indexed.indices_[idx];
                int8_t val = sim.matrix.B_indexed.data_[idx];
                int32_t orig_row = sim.matrix.B_indexed.orig_row_[idx];
                int32_t orig_col = sim.matrix.B_indexed.orig_col_[idx];
                std::cout << "col[" << tiled_col << "]=" << static_cast<int>(val) 
                          << " (orig[" << orig_row << "," << orig_col << "])";
                if (idx < end - 1) std::cout << ", ";
            }
            std::cout << std::endl;
        }
    }
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    std::cout << "  Simulation completed in " << sim.stats.cycle << " cycles" << std::endl;
    
    // Check if simulation was successful
    if (sim.success) {
        std::cout << "  Actual acc_output:" << std::endl;
        for (int i = 0; i < 4; ++i) {
            std::cout << "    [";
            for (int j = 0; j < 4; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < 3) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Verify output correctness
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "acc_output matches expected result for sparse 4x4 matrices");
        
        std::cout << "  ✓ Output verification passed!" << std::endl;
    } else {
        std::cerr << "  ✗ Simulation did not complete successfully" << std::endl;
        std::cerr << "    Final state:" << std::endl;
        std::cerr << "      - Cycle: " << sim.stats.cycle << std::endl;
        std::cerr << "      - Active PEs: " << sim.peModule.num_active_pes() << std::endl;
        std::cerr << "      - Active switches: " << sim.switchModule.num_active_switches() << std::endl;
        test_assert(false, "Simulation completed successfully");
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Testing Sparse 4x4 Matrix Functionality" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_sparse_4x4_functionality();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All sparse 4x4 tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}

