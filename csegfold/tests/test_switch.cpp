#include "csegfold/modules/switch.hpp"
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

void test_switch_idle_creation() {
    SwitchState idle = SwitchModule::idle_sw();
    
    test_assert(idle.status == SwitchStatus::IDLE, "Idle switch status");
    test_assert(!idle.b.val.has_value(), "Idle switch has no B value");
    test_assert(!idle.b.row.has_value(), "Idle switch has no B row");
    test_assert(!idle.b.col.has_value(), "Idle switch has no B col");
    test_assert(!idle.loadB_cycle.has_value(), "Idle switch has no loadB_cycle");
    test_assert(!idle.c_col.has_value(), "Idle switch has no c_col");
}

void test_switch_active() {
    SwitchState sw = SwitchModule::idle_sw();
    test_assert(!sw_active(sw), "Idle switch is not active");
    test_assert(sw_idle(sw), "Idle switch is idle");
    test_assert(!sw_has_c_col(sw), "Idle switch has no c_col");
    
    sw.status = SwitchStatus::MOVE;
    sw.b.val = 5;
    test_assert(sw_active(sw), "Switch with B value is active");
    test_assert(!sw_idle(sw), "Active switch is not idle");
    
    sw.b.val = 0;
    test_assert(!sw_active(sw), "Switch with B=0 is not active");
}

void test_switch_load_done() {
    SwitchState sw = SwitchModule::idle_sw();
    sw.status = SwitchStatus::LOAD_B;
    sw.loadB_cycle = 10;
    
    test_assert(!sw_load_done(sw, 10), "Load not done at same cycle");
    test_assert(sw_load_done(sw, 11), "Load done after num_cycles_load_b cycles");
}

void test_switch_module_creation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    SwitchModule module;
    
    test_assert(module.switches.size() == 4, "Switch module has 4 rows");
    test_assert(module.switches[0].size() == 4, "Switch module has 4 cols");
    test_assert(module.next_switches.size() == 4, "Next switches has 4 rows");
    test_assert(module.b_loaded.size() == 4, "b_loaded vector size");
    test_assert(module.row_full.size() == 4, "row_full vector size");
    test_assert(module.num_switches() == 16, "Total switches");
    test_assert(module.num_active_switches() == 0, "No active switches initially");
}

void test_switch_c_comparisons() {
    SwitchState sw;
    sw.status = SwitchStatus::MOVE;
    sw.b.col = 5;
    sw.c_col = 5;
    
    test_assert(SwitchModule::c_eq_b(sw), "C column equals B column");
    
    sw.c_col = 3;
    test_assert(SwitchModule::c_lt_b(sw), "C column less than B column");
    
    sw.c_col = 7;
    test_assert(SwitchModule::c_gt_b(sw), "C column greater than B column");
}

void test_switch_b_zero() {
    SwitchState sw;
    sw.b.val = 0;
    test_assert(SwitchModule::b_zero(sw), "B is zero");
    
    sw.b.val = 5;
    test_assert(!SwitchModule::b_zero(sw), "B is non-zero");
    
    sw.b.val = std::nullopt;
    test_assert(SwitchModule::b_zero(sw), "B nullopt is considered zero");
}

void test_switch_reset_next() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "2"},
        {"physical_pe_col_num", "2"},
        {"virtual_pe_row_num", "2"},
        {"virtual_pe_col_num", "2"}
    });
    
    SwitchModule module;
    
    // Set some values
    module.next_switches[0][0].status = SwitchStatus::MOVE;
    module.next_switches[0][0].b.val = 10;
    
    module.reset_next();
    
    test_assert(module.next_switches[0][0].status == SwitchStatus::IDLE, "Reset sets status to IDLE");
    test_assert(!module.next_switches[0][0].b.val.has_value(), "Reset clears B value");
}

void test_switch_c_upd() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"c_col_update_per_row", "2"}
    });
    
    SwitchModule module;
    
    module.record_c_upd(0);
    module.record_c_upd(0);
    module.record_c_upd(1);
    
    test_assert(module.c_upd_cnt[0] == 2, "Row 0 has 2 updates");
    test_assert(module.c_upd_cnt[1] == 1, "Row 1 has 1 update");
    test_assert(module.has_remaining_c_upd(), "Has remaining updates");
    
    module.handle_c_upd();
    
    test_assert(module.c_upd_cnt[0] == 0, "Row 0 updates cleared by 2");
    test_assert(module.c_upd_cnt[1] == 0, "Row 1 updates cleared");
}

void test_switch_eviction() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"},
        {"enable_tile_eviction", "true"}
    });
    
    SwitchModule module;
    
    // Set up some switches
    module.next_switches[1][0].b.val = 5;
    module.next_switches[1][1].b.val = 3;
    module.row_full[1] = true;
    
    module.evict_b_rows(1);
    
    test_assert(!module.next_switches[1][0].b.val.has_value(), "Switch[1][0] B cleared");
    test_assert(!module.next_switches[1][1].b.val.has_value(), "Switch[1][1] B cleared");
    test_assert(module.row_full[1] == false, "row_full cleared");
}

int main() {
    std::cout << "\n=== Testing Switch Module ===" << std::endl;
    
    std::cout << "\n--- Switch State Tests ---" << std::endl;
    test_switch_idle_creation();
    test_switch_active();
    test_switch_load_done();
    test_switch_c_comparisons();
    test_switch_b_zero();
    
    std::cout << "\n--- Switch Module Tests ---" << std::endl;
    test_switch_module_creation();
    test_switch_reset_next();
    test_switch_c_upd();
    test_switch_eviction();
    
    std::cout << "\n=== All Switch Tests Passed! ===" << std::endl;
    return 0;
}




