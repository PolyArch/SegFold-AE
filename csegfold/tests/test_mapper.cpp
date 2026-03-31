#include "csegfold/modules/mapper.hpp"
#include "csegfold/modules/module.hpp"
#include <iostream>

using namespace csegfold;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_mapper_creation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    test_assert(mapper.mapping.size() == 4, "Mapper has 4 physical rows");
    test_assert(mapper.mapping[0].size() == 4, "Mapper has 4 physical cols");
    test_assert(mapper.is_fold.size() == 4, "is_fold vector size");
    test_assert(mapper.request_counter == 0, "Initial request counter");
}

void test_mapper_basic_mapping() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    // Map virtual (0,0) to physical space
    auto pos = mapper.map(0, 0);
    test_assert(pos.has_value(), "First mapping succeeds");
    test_assert(pos.value().first == 0 && pos.value().second == 0, "Maps to (0,0)");
    test_assert(mapper.is_mapped(0, 0), "Virtual (0,0) is mapped");
    test_assert(mapper.get_row_length(0) == 1, "Row 0 has length 1");
}

void test_mapper_row_extension() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    // Map multiple positions in same row
    mapper.map(0, 0);
    mapper.map(0, 1);
    mapper.map(0, 2);
    
    test_assert(mapper.get_row_length(0) == 3, "Row 0 has 3 mapped positions");
    test_assert(mapper.is_mapped(0, 0), "Virtual (0,0) mapped");
    test_assert(mapper.is_mapped(0, 1), "Virtual (0,1) mapped");
    test_assert(mapper.is_mapped(0, 2), "Virtual (0,2) mapped");
}

void test_mapper_out_of_bounds() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    test_assert(mapper.out_of_bounds(-1, 0), "Negative row is out of bounds");
    test_assert(mapper.out_of_bounds(0, -1), "Negative col is out of bounds");
    test_assert(mapper.out_of_bounds(4, 0), "Row >= prows is out of bounds");
    test_assert(mapper.out_of_bounds(0, 4), "Col >= pcols is out of bounds");
    test_assert(!mapper.out_of_bounds(0, 0), "(0,0) is not out of bounds");
    test_assert(!mapper.out_of_bounds(3, 3), "(3,3) is not out of bounds");
}

void test_mapper_occupied() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    test_assert(!mapper.is_occupied(0, 0), "Initially (0,0) not occupied");
    
    mapper.map(0, 0);
    test_assert(mapper.is_occupied(0, 0), "After mapping, (0,0) is occupied");
}

void test_mapper_eviction() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    // Map a row
    mapper.map(1, 0);
    mapper.map(1, 1);
    mapper.map(1, 2);
    
    test_assert(mapper.get_row_length(1) == 3, "Row 1 has 3 positions before eviction");
    
    mapper.evict_b_rows(1);
    
    test_assert(mapper.get_row_length(1) == 0, "Row 1 has 0 positions after eviction");
    test_assert(!mapper.is_mapped(1, 0), "Virtual (1,0) unmapped after eviction");
    test_assert(!mapper.is_mapped(1, 1), "Virtual (1,1) unmapped after eviction");
    test_assert(!mapper.is_mapped(1, 2), "Virtual (1,2) unmapped after eviction");
}

void test_mapper_request_limit() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"mapper_request_limit_per_cycle", "3"}
    });
    
    Mapper mapper;
    
    // Map up to limit
    auto pos1 = mapper.map(0, 0);
    auto pos2 = mapper.map(0, 1);
    auto pos3 = mapper.map(0, 2);
    auto pos4 = mapper.map(0, 3);  // Should fail due to limit
    
    test_assert(pos1.has_value(), "First request succeeds");
    test_assert(pos2.has_value(), "Second request succeeds");
    test_assert(pos3.has_value(), "Third request succeeds");
    test_assert(!pos4.has_value(), "Fourth request fails (exceeds limit)");
    
    // Reset counter and try again
    mapper.reset_request_counter();
    auto pos5 = mapper.map(0, 3);
    test_assert(pos5.has_value(), "Request succeeds after counter reset");
}

void test_mapper_physical_coords() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Mapper mapper;
    
    mapper.map(1, 5);
    
    auto phys = mapper.get_physical_coords(1, 5);
    test_assert(phys.has_value(), "Physical coords exist for mapped virtual coords");
    
    auto virt = mapper.get_virtual_coords(phys.value().first, phys.value().second);
    test_assert(virt.has_value(), "Virtual coords can be retrieved from physical");
    test_assert(virt.value().first == 1 && virt.value().second == 5, 
                "Round-trip virtual->physical->virtual");
}

int main() {
    std::cout << "\n=== Testing Mapper Module ===" << std::endl;
    
    std::cout << "\n--- Basic Mapper Tests ---" << std::endl;
    test_mapper_creation();
    test_mapper_basic_mapping();
    test_mapper_row_extension();
    test_mapper_out_of_bounds();
    test_mapper_occupied();
    test_mapper_eviction();
    test_mapper_request_limit();
    test_mapper_physical_coords();
    
    std::cout << "\n=== All Mapper Tests Passed! ===" << std::endl;
    
    std::cout << "\n=== Testing PE Module ===" << std::endl;
    test_pe_idle_creation();
    test_pe_active();
    test_pe_load_done();
    test_pe_mac_done();
    test_pe_module_creation();
    test_pe_update_c();
    test_pe_reset_next();
    test_pe_free_operations();
    
    std::cout << "\n=== All PE Tests Passed! ===" << std::endl;
    return 0;
}




