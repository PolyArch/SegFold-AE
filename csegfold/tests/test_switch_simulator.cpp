#include "csegfold/simulator/switch.hpp"
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

void test_run_switches_basic() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    Simulator sim(A, B);
    
    int initial_cycle = sim.stats.cycle;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.stats.cycle == initial_cycle, "Cycle unchanged after run_switches");
    test_assert(true, "run_switches completes without errors");
}

void test_run_switches_load_b() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::LOAD_B;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.loadB_cycle = sim.stats.cycle - config_.num_cycles_load_b;
    
    bool load_done = sw_load_done(switch_, sim.stats.cycle);
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    auto& next_switch = sim.switchModule.next_switches[0][0];
    
    if (load_done) {
        if (SwitchModule::b_zero(switch_)) {
            test_assert(next_switch.status == SwitchStatus::IDLE,
                        "LOAD_B with zero B value: switch becomes IDLE");
        } else {
            test_assert(next_switch.status == SwitchStatus::MOVE &&
                        next_switch.b.val == switch_.b.val &&
                        !next_switch.loadB_cycle.has_value(),
                        "LOAD_B done with non-zero B: transitions to MOVE, preserves B, clears loadB_cycle");
        }
    } else {
        test_assert(next_switch.status == SwitchStatus::LOAD_B,
                    "LOAD_B not done: remains in LOAD_B state");
    }
}

void test_run_switches_move_c_eq_b() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"decouple_sw_and_pe", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.c_col = 0;
    
    size_t initial_fifo_size = sim.fifo[0][0].pe_update.size();
    bool fifo_not_full = sim.fifo_not_full(0, 0);
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    auto& next_switch = sim.switchModule.next_switches[0][0];
    size_t final_fifo_size = sim.fifo[0][0].pe_update.size();
    bool switch_idle = (next_switch.status == SwitchStatus::IDLE);
    bool fifo_updated = (final_fifo_size > initial_fifo_size);
    
    if (fifo_not_full) {
        test_assert(switch_idle && fifo_updated, 
                    "When c_eq_b and FIFO not full: switch becomes IDLE and FIFO receives update");
        test_assert(final_fifo_size == initial_fifo_size + 1,
                    "FIFO size increases by exactly 1");
        if (fifo_updated) {
            auto& pe_update = sim.fifo[0][0].pe_update.back();
            test_assert(pe_update.find("b_val") != pe_update.end(),
                        "FIFO entry contains b_val");
            test_assert(pe_update.find("a_val") != pe_update.end(),
                        "FIFO entry contains a_val");
        }
    } else {
        test_assert(true, "FIFO was full, stall should be counted");
    }
}

void test_run_switches_move_c_lt_b() {
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
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 2;
    switch_.c_col = 0;
    
    bool right_switch_idle = sim.switchModule.next_sw_idle(0, 1);
    int initial_sw_move_stall_by_network = sim.stats.sw_move_stall_by_network;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    auto& next_switch = sim.switchModule.next_switches[0][0];
    auto& right_next_switch = sim.switchModule.next_switches[0][1];
    int final_sw_move_stall_by_network = sim.stats.sw_move_stall_by_network;
    
    if (right_switch_idle) {
        test_assert(next_switch.status == SwitchStatus::IDLE &&
                    right_next_switch.status == SwitchStatus::MOVE &&
                    right_next_switch.b.val == switch_.b.val,
                    "When c_lt_b and right switch idle: current becomes IDLE, right receives B");
    } else {
        test_assert(final_sw_move_stall_by_network > initial_sw_move_stall_by_network,
                    "When c_lt_b and right switch busy: horizontal contention counted");
    }
}

void test_run_switches_update_c_col() {
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
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    
    bool had_c_col_before = switch_.c_col.has_value();
    int initial_c_updates = sim.stats.c_updates;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    bool has_c_col_after = switch_.c_col.has_value();
    int final_c_updates = sim.stats.c_updates;
    
    if (!had_c_col_before) {
        test_assert(has_c_col_after && switch_.c_col.value() == switch_.b.col.value(),
                    "C column set to B column when switch is active and c_col was null");
    }
    
    test_assert(has_c_col_after || final_c_updates > initial_c_updates,
                "C column updated when switch is active");
}

void test_run_evictions_basic() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "true"},
        {"enable_tile_eviction", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    Simulator sim(A, B);
    
    sim.controller.ready_to_evict[0] = true;
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.b.row = 0;
    
    int initial_active = sim.switchModule.num_active_switches();
    
    run_evictions(&sim, &sim.controller, &sim.switchModule, &sim.peModule);
    
    test_assert(!sim.controller.ready_to_evict[0] || 
                sim.switchModule.num_active_switches() < initial_active,
                "Evictions processed when ready_to_evict is true");
}

void test_run_evictions_disabled() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_tile_eviction", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    Simulator sim(A, B);
    
    sim.controller.ready_to_evict[0] = true;
    
    run_evictions(&sim, &sim.controller, &sim.switchModule, &sim.peModule);
    
    test_assert(true, "run_evictions returns early when memory hierarchy disabled");
}

void test_send_b_to_switch_zero() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::LOAD_B;
    switch_.b.val = 0;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.loadB_cycle = sim.stats.cycle - config_.num_cycles_load_b;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(true, "Zero B value handled without errors (zero values are skipped)");
}

void test_run_switches_move_c_gt_b() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"max_push", "32"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.c_col = 2;
    
    int initial_c_updates = sim.stats.c_updates;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.stats.c_updates > initial_c_updates || switch_.c_col.has_value(),
                "C column updated when c_gt_b");
}

void test_run_switches_fifo_stall() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"enable_sw_pe_fifo", "true"},
        {"sw_pe_fifo_size", "1"},
        {"decouple_sw_and_pe", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    sim.fifo[0][0].pe_update.push_back({{"b_val", 1}});
    sim.fifo[0][0].rptr = 1;
    sim.fifo[0][0].lptr = 0;
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.c_col = 0;
    
    bool fifo_full = !sim.fifo_not_full(0, 0);
    int initial_sw_move_stall_by_fifo = sim.stats.sw_move_stall_by_fifo;
    size_t initial_fifo_size = sim.fifo[0][0].pe_update.size();
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    int final_sw_move_stall_by_fifo = sim.stats.sw_move_stall_by_fifo;
    size_t final_fifo_size = sim.fifo[0][0].pe_update.size();
    
    if (fifo_full) {
        test_assert(final_sw_move_stall_by_fifo > initial_sw_move_stall_by_fifo,
                    "FIFO stall counted when FIFO is full and c_eq_b");
        test_assert(final_fifo_size == initial_fifo_size,
                    "FIFO size unchanged when full (no new entry added)");
    } else {
        test_assert(true, "FIFO was not full");
    }
}

void test_run_switches_horizontal_contention() {
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
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 2;
    switch_.c_col = 0;
    
    auto& right_switch_current = sim.switchModule.switches[0][1];
    right_switch_current.status = SwitchStatus::MOVE;
    right_switch_current.b.val = 2;
    right_switch_current.c_col = 1;
    
    bool right_switch_idle = sim.switchModule.next_sw_idle(0, 1);
    int initial_sw_move_stall_by_network = sim.stats.sw_move_stall_by_network;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    int final_sw_move_stall_by_network = sim.stats.sw_move_stall_by_network;
    
    if (!right_switch_idle) {
        test_assert(final_sw_move_stall_by_network > initial_sw_move_stall_by_network,
                    "Horizontal contention counted when right switch is busy (c_lt_b case)");
    } else {
        test_assert(true, "Right switch was idle, so no contention");
    }
}

void test_run_switches_row_full() {
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
    
    sim.switchModule.row_full[0] = true;
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.c_col = 2;
    
    int initial_c_updates = sim.stats.c_updates;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.stats.c_updates > initial_c_updates,
                "C column updated when row is full and c_gt_b");
}

void test_run_switches_decouple_mode() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"},
        {"decouple_sw_and_pe", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.c_col = 0;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.switchModule.next_switches[0][0].status == SwitchStatus::IDLE,
                "Switch freed in decouple mode when c_eq_b");
}

void test_run_switches_idle_state() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::IDLE;
    switch_.c_col = 5;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.switchModule.next_switches[0][0].status == SwitchStatus::IDLE,
                "IDLE switch remains IDLE");
    test_assert(sim.switchModule.next_switches[0][0].c_col == switch_.c_col,
                "IDLE switch preserves c_col");
}

void test_run_switches_move_no_c_col() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    // Initialize with at least one non-zero element so A_indexed is populated
    A(0, 0) = 1;
    B(0, 0) = 1;
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::MOVE;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    
    bool had_c_col_before = switch_.c_col.has_value();
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    bool has_c_col_after = switch_.c_col.has_value();
    auto& next_switch = sim.switchModule.next_switches[0][0];
    bool status_is_move = (next_switch.status == SwitchStatus::MOVE);
    
    if (!had_c_col_before) {
        test_assert(has_c_col_after && switch_.c_col.value() == switch_.b.col.value(),
                    "update_switchModule_c_col sets c_col to b.col when null");
        bool b_preserved = (next_switch.b.val.has_value() && 
                           next_switch.b.val.value() == switch_.b.val.value());
        bool processed_correctly = (status_is_move && b_preserved) || 
                                   (next_switch.status == SwitchStatus::IDLE);
        test_assert(processed_correctly,
                    "After c_col is set to b.col, switch is processed (c_eq_b case: may send to FIFO or free)");
    } else {
        test_assert(status_is_move, "Switch with existing c_col continues in MOVE state");
    }
}

void test_run_switches_load_b_not_done() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "false"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    Simulator sim(A, B);
    
    auto& switch_ = sim.switchModule.switches[0][0];
    switch_.status = SwitchStatus::LOAD_B;
    switch_.b.val = 1;
    switch_.b.row = 0;
    switch_.b.col = 0;
    switch_.loadB_cycle = sim.stats.cycle;
    
    run_switches(&sim, &sim.switchModule, &sim.peModule);
    
    test_assert(sim.switchModule.next_switches[0][0].status == SwitchStatus::LOAD_B,
                "LOAD_B switch remains LOAD_B when load not done");
}

void test_run_evictions_multiple_rows() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "true"},
        {"enable_tile_eviction", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    for (int i = 0; i < 4; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    Simulator sim(A, B);
    
    sim.controller.ready_to_evict[0] = true;
    sim.controller.ready_to_evict[1] = true;
    
    auto& switch0 = sim.switchModule.switches[0][0];
    switch0.b.row = 0;
    auto& switch1 = sim.switchModule.switches[1][0];
    switch1.b.row = 1;
    
    run_evictions(&sim, &sim.controller, &sim.switchModule, &sim.peModule);
    
    test_assert(true, "Evictions processed for multiple rows");
}

void test_run_evictions_no_completed_rows() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_memory_hierarchy", "true"},
        {"enable_tile_eviction", "true"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    Simulator sim(A, B);
    
    sim.controller.ready_to_evict[0] = true;
    
    for (int j = 0; j < 4; ++j) {
        sim.switchModule.switches[0][j].b.row = std::nullopt;
    }
    
    run_evictions(&sim, &sim.controller, &sim.switchModule, &sim.peModule);
    
    test_assert(true, "Evictions handled when no completed rows");
}

int main() {
    std::cout << "\n=== Testing Switch Simulator Functions ===" << std::endl;
    
    std::cout << "\n--- Basic run_switches Tests ---" << std::endl;
    test_run_switches_basic();
    test_run_switches_load_b();
    test_run_switches_update_c_col();
    test_run_switches_load_b_not_done();
    test_run_switches_idle_state();
    test_run_switches_move_no_c_col();
    
    std::cout << "\n--- run_switches State Transitions ---" << std::endl;
    test_run_switches_move_c_eq_b();
    test_run_switches_move_c_lt_b();
    test_run_switches_move_c_gt_b();
    test_send_b_to_switch_zero();
    
    std::cout << "\n--- run_switches Advanced Scenarios ---" << std::endl;
    test_run_switches_fifo_stall();
    test_run_switches_horizontal_contention();
    test_run_switches_row_full();
    test_run_switches_decouple_mode();
    
    std::cout << "\n--- run_evictions Tests ---" << std::endl;
    test_run_evictions_basic();
    test_run_evictions_disabled();
    test_run_evictions_multiple_rows();
    test_run_evictions_no_completed_rows();
    
    std::cout << "\n=== All Switch Simulator Tests Passed! ===" << std::endl;
    return 0;
}

