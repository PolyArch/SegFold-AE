#include "csegfold/modules/spad.hpp"
#include "csegfold/modules/module.hpp"
#include <iostream>
#include <tuple>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_spad_creation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Just test that it creates without crashing
    test_assert(true, "SPAD module created successfully");
}

void test_spad_store_load() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Store data
    std::unordered_map<int, std::tuple<int, int, int>> c_data;
    c_data[0] = std::make_tuple(42, 1, 2);  // val=42, m=1, n=2
    
    bool stored = spad.store(0, 5, c_data);  // row=0, c_col=5
    test_assert(stored, "Store succeeds");
    test_assert(spad.stats.spad_stores == 1, "Store stats updated");
    
    // Load the same data
    auto [loaded, retrieved] = spad.load(0, 5);
    test_assert(loaded, "Load succeeds");
    test_assert(retrieved.has_value(), "Data retrieved");
    test_assert(retrieved.value().size() == 1, "Retrieved 1 entry");
    
    auto [val, m, n] = retrieved.value()[0];
    test_assert(val == 42, "Retrieved value correct");
    test_assert(m == 1, "Retrieved m correct");
    test_assert(n == 2, "Retrieved n correct");
    test_assert(spad.stats.spad_load_hits == 1, "Load hit stats updated");
}

void test_spad_load_miss() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Load from empty SPAD
    auto [loaded, retrieved] = spad.load(0, 10);
    test_assert(loaded, "Load succeeds even if data not present");
    test_assert(!retrieved.has_value(), "No data retrieved (miss)");
    test_assert(spad.stats.spad_load_misses == 1, "Load miss stats updated");
}

void test_spad_valid_flags() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Store uses up valid flag
    std::unordered_map<int, std::tuple<int, int, int>> c_data;
    c_data[0] = std::make_tuple(1, 0, 0);
    
    bool stored1 = spad.store(0, 0, c_data);
    test_assert(stored1, "First store succeeds");
    
    bool stored2 = spad.store(0, 1, c_data);
    test_assert(!stored2, "Second store fails (valid flag consumed)");
    
    // Reset valid flags
    spad.reset_valid();
    
    bool stored3 = spad.store(0, 1, c_data);
    test_assert(stored3, "Store succeeds after reset_valid");
}

void test_spad_clear_row() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Store some data
    std::unordered_map<int, std::tuple<int, int, int>> c_data;
    c_data[0] = std::make_tuple(1, 0, 0);
    spad.store(0, 5, c_data);
    spad.reset_valid();
    spad.store(0, 10, c_data);
    
    // Clear row 0
    spad.clear_row(0);
    
    spad.reset_valid();
    auto [loaded, retrieved] = spad.load(0, 5);
    test_assert(!retrieved.has_value(), "Data cleared from row");
}

void test_spad_clear_all() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SPADModule spad;
    
    // Store data in multiple rows
    std::unordered_map<int, std::tuple<int, int, int>> c_data;
    c_data[0] = std::make_tuple(1, 0, 0);
    spad.store(0, 0, c_data);
    spad.reset_valid();
    spad.store(1, 0, c_data);
    
    spad.clear();
    
    // Check all rows cleared
    spad.reset_valid();
    auto [loaded0, retrieved0] = spad.load(0, 0);
    auto [loaded1, retrieved1] = spad.load(1, 0);
    test_assert(!retrieved0.has_value(), "Row 0 cleared");
    test_assert(!retrieved1.has_value(), "Row 1 cleared");
}

int main() {
    std::cout << "\n=== Testing SPAD Module ===" << std::endl;
    
    test_spad_creation();
    test_spad_store_load();
    test_spad_load_miss();
    test_spad_valid_flags();
    test_spad_clear_row();
    test_spad_clear_all();
    
    std::cout << "\n=== All SPAD Tests Passed! ===" << std::endl;
    return 0;
}

