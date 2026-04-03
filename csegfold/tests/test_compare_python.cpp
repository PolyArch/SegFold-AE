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
#include <set>
#include <unordered_map>
#include <sys/stat.h>
#include <sys/types.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace csegfold;

// Function to compare configs from two files
void compare_configs(const std::string& config_cpp_file, const std::string& config_python_file) {
    std::cout << "\n  === Config Comparison ===" << std::endl;
    
    std::ifstream file_cpp(config_cpp_file);
    std::ifstream file_python(config_python_file);
    
    if (!file_cpp.is_open()) {
        std::cerr << "  Warning: Could not open C++ config file: " << config_cpp_file << std::endl;
        return;
    }
    if (!file_python.is_open()) {
        std::cerr << "  Warning: Could not open Python config file: " << config_python_file << std::endl;
        file_cpp.close();
        return;
    }
    
    try {
        json config_cpp_json, config_python_json;
        file_cpp >> config_cpp_json;
        file_python >> config_python_json;
        file_cpp.close();
        file_python.close();
    
    std::set<std::string> all_keys;
    for (const auto& [key, value] : config_cpp_json.items()) {
        all_keys.insert(key);
    }
    for (const auto& [key, value] : config_python_json.items()) {
        all_keys.insert(key);
    }
    
    bool found_difference = false;
    for (const auto& key : all_keys) {
        bool in_cpp = config_cpp_json.contains(key);
        bool in_python = config_python_json.contains(key);
        
        if (!in_cpp) {
            std::cout << "  ⚠ Key only in Python: " << key << " = " << config_python_json[key] << std::endl;
            found_difference = true;
        } else if (!in_python) {
            std::cout << "  ⚠ Key only in C++: " << key << " = " << config_cpp_json[key] << std::endl;
            found_difference = true;
        } else {
            std::string cpp_val = config_cpp_json[key].dump();
            std::string python_val = config_python_json[key].dump();
            if (cpp_val != python_val) {
                std::cout << "  ⚠ Mismatch: " << key << " - C++: " << cpp_val << ", Python: " << python_val << std::endl;
                found_difference = true;
            }
        }
    }
    
    if (!found_difference) {
        std::cout << "  ✓ Configs match perfectly!" << std::endl;
    }
    } catch (const json::parse_error& e) {
        std::cerr << "  Error parsing JSON config files: " << e.what() << std::endl;
        file_cpp.close();
        file_python.close();
    } catch (const std::exception& e) {
        std::cerr << "  Error comparing configs: " << e.what() << std::endl;
        file_cpp.close();
        file_python.close();
    }
}

// Function to compare two trace files and find differences
void compare_traces(const std::string& trace_cpp, const std::string& trace_python) {
    std::cout << "\n  === Trace Comparison ===" << std::endl;
    
    std::ifstream file_cpp(trace_cpp);
    std::ifstream file_python(trace_python);
    
    if (!file_cpp.is_open()) {
        std::cerr << "  Warning: Could not open C++ trace file: " << trace_cpp << std::endl;
        return;
    }
    if (!file_python.is_open()) {
        std::cerr << "  Warning: Could not open Python trace file: " << trace_python << std::endl;
        file_cpp.close();
        return;
    }
    
    try {
        json trace_cpp_json, trace_python_json;
        file_cpp >> trace_cpp_json;
        file_python >> trace_python_json;
        file_cpp.close();
        file_python.close();
    
    auto trace_cpp_data = trace_cpp_json["trace"];
    auto trace_python_data = trace_python_json["trace"];
    
    int cpp_cycles = trace_cpp_data.size();
    int python_cycles = trace_python_data.size();
    
    std::cout << "  C++ trace cycles: " << cpp_cycles << std::endl;
    std::cout << "  Python trace cycles: " << python_cycles << std::endl;
    
    int min_cycles = std::min(cpp_cycles, python_cycles);
    int max_cycles = std::max(cpp_cycles, python_cycles);
    
    if (cpp_cycles != python_cycles) {
        std::cout << "  ⚠ Cycle count mismatch in traces!" << std::endl;
    }
    
    // Compare each cycle
    bool found_difference = false;
    for (int i = 0; i < min_cycles; ++i) {
        auto cpp_cycle = trace_cpp_data[i];
        auto python_cycle = trace_python_data[i];
        
        int cpp_cycle_num = cpp_cycle.value("cycle", -1);
        int python_cycle_num = python_cycle.value("cycle", -1);
        
        if (cpp_cycle_num != python_cycle_num) {
            std::cout << "  ⚠ Cycle number mismatch at index " << i 
                      << ": C++=" << cpp_cycle_num << ", Python=" << python_cycle_num << std::endl;
            found_difference = true;
        }
        
        int cpp_active_pes = cpp_cycle.value("num_active_pes", -1);
        int python_active_pes = python_cycle.value("num_active_pes", -1);
        
        if (cpp_active_pes != python_active_pes) {
            std::cout << "  ⚠ Active PEs mismatch at cycle " << cpp_cycle_num 
                      << ": C++=" << cpp_active_pes << ", Python=" << python_active_pes << std::endl;
            found_difference = true;
        }
        
        double cpp_util = cpp_cycle.value("utilization", -1.0);
        double python_util = python_cycle.value("utilization", -1.0);
        
        if (std::abs(cpp_util - python_util) > 0.001) {
            std::cout << "  ⚠ Utilization mismatch at cycle " << cpp_cycle_num 
                      << ": C++=" << cpp_util << ", Python=" << python_util << std::endl;
            found_difference = true;
        }
        
        // Compare b_positions (simplified - just count)
        int cpp_b_pos_count = 0;
        int python_b_pos_count = 0;
        if (cpp_cycle.contains("b_positions")) {
            cpp_b_pos_count = cpp_cycle["b_positions"].size();
        }
        if (python_cycle.contains("b_positions")) {
            python_b_pos_count = python_cycle["b_positions"].size();
        }
        
        if (cpp_b_pos_count != python_b_pos_count) {
            std::cout << "  ⚠ B positions count mismatch at cycle " << cpp_cycle_num 
                      << ": C++=" << cpp_b_pos_count << ", Python=" << python_b_pos_count << std::endl;
            found_difference = true;
        }
    }
    
    if (max_cycles > min_cycles) {
        std::cout << "  ⚠ Trace length mismatch: " << max_cycles - min_cycles << " extra cycle(s) in ";
        if (cpp_cycles > python_cycles) {
            std::cout << "C++ trace" << std::endl;
        } else {
            std::cout << "Python trace" << std::endl;
        }
        found_difference = true;
    }
    
    if (!found_difference && cpp_cycles == python_cycles) {
        std::cout << "  ✓ Traces match perfectly!" << std::endl;
    } else {
        std::cout << "  ⚠ Differences found in traces (check above)" << std::endl;
    }
    } catch (const json::parse_error& e) {
        std::cerr << "  Error parsing JSON trace files: " << e.what() << std::endl;
        file_cpp.close();
        file_python.close();
    } catch (const std::exception& e) {
        std::cerr << "  Error comparing traces: " << e.what() << std::endl;
        file_cpp.close();
        file_python.close();
    }
}

void test_compare_cpp_python_cycles(const std::string& test_name, 
                                     const std::unordered_map<std::string, std::string>& config,
                                     int matrix_size, bool is_dense) {
    std::cout << "\n--- " << test_name << " (C++ vs Python Cycle Comparison) ---" << std::endl;
    
    // Run C++ simulator
    reset();
    // Add save_trace and very_verbose to config for debugging
    std::unordered_map<std::string, std::string> config_with_trace = config;
    config_with_trace["save_trace"] = "true";
    config_with_trace["very_verbose"] = "true";
    update_cfg(config_with_trace);
    
    Matrix<int8_t> A, B;
    if (is_dense) {
        A = Matrix<int8_t>(matrix_size, matrix_size, 1);
        B = Matrix<int8_t>(matrix_size, matrix_size, 1);
    } else {
        // Sparse pattern: diagonal + band
        A = Matrix<int8_t>(matrix_size, matrix_size, 0);
        B = Matrix<int8_t>(matrix_size, matrix_size, 0);
        for (int i = 0; i < matrix_size; ++i) {
            A(i, i) = 1;
            B(i, i) = 1;
            if (i < matrix_size - 1) {
                A(i, i+1) = 1;
                B(i+1, i) = 1;
            }
        }
    }
    
    SegfoldSimulator sim_cpp(A, B);
    
    // Store config after MatrixLoader initialization for later comparison
    auto cpp_config_dict = sim_cpp.cfg.to_dict();
    
    sim_cpp.run();
    
    int cpp_cycles = sim_cpp.stats.cycle;
    bool cpp_success = sim_cpp.success;
    
    std::cout << "  C++ Simulation:" << std::endl;
    std::cout << "    Cycles: " << cpp_cycles << std::endl;
    std::cout << "    Success: " << (cpp_success ? "true" : "false") << std::endl;
    
    // Determine workspace root (SegFold directory)
    // Try to find it by checking for csegfold directory using stat
    std::string workspace_root = ".";
    struct stat info;
    // Check if we're in the workspace root (has csegfold subdirectory)
    if (stat("csegfold", &info) != 0 || !(info.st_mode & S_IFDIR)) {
        // Try going up one level (from build directory)
        if (stat("../csegfold", &info) == 0 && (info.st_mode & S_IFDIR)) {
            workspace_root = "..";
        } else if (stat("../../csegfold", &info) == 0 && (info.st_mode & S_IFDIR)) {
            // Try going up two levels
            workspace_root = "../..";
        }
    }
    
    // Create tmp directory if it doesn't exist
    std::string tmp_dir = workspace_root + "/tmp";
    if (stat(tmp_dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
        mkdir(tmp_dir.c_str(), 0755);
    }
    
    // Save trace, config, and stats with unique filenames
    std::string trace_filename_cpp = tmp_dir + "/segfold_trace_cpp_" + std::to_string(matrix_size) + 
                                     (is_dense ? "_dense" : "_sparse") + ".json";
    std::string config_filename_cpp = tmp_dir + "/segfold_config_cpp_" + std::to_string(matrix_size) + 
                                     (is_dense ? "_dense" : "_sparse") + ".json";
    std::string stats_filename_cpp = tmp_dir + "/segfold_stats_cpp_" + std::to_string(matrix_size) + 
                                     (is_dense ? "_dense" : "_sparse") + ".json";
    
    sim_cpp.dump_trace(trace_filename_cpp);
    sim_cpp.dump_config(config_filename_cpp);
    sim_cpp.dump_stats(stats_filename_cpp, false);
    
    // Verify files were actually created
    std::cout << "    Trace saved to: " << trace_filename_cpp;
    if (std::ifstream(trace_filename_cpp).good()) {
        std::cout << " ✓" << std::endl;
    } else {
        std::cout << " ✗ (file not found!)" << std::endl;
    }
    
    std::cout << "    Config saved to: " << config_filename_cpp;
    if (std::ifstream(config_filename_cpp).good()) {
        std::cout << " ✓" << std::endl;
    } else {
        std::cout << " ✗ (file not found!)" << std::endl;
        std::cerr << "      Warning: Config file was not created. Check for errors above." << std::endl;
    }
    
    std::cout << "    Stats saved to: " << stats_filename_cpp;
    if (std::ifstream(stats_filename_cpp).good()) {
        std::cout << " ✓" << std::endl;
    } else {
        std::cout << " ✗ (file not found!)" << std::endl;
    }
    
    // Write configuration to temp file for Python script
    std::string config_file = tmp_dir + "/segfold_test_config.txt";
    std::ofstream config_out(config_file);
    config_out << "matrix_size=" << matrix_size << std::endl;
    config_out << "is_dense=" << (is_dense ? "1" : "0") << std::endl;
    // Add save_trace to config
    config_out << "save_trace=true" << std::endl;
    // Add trace filename (use same tmp_dir as above)
    std::string trace_filename_python = tmp_dir + "/segfold_trace_python_" + std::to_string(matrix_size) + 
                                        (is_dense ? "_dense" : "_sparse") + ".json";
    config_out << "trace_filename=" << trace_filename_python << std::endl;
    config_out << "workspace_root=" << workspace_root << std::endl;
    for (const auto& [key, value] : config) {
        config_out << key << "=" << value << std::endl;
    }
    config_out.close();
    
    // Call Python script (assume it's in the same directory as this test file)
    // The script should be in csegfold/tests/ relative to workspace root
    // Try multiple possible paths
    std::vector<std::string> possible_paths = {
        "csegfold/tests/compare_simulator.py",
        "../tests/compare_simulator.py",
        "../../csegfold/tests/compare_simulator.py",
        "./compare_simulator.py",  // Same directory as test executable
        "compare_simulator.py"      // Current working directory
    };
    
    // Try to get workspace root from environment or use common paths
    const char* workspace = std::getenv("WORKSPACE");
    if (workspace) {
        std::string workspace_path = std::string(workspace) + "/csegfold/tests/compare_simulator.py";
        possible_paths.insert(possible_paths.begin(), workspace_path);
    }
    
    std::string python_script_path;
    std::ifstream test_file;
    for (const auto& path : possible_paths) {
        test_file.open(path);
        if (test_file.good()) {
            python_script_path = path;
            test_file.close();
            break;
        }
        test_file.close();
    }
    
    if (python_script_path.empty()) {
        std::cerr << "  Warning: Could not find compare_simulator.py, skipping Python comparison" << std::endl;
        std::remove(config_file.c_str());
        return;
    }
    
    // Redirect stderr to stdout to capture config output
    std::string command = "python3 " + python_script_path + " " + config_file + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "  Failed to run Python script" << std::endl;
        return;
    }
    
    int python_cycles = -1;
    bool python_success = false;
    std::vector<std::string> python_config_lines;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        // Remove newline if present
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        if (line.find("PYTHON_CYCLES=") == 0) {
            python_cycles = std::stoi(line.substr(14));
        } else if (line.find("PYTHON_SUCCESS=") == 0) {
            python_success = (line.substr(15).find("True") != std::string::npos);
        } else if (line.find("PYTHON_CONFIG_FULL:") == 0 || 
                   (line.find("  ") == 0 && !python_config_lines.empty())) {
            // Collect Python config output (starts with PYTHON_CONFIG_FULL: or continues with indentation)
            python_config_lines.push_back(line);
        }
    }
    pclose(pipe);
    
    std::cout << "  Python Simulation:" << std::endl;
    std::cout << "    Cycles: " << python_cycles << std::endl;
    std::cout << "    Success: " << (python_success ? "true" : "false") << std::endl;
    
    // Compare configs side by side
    std::cout << "\n  === Config Comparison (C++ vs Python) ===" << std::endl;
    // cpp_config_dict was already defined above after MatrixLoader init
    
    // Parse Python config from stderr output
    std::unordered_map<std::string, std::string> python_config_dict;
    bool in_config_section = false;
    for (const auto& config_line : python_config_lines) {
        if (config_line.find("PYTHON_CONFIG_FULL:") == 0) {
            in_config_section = true;
            continue; // Skip header line
        }
        if (in_config_section && config_line.find("  ") == 0) {
            // Parse "  key: value" format
            size_t colon_pos = config_line.find(':');
            if (colon_pos != std::string::npos && colon_pos > 2) {
                std::string key = config_line.substr(2, colon_pos - 2); // Skip "  "
                std::string value = config_line.substr(colon_pos + 1);
                // Trim whitespace from value
                while (!value.empty() && value[0] == ' ') {
                    value = value.substr(1);
                }
                // Remove trailing whitespace
                while (!value.empty() && value.back() == ' ') {
                    value.pop_back();
                }
                if (!key.empty()) {
                    python_config_dict[key] = value;
                }
            }
        } else if (in_config_section && config_line.find("  ") != 0) {
            // End of config section
            break;
        }
    }
    
    // Only compare if we successfully parsed Python config
    if (python_config_dict.empty() && !python_config_lines.empty()) {
        std::cout << "  Warning: Could not parse Python config from stderr output" << std::endl;
    } else if (!python_config_dict.empty()) {
        // Compare all keys
        std::set<std::string> all_keys;
        for (const auto& [key, value] : cpp_config_dict) {
            all_keys.insert(key);
        }
        for (const auto& [key, value] : python_config_dict) {
            all_keys.insert(key);
        }
        
        bool config_diff_found = false;
        for (const auto& key : all_keys) {
            bool in_cpp = cpp_config_dict.find(key) != cpp_config_dict.end();
            bool in_python = python_config_dict.find(key) != python_config_dict.end();
            
            if (!in_cpp) {
                std::cout << "  ⚠ Key only in Python: " << key << " = " << python_config_dict[key] << std::endl;
                config_diff_found = true;
            } else if (!in_python) {
                std::cout << "  ⚠ Key only in C++: " << key << " = " << cpp_config_dict[key] << std::endl;
                config_diff_found = true;
            } else {
                std::string cpp_val = cpp_config_dict[key];
                std::string python_val = python_config_dict[key];
                // Normalize boolean values for comparison
                if (cpp_val == "true" || cpp_val == "false") {
                    if (python_val == "True" || python_val == "False") {
                        // Convert Python bools to lowercase
                        if (python_val == "True") python_val = "true";
                        if (python_val == "False") python_val = "false";
                    }
                }
                if (cpp_val != python_val) {
                    std::cout << "  ⚠ Mismatch: " << key << " - C++: " << cpp_val << ", Python: " << python_val << std::endl;
                    config_diff_found = true;
                }
            }
        }
        
        if (!config_diff_found) {
            std::cout << "  ✓ Configs match perfectly!" << std::endl;
        }
    }
    
    // Print trace file locations for comparison
    std::cout << "  Trace files saved for comparison:" << std::endl;
    std::cout << "    C++ trace: " << trace_filename_cpp << std::endl;
    std::cout << "    Python trace: " << trace_filename_python << std::endl;
    
    // Compare configs (only if both files exist)
    std::string config_filename_python = tmp_dir + "/segfold_config_python_" + std::to_string(matrix_size) + 
                                        (is_dense ? "_dense" : "_sparse") + ".json";
    std::ifstream test_cpp_config(config_filename_cpp);
    std::ifstream test_python_config(config_filename_python);
    bool cpp_config_exists = test_cpp_config.good();
    bool python_config_exists = test_python_config.good();
    test_cpp_config.close();
    test_python_config.close();
    
    if (cpp_config_exists && python_config_exists) {
        compare_configs(config_filename_cpp, config_filename_python);
    } else {
        if (!cpp_config_exists) {
            std::cout << "  Note: C++ config file not found: " << config_filename_cpp << std::endl;
        }
        if (!python_config_exists) {
            std::cout << "  Note: Python config file not found: " << config_filename_python << std::endl;
        }
    }
    
    // Compare traces if cycle counts differ and both trace files exist
    if (cpp_cycles != python_cycles) {
        std::ifstream test_cpp_trace(trace_filename_cpp);
        std::ifstream test_python_trace(trace_filename_python);
        bool cpp_trace_exists = test_cpp_trace.good();
        bool python_trace_exists = test_python_trace.good();
        test_cpp_trace.close();
        test_python_trace.close();
        
        if (cpp_trace_exists && python_trace_exists) {
            compare_traces(trace_filename_cpp, trace_filename_python);
        } else {
            std::cout << "  Note: Cannot compare traces - files missing:" << std::endl;
            if (!cpp_trace_exists) {
                std::cout << "    C++ trace: " << trace_filename_cpp << std::endl;
            }
            if (!python_trace_exists) {
                std::cout << "    Python trace: " << trace_filename_python << std::endl;
            }
        }
    }
    
    // Compare results
    if (cpp_cycles == python_cycles) {
        std::cout << "  ✓ PASS: Cycle counts match (" << cpp_cycles << " cycles)" << std::endl;
    } else {
        std::cerr << "  ✗ FAIL: Cycle counts differ - C++: " << cpp_cycles 
                  << ", Python: " << python_cycles << std::endl;
        exit(1);
    }
    
    if (cpp_success == python_success) {
        std::cout << "  ✓ PASS: Success status matches (" << (cpp_success ? "success" : "failed") << ")" << std::endl;
    } else {
        std::cerr << "  ✗ FAIL: Success status differs - C++: " << (cpp_success ? "success" : "failed")
                  << ", Python: " << (python_success ? "success" : "failed") << std::endl;
        exit(1);
    }
    
    // Clean up
    std::remove(config_file.c_str());
}

int main() {
    std::cout << "\n=== C++ vs Python Cycle Count Comparison ===" << std::endl;
    
    // Test 1: 4x4 dense matrix on 4x4 PE array (no tile eviction)
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "4"},
            {"b_loader_window_size", "8"},
            {"II", "1"},
            {"max_cycle", "50000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "false"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("4x4 Dense Matrix (4x4 PE, no eviction)", config, 4, true);
    }
    
    // Test 2: 4x4 sparse matrix on 4x4 PE array
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "4"},
            {"b_loader_window_size", "8"},
            {"II", "1"},
            {"max_cycle", "50000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "false"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("4x4 Sparse Matrix (4x4 PE)", config, 4, false);
    }
    
    // Test 3: 8x8 dense matrix on 4x4 PE array (with tile eviction)
    {
        std::unordered_map<std::string, std::string> config = {
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
        };
        test_compare_cpp_python_cycles("8x8 Dense Matrix (4x4 PE, with eviction)", config, 8, true);
    }
    
    // Test 4: 8x8 sparse matrix on 4x4 PE array
    {
        std::unordered_map<std::string, std::string> config = {
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
        };
        test_compare_cpp_python_cycles("8x8 Sparse Matrix (4x4 PE)", config, 8, false);
    }
    
    // Test 5: 16x16 dense matrix on 8x8 PE array
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "8"},
            {"physical_pe_col_num", "8"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "8"},
            {"b_loader_window_size", "16"},
            {"II", "1"},
            {"max_cycle", "100000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("16x16 Dense Matrix (8x8 PE)", config, 16, true);
    }
    
    // Test 6: 16x16 sparse matrix on 8x8 PE array
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "8"},
            {"physical_pe_col_num", "8"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "8"},
            {"b_loader_window_size", "16"},
            {"II", "1"},
            {"max_cycle", "100000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("16x16 Sparse Matrix (8x8 PE)", config, 16, false);
    }
    
    // Test 7: 32x32 dense matrix on 16x16 PE array
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "16"},
            {"physical_pe_col_num", "16"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "16"},
            {"b_loader_window_size", "32"},
            {"II", "1"},
            {"max_cycle", "500000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("32x32 Dense Matrix (16x16 PE)", config, 32, true);
    }
    
    // Test 8: 32x32 sparse matrix on 16x16 PE array
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "16"},
            {"physical_pe_col_num", "16"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "16"},
            {"b_loader_window_size", "32"},
            {"II", "1"},
            {"max_cycle", "500000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("32x32 Sparse Matrix (16x16 PE)", config, 32, false);
    }
    
    // Test 9: 8x8 dense matrix with spatial folding
    {
        std::unordered_map<std::string, std::string> config = {
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
            {"enable_spatial_folding", "true"}
        };
        test_compare_cpp_python_cycles("8x8 Dense Matrix (4x4 PE, spatial folding)", config, 8, true);
    }
    
    // Test 10: 16x16 dense matrix with spatial folding
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "8"},
            {"physical_pe_col_num", "8"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "8"},
            {"b_loader_window_size", "16"},
            {"II", "1"},
            {"max_cycle", "100000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "true"}
        };
        test_compare_cpp_python_cycles("16x16 Dense Matrix (8x8 PE, spatial folding)", config, 16, true);
    }
    
    // Test 11: 8x8 dense matrix with smaller window size (to test eviction behavior)
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "4"},
            {"b_loader_window_size", "4"},  // Small window to force more evictions
            {"II", "1"},
            {"max_cycle", "50000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("8x8 Dense Matrix (4x4 PE, small window)", config, 8, true);
    }
    
    // Test 12: 16x16 dense matrix on 4x4 PE array (larger matrix, smaller PE array)
    {
        std::unordered_map<std::string, std::string> config = {
            {"physical_pe_row_num", "4"},
            {"physical_pe_col_num", "4"},
            {"enable_memory_hierarchy", "false"},
            {"enable_sw_pe_fifo", "true"},
            {"sw_pe_fifo_size", "4"},
            {"b_loader_window_size", "8"},
            {"II", "1"},
            {"max_cycle", "200000"},
            {"verbose", "false"},
            {"enable_tile_eviction", "true"},
            {"enable_spatial_folding", "false"}
        };
        test_compare_cpp_python_cycles("16x16 Dense Matrix (4x4 PE)", config, 16, true);
    }
    
    std::cout << "\n=== All Tests Completed! ===" << std::endl;
    return 0;
}

