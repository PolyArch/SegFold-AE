#include "csegfold/modules/module.hpp"
#include <iomanip>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace csegfold {

// Global instances
Config config_;
Stats stats_;

// Config implementation
void Config::reset() {
    *this = Config();
}

std::unordered_map<std::string, std::string> Config::to_dict() const {
    // NOTE: This function must stay in sync with:
    //   1. Config struct definition in module.hpp
    //   2. update_cfg() function below
    // When adding new config fields, update all three locations.
    std::unordered_map<std::string, std::string> result;

    // Core settings
    result["is_dense"] = is_dense ? "true" : "false";
    result["physical_pe_row_num"] = std::to_string(physical_pe_row_num);
    result["physical_pe_col_num"] = std::to_string(physical_pe_col_num);
    result["virtual_pe_row_num"] = std::to_string(virtual_pe_row_num);
    result["virtual_pe_col_num"] = std::to_string(virtual_pe_col_num);
    result["enable_multi_b_row_loading"] = enable_multi_b_row_loading ? "true" : "false";
    result["enable_b_row_reordering"] = enable_b_row_reordering ? "true" : "false";
    result["b_row_scheduling"] = b_row_scheduling;
    result["enable_dynamic_routing"] = enable_dynamic_routing ? "true" : "false";
    result["enable_partial_b_load"] = enable_partial_b_load ? "true" : "false";
    result["b_loader_window_size"] = std::to_string(b_loader_window_size);
    result["enable_b_v_contention"] = enable_b_v_contention ? "true" : "false";
    result["enable_dynamic_scheduling"] = enable_dynamic_scheduling ? "true" : "false";
    result["enable_tile_eviction"] = enable_tile_eviction ? "true" : "false";
    result["enable_tile_pipeline"] = enable_tile_pipeline ? "true" : "false";
    result["fast_eviction"] = fast_eviction ? "true" : "false";
    result["c_col_update_per_row"] = std::to_string(c_col_update_per_row);
    result["II"] = std::to_string(II);
    result["b_loader_row_limit"] = std::to_string(b_loader_row_limit);
    result["enable_offset"] = enable_offset ? "true" : "false";
    result["disable_multi_b_row_per_row"] = disable_multi_b_row_per_row ? "true" : "false";
    result["enable_sw_pe_fifo"] = enable_sw_pe_fifo ? "true" : "false";
    result["sw_pe_fifo_size"] = std::to_string(sw_pe_fifo_size);
    result["enable_fifo_bypass"] = enable_fifo_bypass ? "true" : "false";
    result["decouple_sw_and_pe"] = decouple_sw_and_pe ? "true" : "false";
    result["decouple_sw_and_controller"] = decouple_sw_and_controller ? "true" : "false";
    result["num_cycles_load_a"] = std::to_string(num_cycles_load_a);
    result["num_cycles_load_b"] = std::to_string(num_cycles_load_b);
    result["num_cycles_store_c"] = std::to_string(num_cycles_store_c);
    result["num_cycles_mult_ii"] = std::to_string(num_cycles_mult_ii);
    result["num_cycles_memory_check"] = std::to_string(num_cycles_memory_check);
    result["bypass_a_memory_hierarchy"] = bypass_a_memory_hierarchy ? "true" : "false";
    result["enable_b_loader_fifo"] = enable_b_loader_fifo ? "true" : "false";
    result["b_loader_fifo_size"] = std::to_string(b_loader_fifo_size);
    result["enable_spad"] = enable_spad ? "true" : "false";
    result["spad_load_ports_per_bank"] = std::to_string(spad_load_ports_per_bank);
    result["spad_store_ports_per_bank"] = std::to_string(spad_store_ports_per_bank);
    result["verbose"] = verbose ? "true" : "false";
    result["very_verbose"] = very_verbose ? "true" : "false";
    result["show_progress"] = show_progress ? "true" : "false";
    result["save_trace"] = save_trace ? "true" : "false";
    result["save_stats_trace"] = save_stats_trace ? "true" : "false";
    result["run_check"] = run_check ? "true" : "false";
    result["max_cycle"] = std::to_string(max_cycle);
    result["debug_log_frequency"] = std::to_string(debug_log_frequency);
    result["use_lookup_table"] = use_lookup_table ? "true" : "false";
    result["reverse_lookup_table"] = reverse_lookup_table ? "true" : "false";
    result["max_updates_per_cycle"] = std::to_string(max_updates_per_cycle);
    result["update_on_move"] = update_on_move ? "true" : "false";
    result["update_with_round_robin"] = update_with_round_robin ? "true" : "false";
    result["enable_decompose_a_row"] = enable_decompose_a_row ? "true" : "false";
    result["num_split"] = std::to_string(num_split);
    result["enable_dynamic_tiling"] = enable_dynamic_tiling ? "true" : "false";
    result["tile_c_multiplier"] = std::to_string(tile_c_multiplier);
    result["enable_a_csc"] = enable_a_csc ? "true" : "false";
    result["enable_memory_hierarchy"] = enable_memory_hierarchy ? "true" : "false";
    result["use_external_memory"] = use_external_memory ? "true" : "false";
    result["memory_server_port"] = std::to_string(memory_server_port);
    result["memory_server_host"] = memory_server_host;
    result["dummy_server_path"] = dummy_server_path;
    result["element_size"] = std::to_string(element_size);
    result["a_pointer_offset"] = std::to_string(a_pointer_offset);
    result["b_pointer_offset"] = std::to_string(b_pointer_offset);
    result["c_pointer_offset"] = std::to_string(c_pointer_offset);
    result["enable_filter"] = enable_filter ? "true" : "false";
    result["enable_filter_intersection"] = enable_filter_intersection ? "true" : "false";
    result["enable_outstanding_filter"] = enable_outstanding_filter ? "true" : "false";
    result["cache_line_size"] = std::to_string(cache_line_size);
    result["memory_backend_type"] = memory_backend_type;
    result["dram_config_file"] = dram_config_file;
    result["l1_size_kb"] = std::to_string(l1_size_kb);
    result["l1_associativity"] = std::to_string(l1_associativity);
    result["l1_line_size"] = std::to_string(l1_line_size);
    result["l1_latency"] = std::to_string(l1_latency);
    result["l2_size_kb"] = std::to_string(l2_size_kb);
    result["l2_associativity"] = std::to_string(l2_associativity);
    result["l2_line_size"] = std::to_string(l2_line_size);
    result["l2_latency"] = std::to_string(l2_latency);
    result["ideal_dram_latency"] = std::to_string(ideal_dram_latency);
    result["enable_spatial_folding"] = enable_spatial_folding ? "true" : "false";
    result["max_push"] = std::to_string(max_push);
    result["mapper_request_limit_per_cycle"] = std::to_string(mapper_request_limit_per_cycle);
    result["M"] = std::to_string(M);
    result["N"] = std::to_string(N);
    result["K"] = std::to_string(K);
    result["densityA"] = std::to_string(densityA);
    result["densityB"] = std::to_string(densityB);
    result["random_state"] = std::to_string(random_state);
    result["preprocess_only"] = preprocess_only ? "true" : "false";
    result["ablat_dynmap"] = ablat_dynmap ? "true" : "false";
    return result;
}

std::string Config::serialize() const {
    std::ostringstream oss;
    auto dict = to_dict();
    oss << "{\n";
    bool first = true;
    for (const auto& [key, value] : dict) {
        if (!first) oss << ",\n";
        first = false;
        oss << "  \"" << key << "\": ";
        if (value == "true" || value == "false") {
            oss << value;
        } else {
            bool is_number = false;
            if (!value.empty()) {
                size_t start = (value[0] == '-') ? 1 : 0;
                size_t dot_count = 0;
                bool all_digits = true;
                for (size_t i = start; i < value.length(); ++i) {
                    if (value[i] == '.') {
                        dot_count++;
                    } else if (value[i] < '0' || value[i] > '9') {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits && dot_count <= 1) {
                    if (dot_count == 0) {
                        is_number = true;
                    } else {
                        size_t dot_pos = value.find('.');
                        if (dot_pos > 0 && dot_pos < value.length() - 1) {
                            bool has_digit_left = false, has_digit_right = false;
                            for (size_t i = start; i < dot_pos && !has_digit_left; ++i) {
                                if (value[i] >= '0' && value[i] <= '9') has_digit_left = true;
                            }
                            for (size_t i = dot_pos + 1; i < value.length() && !has_digit_right; ++i) {
                                if (value[i] >= '0' && value[i] <= '9') has_digit_right = true;
                            }
                            if (has_digit_left && has_digit_right) is_number = true;
                        }
                    }
                }
            }
            
            if (is_number) {
                oss << value;
            } else {
                oss << "\"";
                for (char c : value) {
                    if (c == '"') oss << "\\\"";
                    else if (c == '\\') oss << "\\\\";
                    else if (c == '\n') oss << "\\n";
                    else if (c == '\r') oss << "\\r";
                    else if (c == '\t') oss << "\\t";
                    else oss << c;
                }
                oss << "\"";
            }
        }
    }
    oss << "\n}";
    return oss.str();
}

// Stats implementation
void Stats::reset() {
    *this = Stats();
}

std::unordered_map<std::string, std::string> Stats::to_dict() const {
    std::unordered_map<std::string, std::string> result;
    result["a_reads"] = std::to_string(a_reads);
    result["b_row_reads"] = std::to_string(b_row_reads);
    result["b_reads"] = std::to_string(b_reads);
    result["macs"] = std::to_string(macs);
    result["c_updates"] = std::to_string(c_updates);
    result["b_direct_loads"] = std::to_string(b_direct_loads);
    result["b_fifo_enqueues"] = std::to_string(b_fifo_enqueues);
    result["b_fifo_memory_ready"] = std::to_string(b_fifo_memory_ready);
    result["b_fifo_memory_wait"] = std::to_string(b_fifo_memory_wait);
    result["fifo_bypass_count"] = std::to_string(fifo_bypass_count);
    result["lut_req"] = std::to_string(lut_req);
    result["lut_upd"] = std::to_string(lut_upd);
    result["max_lut_req"] = std::to_string(max_lut_req);
    result["max_lut_upd"] = std::to_string(max_lut_upd);
    result["round_robin_upd"] = std::to_string(round_robin_upd);
    result["lut_hits"] = std::to_string(lut_hits);
    result["lut_miss"] = std::to_string(lut_miss);
    result["cycle"] = std::to_string(cycle);
    result["avg_util"] = std::to_string(avg_util);
    result["max_util"] = std::to_string(max_util);
    result["avg_b_rows"] = std::to_string(avg_b_rows);
    result["avg_b_diff"] = std::to_string(avg_b_diff);
    result["avg_b_elements_on_switch"] = std::to_string(avg_b_elements_on_switch);
    result["avg_pes_waiting_spad"] = std::to_string(avg_pes_waiting_spad);
    result["avg_pes_fifo_empty_stall"] = std::to_string(avg_pes_fifo_empty_stall);
    result["avg_pes_fifo_blocked_stall"] = std::to_string(avg_pes_fifo_blocked_stall);
    // Switch under-utilization stats (sum counters)
    result["sw_move_stall_by_fifo"] = std::to_string(sw_move_stall_by_fifo);
    result["sw_move_stall_by_network"] = std::to_string(sw_move_stall_by_network);
    result["sw_idle_not_in_range"] = std::to_string(sw_idle_not_in_range);
    result["sw_idle_no_b_element"] = std::to_string(sw_idle_no_b_element);
    result["sw_idle_b_row_conflict"] = std::to_string(sw_idle_b_row_conflict);
    // Sub-breakdown of sw_idle_no_b_element
    result["sw_idle_no_b_row"] = std::to_string(sw_idle_no_b_row);
    result["sw_idle_b_row_tail"] = std::to_string(sw_idle_b_row_tail);
    // B row load length analysis
    result["b_row_load_count"] = std::to_string(b_row_load_count);
    result["b_row_total_potential"] = std::to_string(b_row_total_potential);
    result["b_row_total_actual"] = std::to_string(b_row_total_actual);
    result["b_row_tail_events"] = std::to_string(b_row_tail_events);
    // B row length histogram as comma-separated "len:count" pairs
    {
        std::string hist_str;
        for (const auto& [len, cnt] : b_row_length_hist) {
            if (!hist_str.empty()) hist_str += ",";
            hist_str += std::to_string(len) + ":" + std::to_string(cnt);
        }
        result["b_row_length_hist"] = hist_str;
    }
    // B row demand histogram (broadcast factor per B row)
    {
        std::string hist_str;
        for (const auto& [demand, cnt] : b_row_demand_hist) {
            if (!hist_str.empty()) hist_str += ",";
            hist_str += std::to_string(demand) + ":" + std::to_string(cnt);
        }
        result["b_row_demand_hist"] = hist_str;
    }
    // Per-cycle B loads histogram
    {
        std::string hist_str;
        for (const auto& [loads, cnt] : b_loads_per_cycle_hist) {
            if (!hist_str.empty()) hist_str += ",";
            hist_str += std::to_string(loads) + ":" + std::to_string(cnt);
        }
        result["b_loads_per_cycle_hist"] = hist_str;
    }
    // Per PE-row per cycle B loads histogram
    {
        std::string hist_str;
        for (const auto& [loads, cnt] : b_loads_per_pe_row_hist) {
            if (!hist_str.empty()) hist_str += ",";
            hist_str += std::to_string(loads) + ":" + std::to_string(cnt);
        }
        result["b_loads_per_pe_row_hist"] = hist_str;
    }
    // Per PE-row idle switch breakdown
    result["sw_idle_pe_row_no_b_row"] = std::to_string(sw_idle_pe_row_no_b_row);
    result["sw_idle_pe_row_b_short"] = std::to_string(sw_idle_pe_row_b_short);
    result["sw_idle_pe_row_not_in_range"] = std::to_string(sw_idle_pe_row_not_in_range);
    result["sum_pe_rows_with_b_load"] = std::to_string(sum_pe_rows_with_b_load);
    result["pe_row_load_cycles"] = std::to_string(pe_row_load_cycles);
    // B loader bottleneck analysis
    result["sum_b_rows_loaded_per_cycle"] = std::to_string(sum_b_rows_loaded_per_cycle);
    result["sum_active_indices_per_cycle"] = std::to_string(sum_active_indices_per_cycle);
    result["b_row_skip_eviction"] = std::to_string(b_row_skip_eviction);
    result["b_row_skip_no_capacity"] = std::to_string(b_row_skip_no_capacity);
    result["b_row_skip_b_loaded"] = std::to_string(b_row_skip_b_loaded);
    // Switch under-utilization stats (averages per cycle)
    result["avg_sw_move_stall_by_fifo"] = std::to_string(avg_sw_move_stall_by_fifo);
    result["avg_sw_move_stall_by_network"] = std::to_string(avg_sw_move_stall_by_network);
    result["avg_sw_idle_not_in_range"] = std::to_string(avg_sw_idle_not_in_range);
    result["avg_sw_idle_no_b_element"] = std::to_string(avg_sw_idle_no_b_element);
    result["avg_sw_idle_b_row_conflict"] = std::to_string(avg_sw_idle_b_row_conflict);
    result["prof_cost"] = std::to_string(prof_cost);
    result["success"] = success ? "true" : "false";
    result["folding"] = std::to_string(folding);
    result["ideal_a"] = std::to_string(ideal_a);
    result["ideal_b"] = std::to_string(ideal_b);
    result["ideal_c"] = std::to_string(ideal_c);
    result["tile_a"] = std::to_string(tile_a);
    result["tile_b"] = std::to_string(tile_b);
    result["tile_c"] = std::to_string(tile_c);
    result["a_cache_loads"] = std::to_string(a_cache_loads);
    result["b_cache_loads"] = std::to_string(b_cache_loads);
    result["c_cache_loads"] = std::to_string(c_cache_loads);
    result["a_mem_loads"] = std::to_string(a_mem_loads);
    result["b_mem_loads"] = std::to_string(b_mem_loads);
    result["c_mem_loads"] = std::to_string(c_mem_loads);
    result["l1_hits"] = std::to_string(l1_hits);
    result["l1_misses"] = std::to_string(l1_misses);
    result["l2_hits"] = std::to_string(l2_hits);
    result["l2_misses"] = std::to_string(l2_misses);
    result["dram_accesses"] = std::to_string(dram_accesses);
    result["filter_coalesced"] = std::to_string(filter_coalesced);
    result["avg_memory_latency"] = std::to_string(avg_memory_latency);
    result["spad_load_hits"] = std::to_string(spad_load_hits);
    result["spad_load_misses"] = std::to_string(spad_load_misses);
    result["spad_stores"] = std::to_string(spad_stores);
    result["a_nnz"] = std::to_string(a_nnz);
    result["b_nnz"] = std::to_string(b_nnz);
    result["c_nnz"] = std::to_string(c_nnz);
    result["num_push"] = std::to_string(num_push);
    result["total_push"] = std::to_string(total_push);
    return result;
}

std::unordered_map<std::string, std::string> Stats::filter() const {
    auto dict = to_dict();
    std::unordered_map<std::string, std::string> filtered;
    for (const auto& [key, value] : dict) {
        if (key.find("trace") == std::string::npos) {
            filtered[key] = value;
        }
    }
    return filtered;
}

std::string Stats::serialize(bool include_traces) const {
    json output;
    
    auto dict = to_dict();
    for (const auto& [key, value] : dict) {
        if (value == "true") {
            output[key] = true;
        } else if (value == "false") {
            output[key] = false;
        } else if (value.find_first_not_of("0123456789.-") == std::string::npos && !value.empty()) {
            try {
                if (value.find('.') != std::string::npos) {
                    output[key] = std::stod(value);
                } else {
                    output[key] = std::stoi(value);
                }
            } catch (...) {
                output[key] = value;
            }
        } else {
            output[key] = value;
        }
    }
    
    if (include_traces) {
        output["trace_b_rows"] = trace_b_rows;
        output["trace_b_elements_on_switch"] = trace_b_elements_on_switch;
        output["trace_pes_waiting_spad"] = trace_pes_waiting_spad;
        if (!trace_pes_fifo_empty_stall.empty()) {
            output["trace_pes_fifo_empty_stall"] = trace_pes_fifo_empty_stall;
        }
        if (!trace_pes_fifo_blocked_stall.empty()) {
            output["trace_pes_fifo_blocked_stall"] = trace_pes_fifo_blocked_stall;
        }
    }
    
    return output.dump(4);
}

// Logger implementation
Logger::Logger(const std::string& name, bool verbose, bool very_verbose)
    : name_(name) {
    if (!verbose) {
        level_ = LogLevel::CRITICAL;
    } else if (very_verbose) {
        level_ = LogLevel::DEBUG;
    } else {
        level_ = LogLevel::INFO;
    }
}

void Logger::debug(const std::string& msg) {
    if (level_ <= LogLevel::DEBUG) {
        log(LogLevel::DEBUG, msg);
    }
}

void Logger::info(const std::string& msg) {
    if (level_ <= LogLevel::INFO) {
        log(LogLevel::INFO, msg);
    }
}

void Logger::warning(const std::string& msg) {
    if (level_ <= LogLevel::WARNING) {
        log(LogLevel::WARNING, msg);
    }
}

void Logger::error(const std::string& msg) {
    if (level_ <= LogLevel::ERROR) {
        log(LogLevel::ERROR, msg);
    }
}

void Logger::log(LogLevel level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    std::string time_str = oss.str();
    
    std::string level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
        case LogLevel::CRITICAL: level_str = "CRITICAL"; break;
    }
    
    std::cerr << time_str << " | " 
              << std::setw(20) << std::left << name_ << " | "
              << std::setw(8) << std::left << level_str << " | "
              << msg << std::endl;
}


// BaseModule implementation
BaseModule::BaseModule() 
    : cfg(config_), stats(stats_) {
    setup_logger();
}

void BaseModule::setup_logger() {
    // Create logger with class name
    // Note: typeid().name() may return mangled name, but it's sufficient for logging
    std::string class_name = typeid(*this).name();
    log = std::make_unique<Logger>(class_name, cfg.verbose, cfg.very_verbose);
}

int BaseModule::vrows() const {
    return cfg.virtual_pe_row_num;
}

int BaseModule::vcols() const {
    return cfg.virtual_pe_col_num;
}

int BaseModule::prows() const {
    return cfg.physical_pe_row_num;
}

int BaseModule::pcols() const {
    return cfg.physical_pe_col_num;
}

int BaseModule::num_pes() const {
    return pcols() * prows();
}

int BaseModule::cycle() const {
    return stats.cycle;
}

bool BaseModule::verbose() const {
    return cfg.verbose;
}

bool BaseModule::debug() const {
    return cfg.very_verbose;
}

// Utility functions
// NOTE: This function must stay in sync with:
//   1. Config struct definition in module.hpp
//   2. Config::to_dict() function above
// When adding new config fields, update all three locations.
void update_cfg(const std::unordered_map<std::string, std::string>& kwargs) {
    for (const auto& [key, value] : kwargs) {
        // Core settings
        if (key == "is_dense") config_.is_dense = (value == "true");
        else if (key == "physical_pe_row_num") config_.physical_pe_row_num = std::stoi(value);
        else if (key == "physical_pe_col_num") config_.physical_pe_col_num = std::stoi(value);
        else if (key == "virtual_pe_row_num") config_.virtual_pe_row_num = std::stoi(value);
        else if (key == "virtual_pe_col_num") config_.virtual_pe_col_num = std::stoi(value);
        else if (key == "enable_multi_b_row_loading") config_.enable_multi_b_row_loading = (value == "true");
        else if (key == "enable_b_row_reordering") config_.enable_b_row_reordering = (value == "true");
        else if (key == "b_row_scheduling") config_.b_row_scheduling = value;
        else if (key == "enable_dynamic_routing") config_.enable_dynamic_routing = (value == "true");
        else if (key == "enable_partial_b_load") config_.enable_partial_b_load = (value == "true");
        else if (key == "b_loader_window_size") config_.b_loader_window_size = std::stoi(value);
        else if (key == "enable_b_v_contention") config_.enable_b_v_contention = (value == "true");
        else if (key == "enable_dynamic_scheduling") config_.enable_dynamic_scheduling = (value == "true");
        else if (key == "enable_tile_eviction") config_.enable_tile_eviction = (value == "true");
        else if (key == "enable_tile_pipeline") config_.enable_tile_pipeline = (value == "true");
        else if (key == "fast_eviction") config_.fast_eviction = (value == "true");
        else if (key == "c_col_update_per_row") config_.c_col_update_per_row = std::stoi(value);
        else if (key == "II") config_.II = std::stoi(value);
        else if (key == "b_loader_row_limit") config_.b_loader_row_limit = std::stoi(value);
        else if (key == "enable_offset") config_.enable_offset = (value == "true");
        else if (key == "disable_multi_b_row_per_row") config_.disable_multi_b_row_per_row = (value == "true");
        else if (key == "enable_sw_pe_fifo") config_.enable_sw_pe_fifo = (value == "true");
        else if (key == "sw_pe_fifo_size") config_.sw_pe_fifo_size = std::stoi(value);
        else if (key == "enable_fifo_bypass") config_.enable_fifo_bypass = (value == "true");
        else if (key == "decouple_sw_and_pe") config_.decouple_sw_and_pe = (value == "true");
        else if (key == "decouple_sw_and_controller") config_.decouple_sw_and_controller = (value == "true");
        else if (key == "num_cycles_load_a") config_.num_cycles_load_a = std::stoi(value);
        else if (key == "num_cycles_load_b") config_.num_cycles_load_b = std::stoi(value);
        else if (key == "num_cycles_store_c") config_.num_cycles_store_c = std::stoi(value);
        else if (key == "num_cycles_mult_ii") config_.num_cycles_mult_ii = std::stoi(value);
        else if (key == "num_cycles_memory_check") config_.num_cycles_memory_check = std::stoi(value);
        else if (key == "bypass_a_memory_hierarchy") config_.bypass_a_memory_hierarchy = (value == "true");
        else if (key == "enable_b_loader_fifo") config_.enable_b_loader_fifo = (value == "true");
        else if (key == "b_loader_fifo_size") config_.b_loader_fifo_size = std::stoi(value);
        else if (key == "enable_spad") config_.enable_spad = (value == "true");
        else if (key == "spad_load_ports_per_bank") config_.spad_load_ports_per_bank = std::stoi(value);
        else if (key == "spad_store_ports_per_bank") config_.spad_store_ports_per_bank = std::stoi(value);
        else if (key == "verbose") config_.verbose = (value == "true");
        else if (key == "very_verbose") config_.very_verbose = (value == "true");
        else if (key == "show_progress") config_.show_progress = (value == "true");
        else if (key == "save_trace") config_.save_trace = (value == "true");
        else if (key == "save_stats_trace") config_.save_stats_trace = (value == "true");
        else if (key == "run_check") config_.run_check = (value == "true");
        else if (key == "max_cycle") config_.max_cycle = std::stoi(value);
        else if (key == "debug_log_frequency") config_.debug_log_frequency = std::stoi(value);
        else if (key == "use_lookup_table") config_.use_lookup_table = (value == "true");
        else if (key == "reverse_lookup_table") config_.reverse_lookup_table = (value == "true");
        else if (key == "max_updates_per_cycle") config_.max_updates_per_cycle = std::stoi(value);
        else if (key == "update_on_move") config_.update_on_move = (value == "true");
        else if (key == "update_with_round_robin") config_.update_with_round_robin = (value == "true");
        else if (key == "enable_decompose_a_row") config_.enable_decompose_a_row = (value == "true");
        else if (key == "num_split") config_.num_split = std::stoi(value);
        else if (key == "enable_dynamic_tiling") config_.enable_dynamic_tiling = (value == "true");
        else if (key == "tile_c_multiplier") config_.tile_c_multiplier = std::stod(value);
        else if (key == "enable_a_csc") config_.enable_a_csc = (value == "true");
        else if (key == "enable_memory_hierarchy") config_.enable_memory_hierarchy = (value == "true");
        else if (key == "use_external_memory") config_.use_external_memory = (value == "true");
        else if (key == "memory_server_port") config_.memory_server_port = std::stoi(value);
        else if (key == "memory_server_host") config_.memory_server_host = value;
        else if (key == "dummy_server_path") config_.dummy_server_path = value;
        else if (key == "element_size") config_.element_size = std::stoi(value);
        else if (key == "a_pointer_offset") config_.a_pointer_offset = std::stoi(value);
        else if (key == "b_pointer_offset") config_.b_pointer_offset = std::stoi(value);
        else if (key == "c_pointer_offset") config_.c_pointer_offset = std::stoi(value);
        else if (key == "enable_filter") config_.enable_filter = (value == "true");
        else if (key == "enable_filter_intersection") config_.enable_filter_intersection = (value == "true");
        else if (key == "enable_outstanding_filter") config_.enable_outstanding_filter = (value == "true");
        else if (key == "cache_line_size") config_.cache_line_size = std::stoi(value);
        else if (key == "memory_backend_type") config_.memory_backend_type = value;
        else if (key == "dram_config_file") config_.dram_config_file = value;
        else if (key == "l1_size_kb") config_.l1_size_kb = std::stoi(value);
        else if (key == "l1_associativity") config_.l1_associativity = std::stoi(value);
        else if (key == "l1_line_size") config_.l1_line_size = std::stoi(value);
        else if (key == "l1_latency") config_.l1_latency = std::stoi(value);
        else if (key == "l2_size_kb") config_.l2_size_kb = std::stoi(value);
        else if (key == "l2_associativity") config_.l2_associativity = std::stoi(value);
        else if (key == "l2_line_size") config_.l2_line_size = std::stoi(value);
        else if (key == "l2_latency") config_.l2_latency = std::stoi(value);
        else if (key == "ideal_dram_latency") config_.ideal_dram_latency = std::stoi(value);
        else if (key == "enable_spatial_folding") config_.enable_spatial_folding = (value == "true");
        else if (key == "max_push") config_.max_push = std::stoi(value);
        else if (key == "mapper_request_limit_per_cycle") config_.mapper_request_limit_per_cycle = std::stoi(value);
        else if (key == "M") config_.M = std::stoi(value);
        else if (key == "N") config_.N = std::stoi(value);
        else if (key == "K") config_.K = std::stoi(value);
        else if (key == "densityA") config_.densityA = std::stod(value);
        else if (key == "densityB") config_.densityB = std::stod(value);
        else if (key == "random_state") config_.random_state = std::stoi(value);
        else if (key == "preprocess_only") config_.preprocess_only = (value == "true");
        else if (key == "ablat_dynmap") config_.ablat_dynmap = (value == "true");
    }
}

void load_cfg(const std::string& path) {
    try {
        YAML::Node config = YAML::LoadFile(path);
        if (!config.IsMap()) {
            std::cerr << "Warning: YAML file does not contain a map, skipping load." << std::endl;
            return;
        }
        std::unordered_map<std::string, std::string> kwargs;
        for (const auto& node : config) {
            std::string key = node.first.as<std::string>();
            YAML::Node value_node = node.second;
            if (!value_node.IsScalar()) {
                if (!value_node.IsNull()) {
                    std::cerr << "Warning: Config key '" << key << "' has non-scalar value, skipping." << std::endl;
                }
                continue;
            }
            std::string value;
            std::string str_val = value_node.as<std::string>();
            if (str_val == "True" || str_val == "true") {
                value = "true";
            } else if (str_val == "False" || str_val == "false") {
                value = "false";
            } else {
                try {
                    value = std::to_string(value_node.as<int>());
                } catch (...) {
                    try {
                        value = std::to_string(value_node.as<double>());
                    } catch (...) {
                        value = str_val;
                    }
                }
            }
            kwargs[key] = value;
        }
        update_cfg(kwargs);
    } catch (const YAML::BadFile& e) {
        std::cerr << "Error: Could not open YAML file '" << path << "': " << e.what() << std::endl;
    } catch (const YAML::ParserException& e) {
        std::cerr << "Error: Failed to parse YAML file '" << path << "': " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: Unexpected error loading config from '" << path << "': " << e.what() << std::endl;
    }
}

void reset() {
    config_ = Config();
    stats_ = Stats();
}

void update_b_window_size(double density) {
    constexpr double EPS = 1e-6;
    if (std::abs(density - 0.05) < EPS) {
        // Keep current size
    } else if (std::abs(density - 0.1) < EPS) {
        config_.b_loader_window_size *= 2;
    }
}

} // namespace csegfold

