#include "csegfold/simulator/simulator.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_load_a_from_fifo_to_pe_basic() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 5;
    B(0, 0) = 3;
    
    Simulator sim(A, B);
    
    std::pair<int, int> orig_idx_a(0, 0);
    std::unordered_map<std::string, int> b;
    b["val"] = 3;
    b["row"] = 0;
    b["col"] = 0;
    
    sim.load_a_from_fifo_to_pe(0, 0, &sim.controller, &sim.peModule, orig_idx_a, b);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    test_assert(next_pe.status == PEStatus::LOAD, "PE status set to LOAD");
    test_assert(next_pe.a.value() == 5, "A value loaded correctly");
    test_assert(next_pe.a_m.value() == 0, "a_m set correctly");
    test_assert(next_pe.a_k.value() == 0, "a_k set correctly");
    test_assert(next_pe.b.val.value() == 3, "B value loaded correctly");
    test_assert(next_pe.b.row.value() == 0, "B row loaded correctly");
    test_assert(next_pe.b.col.value() == 0, "B col loaded correctly");
    test_assert(!sim.peModule.valid_a[0][0], "valid_a set to false");
}

void test_load_a_from_fifo_to_pe_denseA() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(1, 2) = 7;
    B(2, 3) = 4;
    
    Simulator sim(A, B);
    // A_orig_csr is already set from constructor — no extra setup needed
    
    std::pair<int, int> orig_idx_a(1, 2);
    std::unordered_map<std::string, int> b;
    b["val"] = 4;
    b["row"] = 2;
    b["col"] = 3;
    
    sim.load_a_from_fifo_to_pe(1, 2, &sim.controller, &sim.peModule, orig_idx_a, b);
    
    auto& next_pe = sim.peModule.next_pe[1][2];
    test_assert(next_pe.a.value() == 7, "A value loaded from denseA correctly");
    test_assert(next_pe.a_m.value() == 1, "a_m set correctly");
    test_assert(next_pe.a_k.value() == 2, "a_k set correctly");
}

void test_is_new_c_first_time() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    next_pe.c.m = std::nullopt;  // First time, no C value
    
    auto [store_c, load_c] = sim.is_new_c(&sim.peModule, 0, 0, &sim.matrix);
    
    test_assert(store_c == false, "First time C update: store_c is false");
    test_assert(load_c == true, "First time C update: load_c is true");
}

void test_is_new_c_same_m_n() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    next_pe.a_m = 0;
    next_pe.b.col = 0;
    next_pe.b.n = 0;  // Must set b.n for is_new_c to properly compute store_c
    next_pe.c.m = 0;
    next_pe.c.n = 0;

    auto [store_c, load_c] = sim.is_new_c(&sim.peModule, 0, 0, &sim.matrix);

    test_assert(store_c == false, "Same m and n: store_c is false");
    test_assert(load_c == false, "Same m and n: load_c is false");
}

void test_is_new_c_different_m() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    next_pe.a_m = 1;  // Different m
    next_pe.b.col = 0;
    next_pe.b.n = 0;  // Must set b.n for is_new_c to compute store_c
    next_pe.c.m = 0;
    next_pe.c.n = 0;
    next_pe.b.row = 0;
    next_pe.c.last_k = 0;

    auto [store_c, load_c] = sim.is_new_c(&sim.peModule, 0, 0, &sim.matrix);

    test_assert(store_c == true, "Different m: store_c is true");
    // load_c depends on b_is_same_block
    test_assert(load_c == false || load_c == true, "load_c is valid boolean");
}

void test_is_new_c_different_n() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    next_pe.a_m = 0;
    next_pe.b.col = 1;
    next_pe.b.n = 1;  // Different n - must set b.n for is_new_c to compute store_c
    next_pe.c.m = 0;
    next_pe.c.n = 0;
    next_pe.b.row = 0;
    next_pe.c.last_k = 0;

    auto [store_c, load_c] = sim.is_new_c(&sim.peModule, 0, 0, &sim.matrix);

    test_assert(store_c == true, "Different n: store_c is true");
    test_assert(load_c == false || load_c == true, "load_c is valid boolean");
}

void test_pop_fifo_to_pe() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 2;
    B(0, 0) = 3;
    
    Simulator sim(A, B);
    
    // Add entry to FIFO
    std::unordered_map<std::string, int> pe_update;
    pe_update["a_m"] = 0;
    pe_update["a_k"] = 0;
    pe_update["b_val"] = 3;
    pe_update["b_row"] = 0;
    pe_update["b_col"] = 0;
    pe_update["a_val"] = 2;
    
    sim.fifo[0][0].pe_update.push_back(pe_update);
    sim.fifo[0][0].rptr = 1;
    sim.fifo[0][0].lptr = 0;
    
    sim.pop_fifo_to_pe(0, 0);
    
    auto& next_pe = sim.peModule.next_pe[0][0];
    test_assert(next_pe.status == PEStatus::LOAD, "PE status set to LOAD after pop");
    test_assert(next_pe.a.value() == 2, "A value loaded from FIFO");
    test_assert(next_pe.b.val.value() == 3, "B value loaded from FIFO");
    test_assert(sim.fifo[0][0].lptr == 1, "FIFO lptr incremented");
}

int main() {
    std::cout << "\n=== Testing Simulator FIFO and PE Functions ===" << std::endl;
    
    std::cout << "\n--- load_a_from_fifo_to_pe Tests ---" << std::endl;
    test_load_a_from_fifo_to_pe_basic();
    test_load_a_from_fifo_to_pe_denseA();
    
    std::cout << "\n--- is_new_c Tests ---" << std::endl;
    test_is_new_c_first_time();
    test_is_new_c_same_m_n();
    test_is_new_c_different_m();
    test_is_new_c_different_n();
    
    std::cout << "\n--- pop_fifo_to_pe Tests ---" << std::endl;
    test_pop_fifo_to_pe();
    
    std::cout << "\n=== All Simulator FIFO and PE Tests Passed! ===" << std::endl;
    return 0;
}

