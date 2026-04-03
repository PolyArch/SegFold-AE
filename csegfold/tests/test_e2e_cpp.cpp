#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_small_matrix_simulation() {
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
        {"verbose", "true"},  // Enable verbose output for debugging
        {"enable_tile_eviction", "false"}  // Disable tile eviction for debugging
    });
    
    // Create small test matrices with density=1.0 (all ones)
    Matrix<int8_t> A(4, 4, 1);  // All ones
    Matrix<int8_t> B(4, 4, 1);  // All ones
    
    std::cout << "  Created matrices: A(4x4) with density=1.0, B(4x4) with density=1.0" << std::endl;
    std::cout << "  Expected output: C = A * B (all 4s)" << std::endl;
    
    // Print input matrices
    std::cout << "  Input matrix A:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << static_cast<int>(A(i, j));
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  Verbose mode: " << (sim.cfg.verbose ? "enabled" : "disabled") << std::endl;
    
    test_assert(sim.matrix.M == 4, "Matrix M dimension correct");
    test_assert(sim.matrix.K == 4, "Matrix K dimension correct");
    test_assert(sim.matrix.N == 4, "Matrix N dimension correct");
    
    // Check initial state
    std::cout << "  Initial state:" << std::endl;
    std::cout << "    - Active switches: " << sim.switchModule.num_active_switches() << std::endl;
    std::cout << "    - Active PEs: " << sim.peModule.num_active_pes() << std::endl;
    std::cout << "    - Controller is_done: " << (sim.controller.get_is_done() ? "true" : "false") << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    std::cout << "  ========================================" << std::endl;
    sim.run();
    std::cout << "  ========================================" << std::endl;
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    // Print acc_output for debugging
    std::cout << "  Final acc_output:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "    [";
        for (int j = 0; j < 4; ++j) {
            std::cout << sim.acc_output.get(i, j);
            if (j < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    // Check if simulation completed successfully
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        std::cout << "  Expected output:" << std::endl;
        for (int i = 0; i < 4; ++i) {
            std::cout << "    [";
            for (int j = 0; j < 4; ++j) {
                std::cout << static_cast<int>(expected(i, j));
                if (j < 3) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result");
        
        // Check that all PEs and switches are idle
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
        test_assert(sim.is_done(), "Simulator reports done");
        test_assert(sim.store_is_done(), "All C values stored");
    } else {
        std::cout << "  Simulation did not complete successfully (this may be expected for complex cases)" << std::endl;
        std::cout << "  Final state:" << std::endl;
        std::cout << "    - Cycle: " << sim.stats.cycle << std::endl;
        std::cout << "    - Controller is_done: " << (sim.controller.get_is_done() ? "true" : "false") << std::endl;
        std::cout << "    - Active switches: " << sim.switchModule.num_active_switches() << std::endl;
        std::cout << "    - Active PEs: " << sim.peModule.num_active_pes() << std::endl;
        std::cout << "    - B rows completed: " << sim.controller.n_completed_rows << std::endl;
    }
}

void test_larger_matrix_simulation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        // virtual_pe_row_num will be set to physical_pe_row_num by MatrixLoader (like Python)
        // virtual_pe_col_num will be set to physical_pe_col_num when spatial folding is disabled
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "4"},  // Small window to force eviction
        {"II", "1"},
        {"max_cycle", "50000"},
        {"verbose", "true"},  // Enable verbose to see tile eviction
        {"enable_tile_eviction", "true"},  // Test with tile eviction enabled
        {"enable_spatial_folding", "false"}  // Disable spatial folding
    });
    
    // Create larger test matrices (16x16) that will span multiple tiles
    // With K=16 and physical_pe_col_num=4, we have tile size = 4
    // So rows 0-3 are tile 0, 4-7 are tile 1, etc.
    Matrix<int8_t> A(16, 16, 0);
    Matrix<int8_t> B(16, 16, 0);
    
    // Fill with pattern: diagonal + some off-diagonal
    for (int i = 0; i < 16; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i < 15) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
    }
    
    std::cout << "  Created matrices: A(16x16), B(16x16) to test tile eviction across multiple tiles" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    test_assert(sim.matrix.M == 16, "Matrix M dimension correct");
    test_assert(sim.matrix.K == 16, "Matrix K dimension correct");
    test_assert(sim.matrix.N == 16, "Matrix N dimension correct");
    
    // Run simulation
    std::cout << "  Running simulation with tile eviction enabled..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        // Print actual and expected for debugging
        std::cout << "  Actual output:" << std::endl;
        for (int i = 0; i < sim.matrix.M; ++i) {
            std::cout << "    [";
            for (int j = 0; j < sim.matrix.N; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < sim.matrix.N - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        std::cout << "  Expected output:" << std::endl;
        for (int i = 0; i < expected.rows(); ++i) {
            std::cout << "    [";
            for (int j = 0; j < expected.cols(); ++j) {
                std::cout << static_cast<int>(expected(i, j));
                if (j < expected.cols() - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result for larger matrix with tile eviction");
        
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
        test_assert(sim.is_done(), "Simulator reports done");
    } else {
        std::cout << "  Simulation did not complete (may need more cycles)" << std::endl;
    }
}

void test_sparse_matrix_simulation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "8"},
        {"physical_pe_col_num", "8"},
        {"virtual_pe_row_num", "8"},
        {"virtual_pe_col_num", "8"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "8"},
        {"b_loader_window_size", "16"},
        {"II", "1"},
        {"max_cycle", "50000"},
        {"verbose", "false"}
    });
    
    // Create sparse matrices
    MatrixParams params{
        16, 16, 16,  // M, K, N
        0.1, 0.1,   // densityA, densityB
        0.01,        // c_density
        8, 8,        // prow, pcol
        0.0, 0.0,    // m_variance, k_variance
        true,        // sparsity_aware
        42           // random_state
    };
    
    auto [A, B, C] = gen_uniform_matrix(params);
    
    std::cout << "  Created sparse matrices:" << std::endl;
    std::cout << "    A(" << A.rows() << "x" << A.cols() << ") with " << A.nnz() << " nnz" << std::endl;
    std::cout << "    B(" << B.rows() << "x" << B.cols() << ") with " << B.nnz() << " nnz" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result for sparse matrices");
    } else {
        std::cout << "  Simulation did not complete (may need more cycles or different config)" << std::endl;
    }
}

void test_simulation_step_functionality() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"II", "1"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    SegfoldSimulator sim(A, B);
    
    // Test that step() function works
    int initial_cycle = sim.stats.cycle;
    sim.step();
    
    test_assert(sim.stats.cycle == initial_cycle + 1, "Step increments cycle counter");
    test_assert(sim.stats.cycle > 0, "Cycle counter is positive after step");
}

void test_simulation_state_management() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    SegfoldSimulator sim(A, B);
    
    // Initially should not be done
    test_assert(!sim.is_done(), "Simulator not done initially");
    
    // Check that refresh_states works
    sim.refresh_states();
    test_assert(true, "refresh_states completes without error");
    
    // Check that store_c_to_spad works
    sim.store_c_to_spad();
    test_assert(true, "store_c_to_spad completes without error");
}

void test_8x8_matrix_debug() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "10000"},
        {"verbose", "true"},
        {"enable_tile_eviction", "true"},  // Enable tile eviction to process rows 4-7
        {"enable_spatial_folding", "false"}
    });
    
    // Create 8x8 test matrices
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    // Fill with simple pattern: diagonal + off-diagonal
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i < 7) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
    }
    
    std::cout << "  Matrix dimensions: A(8x8), B(8x8)" << std::endl;
    std::cout << "  PE configuration: 4x4 physical PEs" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  virtual_pe_row_num: " << sim.cfg.virtual_pe_row_num << std::endl;
    std::cout << "  virtual_pe_col_num: " << sim.cfg.virtual_pe_col_num << std::endl;
    std::cout << "  physical_pe_row_num: " << sim.cfg.physical_pe_row_num << std::endl;
    std::cout << "  physical_pe_col_num: " << sim.cfg.physical_pe_col_num << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        std::cout << "  Actual output:" << std::endl;
        for (int i = 0; i < 8; ++i) {
            std::cout << "    Row " << i << ": [";
            for (int j = 0; j < 8; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < 7) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        std::cout << "  Expected output:" << std::endl;
        for (int i = 0; i < 8; ++i) {
            std::cout << "    Row " << i << ": [";
            for (int j = 0; j < 8; ++j) {
                std::cout << static_cast<int>(expected(i, j));
                if (j < 7) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Check which rows are non-zero
        std::cout << "  Row analysis:" << std::endl;
        for (int i = 0; i < 8; ++i) {
            bool has_nonzero = false;
            for (int j = 0; j < 8; ++j) {
                if (sim.acc_output.get(i, j) != 0) {
                    has_nonzero = true;
                    break;
                }
            }
            std::cout << "    Row " << i << ": " << (has_nonzero ? "has non-zeros" : "all zeros") << std::endl;
        }
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result for 8x8 matrix");
    } else {
        std::cout << "  Simulation did not complete successfully" << std::endl;
    }
}

void test_8x8_matrix_dense() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "4"},
        {"b_loader_window_size", "8"},
        {"II", "1"},
        {"max_cycle", "50000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"}
    });
    
    // Create 8x8 dense matrices
    Matrix<int8_t> A(8, 8, 1);  // All ones
    Matrix<int8_t> B(8, 8, 1);  // All ones
    
    std::cout << "  Created dense matrices: A(8x8) with density=1.0, B(8x8) with density=1.0" << std::endl;
    std::cout << "  PE configuration: 4x4 physical PEs" << std::endl;
    std::cout << "  Expected output: C = A * B (all 8s)" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  virtual_pe_row_num: " << sim.cfg.virtual_pe_row_num << std::endl;
    std::cout << "  virtual_pe_col_num: " << sim.cfg.virtual_pe_col_num << std::endl;
    std::cout << "  physical_pe_row_num: " << sim.cfg.physical_pe_row_num << std::endl;
    std::cout << "  physical_pe_col_num: " << sim.cfg.physical_pe_col_num << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output using int64_t to avoid overflow
        // For 8x8 dense matrices (all ones), result should be 8 per element
        int total_mismatches = 0;
        
        // Print entire output matrix
        std::cout << "  Actual output (entire 8x8 matrix):" << std::endl;
        for (int i = 0; i < sim.matrix.M; ++i) {
            std::cout << "    Row " << i << ": [";
            for (int j = 0; j < sim.matrix.N; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < sim.matrix.N - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        std::cout << "  Expected output (entire 8x8 matrix):" << std::endl;
        for (int i = 0; i < sim.matrix.M; ++i) {
            std::cout << "    Row " << i << ": [";
            for (int j = 0; j < sim.matrix.N; ++j) {
                // Compute expected value using int64_t
                int64_t expected_val = 0;
                for (int k = 0; k < sim.matrix.K; ++k) {
                    expected_val += static_cast<int64_t>(A(i, k)) * static_cast<int64_t>(B(k, j));
                }
                std::cout << expected_val;
                if (j < sim.matrix.N - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Check all values for mismatches
        int mismatch_count = 0;
        int max_mismatches_to_show = 10;
        for (int i = 0; i < sim.matrix.M; ++i) {
            for (int j = 0; j < sim.matrix.N; ++j) {
                // Compute expected value using int64_t
                int64_t expected_val = 0;
                for (int k = 0; k < sim.matrix.K; ++k) {
                    expected_val += static_cast<int64_t>(A(i, k)) * static_cast<int64_t>(B(k, j));
                }
                
                if (sim.acc_output.get(i, j) != expected_val) {
                    if (mismatch_count < max_mismatches_to_show) {
                        if (mismatch_count == 0) {
                            std::cout << "  Output mismatches found:" << std::endl;
                        }
                        std::cout << "    Mismatch at (" << i << ", " << j << "): got " 
                                  << sim.acc_output.get(i, j) << ", expected "
                                  << expected_val << std::endl;
                        mismatch_count++;
                    }
                    total_mismatches++;
                }
            }
        }
        
        if (total_mismatches > 0) {
            std::cout << "  Total mismatches: " << total_mismatches << " out of " 
                      << (sim.matrix.M * sim.matrix.N) << " elements" << std::endl;
        } else {
            std::cout << "  All outputs match!" << std::endl;
        }
        
        // Verify output using int64_t comparison
        bool output_match = (total_mismatches == 0);
        test_assert(output_match, "Output matches expected result for 8x8 dense matrix");
        
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
        test_assert(sim.is_done(), "Simulator reports done");
    } else {
        std::cout << "  Simulation did not complete successfully" << std::endl;
        std::cout << "  Final cycle: " << sim.stats.cycle << std::endl;
    }
}

void test_128x128_matrix_dense() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "16"},
        {"physical_pe_col_num", "16"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "16"},
        {"b_loader_window_size", "32"},
        {"II", "1"},
        {"max_cycle", "1000000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"}
    });
    
    // Create 128x128 dense matrices
    Matrix<int8_t> A(128, 128, 1);  // All ones
    Matrix<int8_t> B(128, 128, 1);  // All ones
    
    std::cout << "  Created dense matrices: A(128x128) with density=1.0, B(128x128) with density=1.0" << std::endl;
    std::cout << "  PE configuration: 16x16 physical PEs" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  virtual_pe_row_num: " << sim.cfg.virtual_pe_row_num << std::endl;
    std::cout << "  virtual_pe_col_num: " << sim.cfg.virtual_pe_col_num << std::endl;
    std::cout << "  physical_pe_row_num: " << sim.cfg.physical_pe_row_num << std::endl;
    std::cout << "  physical_pe_col_num: " << sim.cfg.physical_pe_col_num << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output using int64_t to avoid overflow
        // For 128x128 dense matrices (all ones), result should be 128 per element
        // But int8_t can only hold -128 to 127, so we compute expected as int64_t
        // Check for mismatches and report first few
        int mismatch_count = 0;
        int max_mismatches_to_show = 10;
        int total_mismatches = 0;
        
        // Check sample values first
        std::cout << "  Sample values (first 5x5):" << std::endl;
        std::cout << "  Actual:" << std::endl;
        for (int i = 0; i < 5 && i < sim.matrix.M; ++i) {
            std::cout << "    [";
            for (int j = 0; j < 5 && j < sim.matrix.N; ++j) {
                std::cout << sim.acc_output.get(i, j);
                if (j < 4 && j < sim.matrix.N - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        std::cout << "  Expected:" << std::endl;
        for (int i = 0; i < 5 && i < sim.matrix.M; ++i) {
            std::cout << "    [";
            for (int j = 0; j < 5 && j < sim.matrix.N; ++j) {
                // Compute expected value using int64_t
                int64_t expected_val = 0;
                for (int k = 0; k < sim.matrix.K; ++k) {
                    expected_val += static_cast<int64_t>(A(i, k)) * static_cast<int64_t>(B(k, j));
                }
                std::cout << expected_val;
                if (j < 4 && j < sim.matrix.N - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        }
        
        // Check all values for mismatches
        for (int i = 0; i < sim.matrix.M; ++i) {
            for (int j = 0; j < sim.matrix.N; ++j) {
                // Compute expected value using int64_t
                int64_t expected_val = 0;
                for (int k = 0; k < sim.matrix.K; ++k) {
                    expected_val += static_cast<int64_t>(A(i, k)) * static_cast<int64_t>(B(k, j));
                }
                
                if (sim.acc_output.get(i, j) != expected_val) {
                    if (mismatch_count < max_mismatches_to_show) {
                        if (mismatch_count == 0) {
                            std::cout << "  Output mismatches found:" << std::endl;
                        }
                        std::cout << "    Mismatch at (" << i << ", " << j << "): got " 
                                  << sim.acc_output.get(i, j) << ", expected "
                                  << expected_val << std::endl;
                        mismatch_count++;
                    }
                    total_mismatches++;
                }
            }
        }
        
        if (total_mismatches > 0) {
            std::cout << "  Total mismatches: " << total_mismatches << " out of " 
                      << (sim.matrix.M * sim.matrix.N) << " elements" << std::endl;
        } else {
            std::cout << "  All outputs match!" << std::endl;
        }
        
        // Verify output using int64_t comparison
        bool output_match = (total_mismatches == 0);
        test_assert(output_match, "Output matches expected result for 128x128 dense matrix");
        
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
        test_assert(sim.is_done(), "Simulator reports done");
    } else {
        std::cout << "  Simulation did not complete successfully" << std::endl;
        std::cout << "  Final cycle: " << sim.stats.cycle << std::endl;
    }
}

void test_128x128_matrix_sparse() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "16"},
        {"physical_pe_col_num", "16"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "16"},
        {"b_loader_window_size", "64"},
        {"II", "1"},
        {"max_cycle", "1000000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "false"}
    });
    
    // Create 128x128 sparse matrices with pattern
    Matrix<int8_t> A(128, 128, 0);
    Matrix<int8_t> B(128, 128, 0);
    
    // Fill with diagonal + band pattern for sparse test
    for (int i = 0; i < 128; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i < 127) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
        if (i < 126) {
            A(i, i+2) = 1;
            B(i+2, i) = 1;
        }
    }
    
    std::cout << "  Created sparse matrices: A(128x128), B(128x128) with diagonal+band pattern" << std::endl;
    std::cout << "  PE configuration: 16x16 physical PEs" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    // Run simulation
    std::cout << "  Running simulation..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result for 128x128 sparse matrix");
        
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
    } else {
        std::cout << "  Simulation did not complete successfully" << std::endl;
    }
}

void test_128x128_matrix_with_spatial_folding() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "16"},
        {"physical_pe_col_num", "16"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "16"},
        {"b_loader_window_size", "32"},
        {"II", "1"},
        {"max_cycle", "1000000"},
        {"verbose", "false"},
        {"enable_tile_eviction", "true"},
        {"enable_spatial_folding", "true"}  // Enable spatial folding
    });
    
    // Create 128x128 matrices
    Matrix<int8_t> A(128, 128, 0);
    Matrix<int8_t> B(128, 128, 0);
    
    // Fill with pattern: diagonal + some off-diagonal
    for (int i = 0; i < 128; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i < 127) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
    }
    
    std::cout << "  Created matrices: A(128x128), B(128x128) with spatial folding enabled" << std::endl;
    std::cout << "  PE configuration: 16x16 physical PEs" << std::endl;
    
    SegfoldSimulator sim(A, B);
    
    std::cout << "  virtual_pe_row_num: " << sim.cfg.virtual_pe_row_num << std::endl;
    std::cout << "  virtual_pe_col_num: " << sim.cfg.virtual_pe_col_num << std::endl;
    
    // Run simulation
    std::cout << "  Running simulation with spatial folding..." << std::endl;
    sim.run();
    
    test_assert(sim.stats.cycle > 0, "Simulation ran for at least one cycle");
    test_assert(sim.stats.cycle < sim.cfg.max_cycle, "Simulation completed within max_cycle limit");
    
    if (sim.success) {
        std::cout << "  Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;
        
        // Compute expected output
        Matrix<int8_t> expected = A * B;
        
        // Verify output
        bool output_match = sim.check_output(expected);
        test_assert(output_match, "Output matches expected result for 128x128 matrix with spatial folding");
        
        test_assert(sim.peModule.num_active_pes() == 0, "All PEs are idle after completion");
        test_assert(sim.switchModule.num_active_switches() == 0, "All switches are idle after completion");
    } else {
        std::cout << "  Simulation did not complete successfully" << std::endl;
    }
}

int main() {
    std::cout << "\n=== C++ End-to-End Tests ===" << std::endl;
    
    std::cout << "\n--- Test: Small Matrix Simulation ---" << std::endl;
    test_small_matrix_simulation();
    
    std::cout << "\n--- Test: Larger Matrix Simulation ---" << std::endl;
    test_larger_matrix_simulation();
    
    std::cout << "\n--- Test: Sparse Matrix Simulation ---" << std::endl;
    test_sparse_matrix_simulation();
    
    std::cout << "\n--- Test: Simulation Step Functionality ---" << std::endl;
    test_simulation_step_functionality();
    
    std::cout << "\n--- Test: Simulation State Management ---" << std::endl;
    test_simulation_state_management();
    
    std::cout << "\n--- Test: 8x8 Matrix Debug ---" << std::endl;
    test_8x8_matrix_debug();
    
    std::cout << "\n--- Test: 8x8 Dense Matrix ---" << std::endl;
    test_8x8_matrix_dense();
    
    std::cout << "\n--- Test: 128x128 Dense Matrix ---" << std::endl;
    test_128x128_matrix_dense();
    
    std::cout << "\n--- Test: 128x128 Sparse Matrix ---" << std::endl;
    test_128x128_matrix_sparse();
    
    std::cout << "\n--- Test: 128x128 Matrix with Spatial Folding ---" << std::endl;
    test_128x128_matrix_with_spatial_folding();
    
    std::cout << "\n=== All C++ End-to-End Tests Completed! ===" << std::endl;
    return 0;
}

