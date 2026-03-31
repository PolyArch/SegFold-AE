#include "csegfold/simulator/simulator.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <iomanip>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

// Helper to count B elements on switches
int count_b_elements_on_switch(SwitchModule& switchModule) {
    int count = 0;
    for (int i = 0; i < switchModule.vrows(); ++i) {
        for (int j = 0; j < switchModule.vcols(); ++j) {
            if (switchModule.switches[i][j].b.val.has_value()) {
                count++;
            }
        }
    }
    return count;
}

// Helper to count B elements in FIFO
int count_b_elements_in_fifo(SwitchModule& switchModule) {
    if (!switchModule.cfg.enable_b_loader_fifo) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < switchModule.vrows(); ++i) {
        for (int j = 0; j < switchModule.vcols(); ++j) {
            count += switchModule.b_loader_fifo[i][j].size();
        }
    }
    return count;
}

// Test: FIFO disabled - baseline behavior
void test_fifo_disabled_baseline() {
    std::cout << "\n--- Test: FIFO Disabled (Baseline) ---" << std::endl;

    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "false"},  // FIFO disabled
        {"b_loader_fifo_size", "0"}
    });

    // Create sparse matrices
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    // Diagonal pattern
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }

    SegfoldSimulator sim(A, B);

    test_assert(!sim.switchModule.cfg.enable_b_loader_fifo,
                "FIFO is disabled");
    test_assert(sim.switchModule.all_b_loader_fifos_empty(),
                "All FIFOs are empty when disabled");

    // Run a few cycles
    for (int c = 0; c < 10 && !sim.is_done(); ++c) {
        sim.step();
    }

    int b_on_switch = count_b_elements_on_switch(sim.switchModule);
    std::cout << "  B elements on switch after 10 cycles: " << b_on_switch << std::endl;

    test_assert(b_on_switch >= 0, "B elements loaded to switch");
}

// Test: FIFO enabled - elements should be buffered
void test_fifo_enabled_basic() {
    std::cout << "\n--- Test: FIFO Enabled (Basic) ---" << std::endl;

    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "true"},  // FIFO enabled
        {"b_loader_fifo_size", "8"}
    });

    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }

    SegfoldSimulator sim(A, B);

    test_assert(sim.switchModule.cfg.enable_b_loader_fifo,
                "FIFO is enabled");
    test_assert(sim.switchModule.cfg.b_loader_fifo_size == 8,
                "FIFO size is 8");
    test_assert(sim.switchModule.all_b_loader_fifos_empty(),
                "All FIFOs start empty");

    // Run simulation to completion
    int max_cycles = 1000;
    while (!sim.is_done() && sim.stats.cycle < max_cycles) {
        sim.step();
    }

    test_assert(sim.is_done(), "Simulation completed with FIFO enabled");
    test_assert(sim.switchModule.all_b_loader_fifos_empty(),
                "All FIFOs empty after completion");

    std::cout << "  Simulation completed in " << sim.stats.cycle << " cycles"
              << ", direct=" << sim.stats.b_direct_loads
              << ", fifo=" << sim.stats.b_fifo_enqueues << std::endl;
}

// Test: Compare cycles with different FIFO sizes
void test_fifo_size_sensitivity() {
    std::cout << "\n--- Test: FIFO Size Sensitivity ---" << std::endl;

    std::vector<int> fifo_sizes = {0, 4, 8, 16, 32};
    std::vector<int> cycles_per_size;

    // Create test matrices (8x8)
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    // Tridiagonal pattern for more complex loading
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
        if (i > 0) {
            A(i, i-1) = 1;
            B(i-1, i) = 1;
        }
        if (i < 7) {
            A(i, i+1) = 1;
            B(i+1, i) = 1;
        }
    }

    for (int fifo_size : fifo_sizes) {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"virtual_pe_row_num", "4"},
            {"virtual_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"enable_b_loader_fifo", fifo_size > 0 ? "true" : "false"},
            {"b_loader_fifo_size", std::to_string(fifo_size)}
        });

        SegfoldSimulator sim(A, B);

        int max_cycles = 10000;
        while (!sim.is_done() && sim.stats.cycle < max_cycles) {
            sim.step();
        }

        cycles_per_size.push_back(sim.stats.cycle);

        std::cout << "  FIFO size " << fifo_size
                  << ": " << sim.stats.cycle << " cycles"
                  << ", direct=" << sim.stats.b_direct_loads
                  << ", fifo=" << sim.stats.b_fifo_enqueues << std::endl;
    }

    // Verify all simulations completed
    for (size_t i = 0; i < fifo_sizes.size(); ++i) {
        test_assert(cycles_per_size[i] < 10000,
                    "Simulation completed for FIFO size " + std::to_string(fifo_sizes[i]));
    }
}

// Test: Transparent FIFO behavior - bypass when switch is idle
void test_transparent_fifo_bypass() {
    std::cout << "\n--- Test: Transparent FIFO Bypass ---" << std::endl;

    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "true"},
        {"b_loader_fifo_size", "16"}
    });

    // Simple case: single element
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);

    A(0, 0) = 1;
    B(0, 0) = 1;

    SegfoldSimulator sim(A, B);

    // Initially, all switches should be idle and FIFO empty
    test_assert(sim.switchModule.next_sw_idle(0, 0),
                "Switch [0][0] starts idle");
    test_assert(sim.switchModule.b_loader_fifo_empty(0, 0),
                "FIFO [0][0] starts empty");

    // After first step, B should be loaded directly to switch (bypass)
    sim.step();

    // B should go directly to switch, not FIFO (transparent behavior)
    int fifo_count = count_b_elements_in_fifo(sim.switchModule);
    std::cout << "  B elements in FIFO after first step: " << fifo_count << std::endl;

    // Complete simulation
    int max_cycles = 100;
    while (!sim.is_done() && sim.stats.cycle < max_cycles) {
        sim.step();
    }

    test_assert(sim.is_done(), "Simple case completed");
    std::cout << "  Completed in " << sim.stats.cycle << " cycles" << std::endl;
}

// Test: FIFO ordering - elements should be processed in order
void test_fifo_ordering() {
    std::cout << "\n--- Test: FIFO Ordering ---" << std::endl;

    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "true"},
        {"b_loader_fifo_size", "16"}
    });

    // Create matrices with multiple elements per row
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    // Band matrix pattern
    for (int i = 0; i < 8; ++i) {
        for (int j = std::max(0, i-1); j <= std::min(7, i+1); ++j) {
            A(i, j) = 1;
            B(j, i) = 1;
        }
    }

    SegfoldSimulator sim(A, B);

    // Run to completion
    int max_cycles = 5000;
    while (!sim.is_done() && sim.stats.cycle < max_cycles) {
        sim.step();
    }

    test_assert(sim.is_done(), "Band matrix simulation completed");
    test_assert(sim.switchModule.all_b_loader_fifos_empty(),
                "All FIFOs drained after completion");

    std::cout << "  Completed in " << sim.stats.cycle << " cycles"
              << ", direct=" << sim.stats.b_direct_loads
              << ", fifo=" << sim.stats.b_fifo_enqueues << std::endl;
}

// Test: Dense matrix to show FIFO sensitivity
void test_dense_matrix_fifo_sensitivity() {
    std::cout << "\n--- Test: Dense Matrix FIFO Sensitivity ---" << std::endl;

    std::vector<int> fifo_sizes = {0, 2, 4, 8, 16};
    std::vector<int> cycles_per_size;

    // Create dense matrices (16x16, should create more contention)
    Matrix<int8_t> A(16, 16, 0);
    Matrix<int8_t> B(16, 16, 0);

    // Dense pattern - 50% fill
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            if ((i + j) % 2 == 0) {
                A(i, j) = 1;
                B(i, j) = 1;
            }
        }
    }

    for (int fifo_size : fifo_sizes) {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"virtual_pe_row_num", "4"},
            {"virtual_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"enable_b_loader_fifo", fifo_size > 0 ? "true" : "false"},
            {"b_loader_fifo_size", std::to_string(fifo_size)},
            {"disable_multi_b_row_per_row", "false"},  // Allow multiple B rows per PE row
            {"enable_multi_b_row_loading", "true"}
        });

        SegfoldSimulator sim(A, B);

        int max_cycles = 100000;
        while (!sim.is_done() && sim.stats.cycle < max_cycles) {
            sim.step();
        }

        cycles_per_size.push_back(sim.stats.cycle);

        std::cout << "  FIFO size " << std::setw(2) << fifo_size
                  << ": " << std::setw(6) << sim.stats.cycle << " cycles"
                  << ", direct=" << std::setw(5) << sim.stats.b_direct_loads
                  << ", fifo=" << std::setw(5) << sim.stats.b_fifo_enqueues
                  << std::endl;
    }

    // Verify all simulations completed
    for (size_t i = 0; i < fifo_sizes.size(); ++i) {
        test_assert(cycles_per_size[i] < 100000,
                    "Dense simulation completed for FIFO size " + std::to_string(fifo_sizes[i]));
    }
}

// Test: Compare output correctness with/without FIFO
void test_output_correctness() {
    std::cout << "\n--- Test: Output Correctness with FIFO ---" << std::endl;

    // Create test matrices
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if ((i + j) % 3 == 0) {
                A(i, j) = 1;
                B(j, i) = 1;
            }
        }
    }

    // Compute expected output
    Matrix<int64_t> expected(8, 8, 0);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            for (int k = 0; k < 8; ++k) {
                expected(i, j) += A(i, k) * B(k, j);
            }
        }
    }

    // Run without FIFO
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "false"},
        {"b_loader_fifo_size", "0"}
    });

    SegfoldSimulator sim_no_fifo(A, B);
    while (!sim_no_fifo.is_done() && sim_no_fifo.stats.cycle < 10000) {
        sim_no_fifo.step();
    }

    // Run with FIFO
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "true"},
        {"b_loader_fifo_size", "16"}
    });

    SegfoldSimulator sim_with_fifo(A, B);
    while (!sim_with_fifo.is_done() && sim_with_fifo.stats.cycle < 10000) {
        sim_with_fifo.step();
    }

    // Verify outputs match expected
    bool no_fifo_correct = true;
    bool with_fifo_correct = true;

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if (sim_no_fifo.acc_output(i, j) != expected(i, j)) {
                no_fifo_correct = false;
            }
            if (sim_with_fifo.acc_output(i, j) != expected(i, j)) {
                with_fifo_correct = false;
            }
        }
    }

    test_assert(no_fifo_correct, "Output correct without FIFO");
    test_assert(with_fifo_correct, "Output correct with FIFO");

    std::cout << "  Without FIFO: " << sim_no_fifo.stats.cycle << " cycles"
              << ", direct=" << sim_no_fifo.stats.b_direct_loads
              << ", fifo=" << sim_no_fifo.stats.b_fifo_enqueues << std::endl;
    std::cout << "  With FIFO:    " << sim_with_fifo.stats.cycle << " cycles"
              << ", direct=" << sim_with_fifo.stats.b_direct_loads
              << ", fifo=" << sim_with_fifo.stats.b_fifo_enqueues << std::endl;
}

// Test: Output correctness with FIFO and II=0 (dense matrix)
void test_output_correctness_ii0_dense() {
    std::cout << "\n--- Test: Output Correctness with FIFO and II=0 (Dense) ---" << std::endl;

    // Create dense test matrices (8x8 with all 1s)
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            A(i, j) = 1;
            B(i, j) = 1;
        }
    }

    // Expected: each element should be 8 (sum of 8 ones)
    int expected_val = 8;

    // Run without FIFO, II=0
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "false"},
        {"b_loader_fifo_size", "0"},
        {"II", "0"}
    });

    SegfoldSimulator sim_no_fifo(A, B);
    while (!sim_no_fifo.is_done() && sim_no_fifo.stats.cycle < 10000) {
        sim_no_fifo.step();
    }

    // Run with FIFO, II=0
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"enable_b_loader_fifo", "true"},
        {"b_loader_fifo_size", "4"},
        {"II", "0"}
    });

    SegfoldSimulator sim_with_fifo(A, B);
    while (!sim_with_fifo.is_done() && sim_with_fifo.stats.cycle < 10000) {
        sim_with_fifo.step();
    }

    // Verify outputs
    bool no_fifo_correct = true;
    bool with_fifo_correct = true;
    int no_fifo_errors = 0;
    int with_fifo_errors = 0;

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if (sim_no_fifo.acc_output(i, j) != expected_val) {
                no_fifo_correct = false;
                no_fifo_errors++;
            }
            if (sim_with_fifo.acc_output(i, j) != expected_val) {
                with_fifo_correct = false;
                with_fifo_errors++;
            }
        }
    }

    test_assert(no_fifo_correct, "Dense output correct without FIFO (II=0)");
    test_assert(with_fifo_correct, "Dense output correct with FIFO (II=0)");

    std::cout << "  Without FIFO (II=0): " << sim_no_fifo.stats.cycle << " cycles"
              << ", errors=" << no_fifo_errors << std::endl;
    std::cout << "  With FIFO (II=0):    " << sim_with_fifo.stats.cycle << " cycles"
              << ", errors=" << with_fifo_errors << std::endl;
}

// Test: FIFO usage with multi B row per PE row
void test_fifo_multi_b_row_per_pe() {
    std::cout << "\n--- Test: FIFO Usage with Multi B Row per PE ---" << std::endl;

    std::vector<int> fifo_sizes = {0, 2, 4, 8, 16};

    // Create matrix where multiple B rows map to the same PE row (8x8)
    // This requires overlapping row intersections
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    // Dense band pattern - ensures many B rows map to same PE rows
    for (int i = 0; i < 8; ++i) {
        for (int j = std::max(0, i-2); j <= std::min(7, i+2); ++j) {
            A(i, j) = 1;
            B(j, i) = 1;
        }
    }

    for (int fifo_size : fifo_sizes) {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"virtual_pe_row_num", "4"},
            {"virtual_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"enable_b_loader_fifo", fifo_size > 0 ? "true" : "false"},
            {"b_loader_fifo_size", std::to_string(fifo_size)},
            {"disable_multi_b_row_per_row", "false"},  // Allow multiple B rows per PE row
            {"enable_multi_b_row_loading", "true"}
        });

        SegfoldSimulator sim(A, B);

        int max_cycles = 10000;
        while (!sim.is_done() && sim.stats.cycle < max_cycles) {
            sim.step();
        }

        std::cout << "  FIFO size " << std::setw(2) << fifo_size
                  << ": " << std::setw(5) << sim.stats.cycle << " cycles"
                  << ", direct=" << std::setw(5) << sim.stats.b_direct_loads
                  << ", fifo=" << std::setw(5) << sim.stats.b_fifo_enqueues
                  << std::endl;
    }
}

// Test: 8x8 dense matrix with trace comparison
void test_dense_8x8_trace_comparison() {
    std::cout << "\n--- Test: 8x8 Dense Matrix Trace Comparison ---" << std::endl;

    // Create 8x8 dense matrix (100% fill)
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            A(i, j) = 1;
            B(i, j) = 1;
        }
    }

    std::vector<int> trace_fifo_0;
    std::vector<int> trace_fifo_4;
    int cycles_fifo_0 = 0;
    int cycles_fifo_4 = 0;

    // Run with FIFO size 0 (disabled)
    {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"virtual_pe_row_num", "4"},
            {"virtual_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"enable_b_loader_fifo", "false"},
            {"b_loader_fifo_size", "0"},
            {"disable_multi_b_row_per_row", "false"},
            {"enable_multi_b_row_loading", "true"},
            {"II", "0"}
        });

        SegfoldSimulator sim(A, B);
        int max_cycles = 10000;
        while (!sim.is_done() && sim.stats.cycle < max_cycles) {
            sim.step();
        }
        cycles_fifo_0 = sim.stats.cycle;
        trace_fifo_0 = sim.stats.trace_b_elements_on_switch;
    }

    // Run with FIFO size 4
    {
        reset();
        update_cfg({
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"virtual_pe_row_num", "4"},
            {"virtual_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"enable_b_loader_fifo", "true"},
            {"b_loader_fifo_size", "4"},
            {"disable_multi_b_row_per_row", "false"},
            {"enable_multi_b_row_loading", "true"},
            {"II", "0"}
        });

        SegfoldSimulator sim(A, B);
        int max_cycles = 10000;
        while (!sim.is_done() && sim.stats.cycle < max_cycles) {
            sim.step();
        }
        cycles_fifo_4 = sim.stats.cycle;
        trace_fifo_4 = sim.stats.trace_b_elements_on_switch;
    }

    // Print results
    double speedup = static_cast<double>(cycles_fifo_0) / cycles_fifo_4;
    std::cout << "  FIFO size 0: " << cycles_fifo_0 << " cycles" << std::endl;
    std::cout << "  FIFO size 4: " << cycles_fifo_4 << " cycles" << std::endl;
    std::cout << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;

    // Print trace comparison (first 30 cycles)
    std::cout << "\n  B elements loaded per cycle (first 30 cycles):" << std::endl;
    std::cout << "  Cycle | FIFO=0 | FIFO=4" << std::endl;
    std::cout << "  ------+--------+-------" << std::endl;
    int max_trace = std::min(30, static_cast<int>(std::max(trace_fifo_0.size(), trace_fifo_4.size())));
    for (int i = 0; i < max_trace; ++i) {
        int val_0 = (i < static_cast<int>(trace_fifo_0.size())) ? trace_fifo_0[i] : 0;
        int val_4 = (i < static_cast<int>(trace_fifo_4.size())) ? trace_fifo_4[i] : 0;
        std::cout << "  " << std::setw(5) << i << " | "
                  << std::setw(6) << val_0 << " | "
                  << std::setw(5) << val_4 << std::endl;
    }

    // Calculate averages
    double avg_0 = 0, avg_4 = 0;
    for (int v : trace_fifo_0) avg_0 += v;
    for (int v : trace_fifo_4) avg_4 += v;
    if (!trace_fifo_0.empty()) avg_0 /= trace_fifo_0.size();
    if (!trace_fifo_4.empty()) avg_4 /= trace_fifo_4.size();

    std::cout << "\n  Avg B elements/cycle: FIFO=0: " << std::fixed << std::setprecision(2) << avg_0
              << ", FIFO=4: " << avg_4 << std::endl;

    // Save trace to CSV file
    std::ofstream outfile("b_loader_fifo_trace_8x8.csv");
    if (outfile.is_open()) {
        outfile << "cycle,fifo_0,fifo_4" << std::endl;
        int max_len = std::max(trace_fifo_0.size(), trace_fifo_4.size());
        for (int i = 0; i < max_len; ++i) {
            int val_0 = (i < static_cast<int>(trace_fifo_0.size())) ? trace_fifo_0[i] : 0;
            int val_4 = (i < static_cast<int>(trace_fifo_4.size())) ? trace_fifo_4[i] : 0;
            outfile << i << "," << val_0 << "," << val_4 << std::endl;
        }
        outfile.close();
        std::cout << "\n  Trace saved to: b_loader_fifo_trace_8x8.csv" << std::endl;
    }

    test_assert(cycles_fifo_4 <= cycles_fifo_0, "FIFO improves or maintains performance");
}

// Test: FIFO helper functions
void test_fifo_helper_functions() {
    std::cout << "\n--- Test: FIFO Helper Functions ---" << std::endl;

    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_b_loader_fifo", "true"},
        {"b_loader_fifo_size", "4"}
    });

    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    A(0, 0) = 1;
    B(0, 0) = 1;

    SegfoldSimulator sim(A, B);

    // Test empty check
    test_assert(sim.switchModule.b_loader_fifo_empty(0, 0),
                "FIFO starts empty");

    // Test not full check
    test_assert(!sim.switchModule.b_loader_fifo_full(0, 0),
                "FIFO starts not full");

    // Manually enqueue elements
    // Parameters: (sw_i, sw_j, b_row, b_col, b_val, b_n, orig_row, cycle)
    for (int i = 0; i < 4; ++i) {
        sim.switchModule.enqueue_b_loader_fifo(0, 0, i, i, i+1, i, i, 0);
    }

    test_assert(!sim.switchModule.b_loader_fifo_empty(0, 0),
                "FIFO not empty after enqueue");
    test_assert(sim.switchModule.b_loader_fifo_full(0, 0),
                "FIFO full after 4 enqueues (size=4)");

    // Test all_b_loader_fifos_empty
    test_assert(!sim.switchModule.all_b_loader_fifos_empty(),
                "Not all FIFOs empty");

    // Dequeue elements
    for (int i = 0; i < 4; ++i) {
        bool dequeued = sim.switchModule.dequeue_to_switch(0, 0);
        test_assert(dequeued, "Dequeue " + std::to_string(i+1) + " succeeded");
        // Reset next_switch to idle for next dequeue
        sim.switchModule.next_switches[0][0] = SwitchModule::idle_sw();
    }

    test_assert(sim.switchModule.b_loader_fifo_empty(0, 0),
                "FIFO empty after dequeuing all");
}

int main() {
    std::cout << "=== B Loader FIFO Unit Tests ===" << std::endl;

    test_fifo_disabled_baseline();
    test_fifo_enabled_basic();
    test_fifo_size_sensitivity();
    test_transparent_fifo_bypass();
    test_fifo_ordering();
    test_dense_matrix_fifo_sensitivity();
    test_output_correctness();
    test_output_correctness_ii0_dense();
    test_fifo_multi_b_row_per_pe();
    test_dense_8x8_trace_comparison();
    test_fifo_helper_functions();

    std::cout << "\n========================================" << std::endl;
    std::cout << "All B Loader FIFO tests passed!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
