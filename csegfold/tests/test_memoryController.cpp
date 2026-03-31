#include "csegfold/modules/memoryController.hpp"
#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>
#include <unordered_set>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_memoryController_creation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "8"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    test_assert(controller.matrix == &matrix, "MemoryController stores matrix pointer");
    test_assert(controller.enable_memory_hierarchy == false, "enable_memory_hierarchy is false");
    test_assert(controller.B_csr.size() > 0, "B_csr is populated");
    test_assert(controller.cnt == controller.cfg.II, "cnt initialized to II");
}

void test_filter_intersections() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    test_assert(controller.B_rows_to_load.size() > 0, "B_rows_to_load is populated");
    
    // B_rows_to_load contains tiled B row indices, not original B row indices
    // After tiling, B_indexed has more rows than the original B matrix
    int tiled_B_rows = matrix.B_indexed.rows_;
    bool all_rows_valid = true;
    for (int row : controller.B_rows_to_load) {
        if (row < 0 || row >= tiled_B_rows) {
            all_rows_valid = false;
            break;
        }
    }
    test_assert(all_rows_valid, "All B_rows_to_load indices are valid (checked against tiled B matrix size)");
}

void test_fill_b_loader_window() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "4"},
        {"enable_tile_eviction", "false"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    test_assert(controller.active_indices.size() > 0, "active_indices is populated");
    test_assert(static_cast<int>(controller.active_indices.size()) <= controller.cfg.b_loader_window_size, 
                "active_indices size <= b_loader_window_size");
    test_assert(controller.lptr > 0, "lptr is advanced");
}

void test_check_b_loader_tile() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_tile_eviction", "false"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    auto [is_append, is_evict] = controller.check_b_loader_tile(0);
    test_assert(is_append == true, "check_b_loader_tile returns is_append=true when eviction disabled");
    test_assert(is_evict == false, "check_b_loader_tile returns is_evict=false when eviction disabled");
}

void test_get_is_done() {
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
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    bool initially_done = controller.get_is_done();
    test_assert(initially_done == false, "Initially not done (has active_indices)");
    
    controller.active_indices.clear();
    controller.lptr = static_cast<int>(controller.B_rows_to_load.size());
    bool should_be_done = controller.get_is_done();
    test_assert(should_be_done == true, "Done when active_indices empty and lptr >= B_rows_to_load.size()");
}

void test_get_element_pointers() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_a_csc", "false"},
        {"a_pointer_offset", "0"},
        {"b_pointer_offset", "1000"},
        {"c_pointer_offset", "2000"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1; A(0, 1) = 1;
    A(1, 1) = 1;
    B(0, 0) = 1; B(1, 1) = 1;
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    int a_ptr = controller.get_a_element_pointer(0, 0);
    test_assert(a_ptr >= controller.cfg.a_pointer_offset, "A pointer >= a_pointer_offset");
    
    int b_ptr = controller.get_b_element_pointer(0, 0);
    test_assert(b_ptr >= controller.cfg.b_pointer_offset, "B pointer >= b_pointer_offset");
    
    int c_ptr = controller.get_c_element_pointer(0, 0);
    test_assert(c_ptr >= controller.cfg.c_pointer_offset, "C pointer >= c_pointer_offset");
    
    int a_ptr2 = controller.get_a_element_pointer(0, 1);
    test_assert(a_ptr2 > a_ptr, "A pointer increases with offset");
}

void test_get_a_element_pointer_csc() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_a_csc", "true"},
        {"a_pointer_offset", "0"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1; A(1, 0) = 1;
    B(0, 0) = 1;
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    int ptr1 = controller.get_a_element_pointer(0, 0);
    int ptr2 = controller.get_a_element_pointer(1, 0);
    
    test_assert(ptr1 >= 0, "A pointer (CSC) for (0,0) >= 0");
    test_assert(ptr2 >= 0, "A pointer (CSC) for (1,0) >= 0");
    test_assert(ptr1 != ptr2 || ptr1 == 0, "A pointers (CSC) are different or both zero");
}

void test_remove_completed_rows() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "8"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    int initial_size = static_cast<int>(controller.active_indices.size());
    int initial_completed = controller.n_completed_rows;
    
    std::unordered_set<int> completed = {controller.active_indices[0]};
    controller.remove_completed_rows(completed);
    
    test_assert(controller.n_completed_rows == initial_completed + 1, "n_completed_rows incremented");
    test_assert(static_cast<int>(controller.active_indices.size()) <= initial_size, 
                "active_indices size decreased or stayed same");
    
    bool removed = true;
    for (int row : completed) {
        for (int idx : controller.active_indices) {
            if (idx == row) {
                removed = false;
                break;
            }
        }
    }
    test_assert(removed, "Completed rows removed from active_indices");
}

void test_b_loader_limit_and_offset() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_partial_b_load", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1; B(0, 1) = 1; B(0, 2) = 1;
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    test_assert(controller.b_loader_limit.size() > 0, "b_loader_limit is populated");
    test_assert(controller.b_loader_offset.size() > 0, "b_loader_offset is populated (partial load enabled)");
    
    if (controller.b_loader_limit.find(0) != controller.b_loader_limit.end()) {
        test_assert(controller.b_loader_limit[0] == 3, "b_loader_limit[0] = 3 (3 NNZ in row 0)");
    }
    
    if (controller.b_loader_offset.find(0) != controller.b_loader_offset.end()) {
        test_assert(controller.b_loader_offset[0] == 0, "b_loader_offset[0] = 0 initially");
    }
}

void test_ready_to_evict() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_tile_eviction", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    test_assert(controller.ready_to_evict.size() == 4, "ready_to_evict has 4 entries");
    bool has_true = false;
    for (bool val : controller.ready_to_evict) {
        if (val) {
            has_true = true;
            break;
        }
    }
    test_assert(has_true, "ready_to_evict set to true when first row is added (tile eviction enabled)");
}

void test_memory_hierarchy_early_returns() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    controller.send("test");
    controller.flush_requests();
    controller.filter_requests();
    controller.reset_request_id();
    
    test_assert(true, "Memory hierarchy functions return early without errors");
}

void test_get_awaiting_b_loads() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "8"},
        {"enable_partial_b_load", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    // Create matrices with intersections
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    controller.filter_intersections();
    controller.fill_b_loader_window();
    
    int awaiting = controller.get_awaiting_b_loads();
    test_assert(awaiting >= 0, "get_awaiting_b_loads returns non-negative value");
    
    if (controller.active_indices.size() > 0) {
        test_assert(awaiting > 0, "get_awaiting_b_loads returns positive value when active indices exist");
    }
}

void test_get_awaiting_b_loads_partial() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "8"},
        {"enable_partial_b_load", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    controller.filter_intersections();
    controller.fill_b_loader_window();
    
    // Set partial load offsets
    for (int r : controller.active_indices) {
        controller.b_loader_offset[r] = 0;
        controller.b_loader_limit[r] = 2;  // Only load first 2 elements
    }
    
    int awaiting = controller.get_awaiting_b_loads();
    test_assert(awaiting >= 0, "get_awaiting_b_loads with partial load returns non-negative value");
}

void test_get_awaiting_b_loads_no_intersections() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"b_loader_window_size", "8"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    // A and B have no intersections
    A(0, 0) = 1;
    B(3, 3) = 1;  // Different positions
    
    MatrixLoader matrix(A, B);
    MemoryController controller(&matrix);
    
    controller.filter_intersections();
    controller.fill_b_loader_window();
    
    int awaiting = controller.get_awaiting_b_loads();
    test_assert(awaiting == 0, "get_awaiting_b_loads returns 0 when no intersections");
}

int main() {
    std::cout << "\n=== Testing MemoryController Module ===" << std::endl;
    
    std::cout << "\n--- Basic MemoryController Tests ---" << std::endl;
    test_memoryController_creation();
    test_b_loader_limit_and_offset();
    test_ready_to_evict();
    
    std::cout << "\n--- Filter and Window Tests ---" << std::endl;
    test_filter_intersections();
    test_fill_b_loader_window();
    test_check_b_loader_tile();
    
    std::cout << "\n--- Element Pointer Tests ---" << std::endl;
    test_get_element_pointers();
    test_get_a_element_pointer_csc();
    
    std::cout << "\n--- State Management Tests ---" << std::endl;
    test_get_is_done();
    test_remove_completed_rows();
    
    std::cout << "\n--- Memory Hierarchy Tests ---" << std::endl;
    test_memory_hierarchy_early_returns();
    
    std::cout << "\n--- Awaiting B Loads Tests ---" << std::endl;
    test_get_awaiting_b_loads();
    test_get_awaiting_b_loads_partial();
    test_get_awaiting_b_loads_no_intersections();
    
    std::cout << "\n=== All MemoryController Tests Passed! ===" << std::endl;
    return 0;
}

