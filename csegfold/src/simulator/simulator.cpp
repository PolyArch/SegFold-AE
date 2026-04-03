#include "csegfold/simulator/simulator.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <sys/stat.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace csegfold {

int _idx_w(int length) {
    if (length <= 0) {
        throw std::runtime_error("Length must be a positive integer");
    }
    int bits = 0;
    int temp = length - 1;
    while (temp > 0) {
        temp >>= 1;
        bits++;
    }
    return 1 << (bits * 2);
}

Simulator::Simulator(const Matrix<int8_t>& A, const Matrix<int8_t>& B) 
    : BaseModule(), matrix(A, B), peModule(), switchModule(), spadModule(), controller(&matrix) {
    
    if (cfg.use_lookup_table) {
        if (cfg.reverse_lookup_table) {
            lut = std::make_unique<ReverseLookUpTable>();
        } else {
            lut = std::make_unique<LookUpTable>();
        }
        switchModule.lut = lut.get();
    }
    
    success = true;
    
    profile_AB();
    record_profiling_data();
    
    if (!cfg.enable_dynamic_scheduling) {
        record_metadata_for_profiling_C();
    }
    
    if (cfg.enable_sw_pe_fifo) {
        init_fifo();
    }
    
    acc_output = SparseAccumulator(matrix.M, matrix.N);
    snapshot.clear();
    trace.clear();
    stats.success = true;
    
    stats.a_nnz = matrix.A_indexed.nnz();
    stats.b_nnz = matrix.B_indexed.nnz();
    stats.c_nnz = 0;
}

Simulator::Simulator(const CSRMatrix& A_csr, const CSRMatrix& B_csr)
    : BaseModule(), matrix(A_csr, B_csr), peModule(), switchModule(), spadModule(), controller(&matrix) {

    if (cfg.use_lookup_table) {
        if (cfg.reverse_lookup_table) {
            lut = std::make_unique<ReverseLookUpTable>();
        } else {
            lut = std::make_unique<LookUpTable>();
        }
        switchModule.lut = lut.get();
    }

    success = true;

    profile_AB();
    record_profiling_data();

    if (!cfg.enable_dynamic_scheduling) {
        record_metadata_for_profiling_C();
    }

    if (cfg.enable_sw_pe_fifo) {
        init_fifo();
    }

    acc_output = SparseAccumulator(matrix.M, matrix.N);
    snapshot.clear();
    trace.clear();
    stats.success = true;

    stats.a_nnz = matrix.A_indexed.nnz();
    stats.b_nnz = matrix.B_indexed.nnz();
    stats.c_nnz = 0;
}

Simulator::~Simulator() = default;

void Simulator::profile_AB() {
    int a_rows = matrix.A.rows() > 0 ? matrix.A.rows() : matrix.A_orig_csr.rows_;
    int a_cols = matrix.A.cols() > 0 ? matrix.A.cols() : matrix.A_orig_csr.cols_;
    int b_rows = matrix.B.rows() > 0 ? matrix.B.rows() : matrix.B_orig_csr.rows_;
    int b_cols = matrix.B.cols() > 0 ? matrix.B.cols() : matrix.B_orig_csr.cols_;
    a_row_idx_w = _idx_w(a_rows);
    a_col_idx_w = _idx_w(a_cols);
    b_row_idx_w = _idx_w(b_rows);
    b_col_idx_w = _idx_w(b_cols);
}

void Simulator::record_profiling_data() {
    int a_rows = matrix.A.rows() > 0 ? matrix.A.rows() : matrix.A_orig_csr.rows_;
    int a_cols = matrix.A.cols() > 0 ? matrix.A.cols() : matrix.A_orig_csr.cols_;
    int b_rows = matrix.B.rows() > 0 ? matrix.B.rows() : matrix.B_orig_csr.rows_;
    int b_cols = matrix.B.cols() > 0 ? matrix.B.cols() : matrix.B_orig_csr.cols_;
    stats.metadata["A rows"] = std::to_string(a_rows);
    stats.metadata["A cols"] = std::to_string(a_cols);
    stats.metadata["B rows"] = std::to_string(b_rows);
    stats.metadata["B cols"] = std::to_string(b_cols);
    stats.metadata["a_row_idx_w"] = std::to_string(a_row_idx_w);
    stats.metadata["a_col_idx_w"] = std::to_string(a_col_idx_w);
    stats.metadata["b_row_idx_w"] = std::to_string(b_row_idx_w);
    stats.metadata["b_col_idx_w"] = std::to_string(b_col_idx_w);
}

void Simulator::record_metadata_for_profiling_C() {
    auto [totA, totB] = metadata_for_profiling_C();
    stats.metadata["A metadata for profiling C"] = std::to_string(totA);
    stats.metadata["B metadata for profiling C"] = std::to_string(totB);
    stats.prof_cost += totA + totB;
}

std::pair<int, int> Simulator::metadata_for_profiling_C() const {
    int totA = 0, totB = 0;
    // Simplified implementation
    return {totA, totB};
}

void Simulator::init_fifo() {
    fifo = std::vector<std::vector<FIFOEntry>>(
        vrows(), std::vector<FIFOEntry>(vcols()));
}

void Simulator::refresh_states() {
    for (size_t i = 0; i < peModule.pe.size(); ++i) {
        for (size_t j = 0; j < peModule.pe[i].size(); ++j) {
            peModule.pe[i][j].update(peModule.next_pe[i][j]);
        }
    }
    for (size_t i = 0; i < switchModule.switches.size(); ++i) {
        for (size_t j = 0; j < switchModule.switches[i].size(); ++j) {
            switchModule.switches[i][j].update(switchModule.next_switches[i][j]);
        }
    }
}

void Simulator::store_c_to_spad() {
    for (size_t i = 0; i < peModule.pe.size(); ++i) {
        for (size_t j = 0; j < peModule.pe[i].size(); ++j) {
            auto& pe = peModule.pe[i][j];
            auto& next_pe = peModule.next_pe[i][j];
            next_pe.update(pe);
            if (pe.c.c_col.has_value()) {
                std::unordered_map<int, std::tuple<int, int, int>> c_data;
                c_data[pe.c.c_col.value()] = std::make_tuple(pe.c.val, pe.c.m.value_or(0), pe.c.n.value_or(0));
                bool valid_store = spadModule.store(static_cast<int>(i), pe.c.c_col.value(), c_data);
                if (valid_store) {
                    next_pe.c = PEModule::idle_pe().c;
                }
            }
        }
    }
}

bool Simulator::fifo_is_empty() const {
    if (!cfg.enable_sw_pe_fifo) {
        return true;
    }
    for (size_t i = 0; i < fifo.size(); ++i) {
        for (size_t j = 0; j < fifo[i].size(); ++j) {
            if (fifo[i][j].rptr > fifo[i][j].lptr) {
                return false;
            }
        }
    }
    return true;
}

void Simulator::pop_fifo_to_pe(int i, int j) {
    assert(cfg.enable_sw_pe_fifo);
    int lptr = fifo[i][j].lptr;
    assert(lptr < static_cast<int>(fifo[i][j].pe_update.size()));
    auto& pe_update = fifo[i][j].pe_update[lptr];
    fifo[i][j].lptr++;
    
    std::pair<int, int> orig_idx_a(pe_update.at("a_m"), pe_update.at("a_k"));
    std::unordered_map<std::string, int> b;
    b["val"] = pe_update.at("b_val");
    b["row"] = pe_update.at("b_row");
    b["col"] = pe_update.at("b_col");
    if (pe_update.find("b_n") != pe_update.end()) {
        b["n"] = pe_update.at("b_n");  // Store original output column index
    }
    
    load_a_from_fifo_to_pe(i, j, &controller, &peModule, orig_idx_a, b);
    
    auto [store_c, load_c] = is_new_c(&peModule, i, j, &matrix);
    peModule.store_c[i][j] = store_c;
    peModule.load_c[i][j] = load_c;
}

void Simulator::bypass_fifo_to_pe(int i, int j, const std::unordered_map<std::string, int>& pe_update) {
    std::pair<int, int> orig_idx_a(pe_update.at("a_m"), pe_update.at("a_k"));
    std::unordered_map<std::string, int> b;
    b["val"] = pe_update.at("b_val");
    b["row"] = pe_update.at("b_row");
    b["col"] = pe_update.at("b_col");
    if (pe_update.find("b_n") != pe_update.end()) {
        b["n"] = pe_update.at("b_n");
    }
    load_a_from_fifo_to_pe(i, j, &controller, &peModule, orig_idx_a, b);
    auto [store_c, load_c] = is_new_c(&peModule, i, j, &matrix);
    peModule.store_c[i][j] = store_c;
    peModule.load_c[i][j] = load_c;
}

void Simulator::load_a_from_fifo_to_pe(int i, int j, MemoryController* controller, PEModule* peModule, 
                                       const std::pair<int, int>& orig_idx_a, 
                                       const std::unordered_map<std::string, int>& b) {
    auto& next_pe = peModule->next_pe[i][j];
    peModule->valid_a[i][j] = false;
    int c_col = b.at("col");
    
    // Use original A matrix to get the value, not the tiled one
    int val = 0;
    if (orig_idx_a.first >= 0 && orig_idx_a.second >= 0) {
        val = controller->matrix->A_orig_csr.get(orig_idx_a.first, orig_idx_a.second);
    }
    
    next_pe.status = PEStatus::LOAD;
    next_pe.loadA_cycle = controller->cycle();
    next_pe.a = val;
    next_pe.a_m = orig_idx_a.first;  // Store m index
    next_pe.a_k = orig_idx_a.second;  // Store k index
    next_pe.b.val = b.at("val");
    next_pe.b.row = b.at("row");
    next_pe.b.col = b.at("col");
    // Store original output column index if available
    if (b.find("n") != b.end()) {
        next_pe.b.n = b.at("n");  // Store original output column index
    }
    
    // Always log for rows 4-7
    bool should_log = (orig_idx_a.first >= 4 || (b.find("n") != b.end() && b.at("n") >= 4));
    if (should_log) {
        std::ostringstream oss;
        oss << "[FIFO->PE] Cycle " << controller->cycle()
            << " PE[" << i << "][" << j << "] loaded from FIFO: "
            << "a=" << val << " (m=" << orig_idx_a.first << ", k=" << orig_idx_a.second << ") "
            << "b=" << b.at("val") << " (row=" << b.at("row") << ", col=" << b.at("col") << ")";
        if (b.find("n") != b.end()) {
            oss << " b_n=" << b.at("n");
        }
        controller->log->info(oss.str());
    }
    
    if (controller->cfg.enable_memory_hierarchy && !controller->cfg.bypass_a_memory_hierarchy) {
        controller->submit_load_request(orig_idx_a.first, orig_idx_a.second, "A", i, j, "pe", c_col);
    }
}

std::pair<bool, bool> Simulator::is_new_c(PEModule* peModule, int i, int j, MatrixLoader* matrix) {
    auto& next_pe = peModule->next_pe[i][j];
    
    if (peModule->cfg.ablat_dynmap) {
        bool is_first_time = !next_pe.c.m.has_value();
        bool has_valid_c = next_pe.c.c_col.has_value();
        
        if (peModule->debug()) {
            std::ostringstream oss;
            oss << "[is_new_c] Cycle " << peModule->cycle() 
                << " PE[" << i << "][" << j << "] ABLATION: ablat_dynmap=true"
                << " is_first_time=" << is_first_time
                << " has_valid_c=" << has_valid_c
                << " -> store_c=" << has_valid_c << ", load_c=false";
            peModule->log->debug(oss.str());
        }
        
        return {has_valid_c, false};
    }
    
    if (!next_pe.c.m.has_value()) {
    if (peModule->debug()) {
        std::ostringstream oss;
        oss << "[is_new_c] Cycle " << peModule->cycle() 
            << " PE[" << i << "][" << j << "] First time C: "
            << "a_m=" << (next_pe.a_m.has_value() ? std::to_string(next_pe.a_m.value()) : "none")
            << " b_n=" << (next_pe.b.n.has_value() ? std::to_string(next_pe.b.n.value()) : "none")
            << " b_col=" << (next_pe.b.col.has_value() ? std::to_string(next_pe.b.col.value()) : "none")
            << " -> store_c=false, load_c=true";
        peModule->log->debug(oss.str());
    }
        return {false, true};
    }
    
    // Check if m or n changed (store_c = True if m or n changed)
    bool store_c = false;
    if (next_pe.a_m.has_value() && next_pe.b.n.has_value()) {
        // Compare a.m with c.m, and b.n (output column) with c.n
        store_c = (next_pe.a_m.value() != next_pe.c.m.value()) || 
                  (next_pe.b.n.value() != next_pe.c.n.value());
    }
    
    // load_c = True if same block and store_c is True
    bool load_c = false;
    if (store_c && next_pe.b.row.has_value() && next_pe.c.last_k.has_value()) {
        load_c = matrix->b_is_same_block(next_pe.b.row.value(), next_pe.c.last_k.value());
    }
    
    if (peModule->debug()) {
        std::ostringstream oss;
        oss << "[is_new_c] Cycle " << peModule->cycle() 
            << " PE[" << i << "][" << j << "] C check: "
            << "a_m=" << (next_pe.a_m.has_value() ? std::to_string(next_pe.a_m.value()) : "none")
            << " c_m=" << (next_pe.c.m.has_value() ? std::to_string(next_pe.c.m.value()) : "none")
            << " b_n=" << (next_pe.b.n.has_value() ? std::to_string(next_pe.b.n.value()) : "none")
            << " c_n=" << (next_pe.c.n.has_value() ? std::to_string(next_pe.c.n.value()) : "none")
            << " b_col=" << (next_pe.b.col.has_value() ? std::to_string(next_pe.b.col.value()) : "none")
            << " -> store_c=" << store_c << " load_c=" << load_c;
        peModule->log->debug(oss.str());
    }
    
    return {store_c, load_c};
}

bool Simulator::fifo_not_full(int i, int j) const {
    assert(cfg.enable_sw_pe_fifo);
    int qsize = fifo[i][j].rptr - fifo[i][j].lptr;
    return qsize < cfg.sw_pe_fifo_size;
}

bool Simulator::is_done() const {
    return controller.get_is_done() &&
           switchModule.num_active_switches() == 0 &&
           peModule.num_active_pes() == 0 &&
           fifo_is_empty() &&
           switchModule.all_b_loader_fifos_empty();
}

bool Simulator::store_is_done() const {
    return peModule.log_active_c() == 0;
}

bool Simulator::check_output(const Matrix<int8_t>& cpu_output) const {
    for (int i = 0; i < matrix.M; ++i) {
        for (int j = 0; j < matrix.N; ++j) {
            if (acc_output.get(i, j) != cpu_output(i, j)) {
                if (cfg.verbose) {
                    std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                              << acc_output.get(i, j) << " != " << cpu_output(i, j) << std::endl;
                }
                return false;
            }
        }
    }
    log->info("All outputs match!");
    return true;
}

bool Simulator::check_output(const Matrix<int>& cpu_output) const {
    for (int i = 0; i < matrix.M; ++i) {
        for (int j = 0; j < matrix.N; ++j) {
            if (acc_output.get(i, j) != cpu_output(i, j)) {
                if (cfg.verbose) {
                    std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                              << acc_output.get(i, j) << " != " << cpu_output(i, j) << std::endl;
                }
                return false;
            }
        }
    }
    log->info("All outputs match!");
    return true;
}

void Simulator::log_cycle() {
    snapshot.clear();
    snapshot["cycle"] = std::to_string(stats.cycle);
    
    // Collect b_positions from PEs and switches
    auto pe_positions = peModule.b_positions(stats.cycle);
    auto sw_positions = switchModule.b_positions(stats.cycle);
    
    // Convert to JSON-compatible format
    json b_positions_json = json::array();
    
    // Add PE positions
    for (const auto& pos : pe_positions) {
        json pos_json;
        pos_json["pos"] = "pe";
        pos_json["pe_row"] = pos.at("pe_row");
        pos_json["pe_col"] = pos.at("pe_col");
        pos_json["b_row"] = pos.at("b_row");
        pos_json["b_col"] = pos.at("b_col");
        pos_json["c_row"] = pos.at("c_row");
        pos_json["c_col"] = pos.at("c_col");
        pos_json["value"] = pos.at("value");
        
        // Convert status enum to string (matching Python enum values)
        int status_val = pos.at("status");
        std::string status_str;
        if (status_val == 0) status_str = "IDLE";
        else if (status_val == 1) status_str = "LOAD";
        else if (status_val == 2) status_str = "MAC";
        else status_str = "UNKNOWN";
        pos_json["status"] = status_str;
        
        // Always include remaining (animator expects it)
        int remaining = pos.at("remaining");
        pos_json["remaining"] = remaining;
        b_positions_json.push_back(pos_json);
    }
    
    // Add switch positions
    for (const auto& pos : sw_positions) {
        json pos_json;
        int pos_type = pos.at("pos");
        if (pos_type == 1) { // switch
            pos_json["pos"] = "switch";
            pos_json["sw_row"] = pos.at("sw_row");
            pos_json["sw_col"] = pos.at("sw_col");
            pos_json["b_row"] = pos.at("b_row");
            pos_json["b_col"] = pos.at("b_col");
            pos_json["value"] = pos.at("value");
            pos_json["c_col"] = pos.at("c_col");
            
            // Convert status enum to string (matching Python enum values)
            int status_val = pos.at("status");
            std::string status_str;
            if (status_val == 0) status_str = "IDLE";
            else if (status_val == 1) status_str = "LOAD_B";
            else if (status_val == 2) status_str = "MOVE";
            else status_str = "UNKNOWN";
            pos_json["status"] = status_str;
            
            // Always include remaining (animator may expect it)
            int remaining = pos.at("remaining");
            pos_json["remaining"] = remaining;
        } else { // idle_switch (pos_type == 2)
            pos_json["pos"] = "idle_switch";
            pos_json["sw_row"] = pos.at("sw_row");
            pos_json["sw_col"] = pos.at("sw_col");
            int c_col = pos.at("c_col");
            if (c_col >= 0) {
                pos_json["c_col"] = c_col;
            }
        }
        b_positions_json.push_back(pos_json);
    }
    
    // Store snapshot data as strings (matching Python structure)
    snapshot["b_positions"] = b_positions_json.dump();
    snapshot["num_active_pes"] = std::to_string(peModule.num_active_pes());
    
    double utilization = 0.0;
    int num_pes = peModule.num_pes();
    if (num_pes > 0) {
        utilization = static_cast<double>(peModule.num_active_pes()) / num_pes;
    }
    snapshot["utilization"] = std::to_string(utilization);
    
    // Store snapshot in trace
    trace.push_back(snapshot);
}

void Simulator::dump_trace(const std::string& filename) {
    json output;
    
    // Convert matrices A and B to JSON arrays using CSR format
    json A_json = json::array();
    json B_json = json::array();

    const auto& A_csr = matrix.A_orig_csr;
    const auto& B_csr = matrix.B_orig_csr;

    for (int i = 0; i < A_csr.rows_; ++i) {
        json row_a = json::array();
        for (int j = 0; j < A_csr.cols_; ++j) {
            row_a.push_back(A_csr.get(i, j));
        }
        A_json.push_back(row_a);
    }

    for (int i = 0; i < B_csr.rows_; ++i) {
        json row_b = json::array();
        for (int j = 0; j < B_csr.cols_; ++j) {
            row_b.push_back(B_csr.get(i, j));
        }
        B_json.push_back(row_b);
    }
    
    output["A"] = A_json;
    output["B"] = B_json;

    // Add PE grid dimensions for animator
    output["pe_rows"] = vrows();
    output["pe_cols"] = vcols();

    // Convert trace to JSON array
    json trace_json = json::array();
    for (const auto& snapshot : trace) {
        json snapshot_json;
        if (snapshot.find("cycle") != snapshot.end()) {
            snapshot_json["cycle"] = std::stoi(snapshot.at("cycle"));
        }
        if (snapshot.find("b_positions") != snapshot.end()) {
            snapshot_json["b_positions"] = json::parse(snapshot.at("b_positions"));
        }
        if (snapshot.find("num_active_pes") != snapshot.end()) {
            snapshot_json["num_active_pes"] = std::stoi(snapshot.at("num_active_pes"));
        }
        if (snapshot.find("utilization") != snapshot.end()) {
            snapshot_json["utilization"] = std::stod(snapshot.at("utilization"));
        }
        trace_json.push_back(snapshot_json);
    }
    
    output["trace"] = trace_json;
    
    // Write to file
    std::ofstream file(filename);
    if (file.is_open()) {
        file << std::setw(4) << output << std::endl;
        file.close();
        if (verbose()) {
            log->info("Trace dumped to " + filename);
        }
    } else {
        log->error("Failed to open file for writing trace: " + filename);
    }
}

void Simulator::dump_config(const std::string& filename) {
    try {
        std::string serialized = cfg.serialize();
        if (serialized.empty()) {
            std::cerr << "Error: Config serialization returned empty string" << std::endl;
            return;
        }
        
        json output = json::parse(serialized);
        
        size_t last_slash = filename.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            std::string dir = filename.substr(0, last_slash);
            struct stat info;
            if (stat(dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
                std::string mkdir_cmd = "mkdir -p " + dir;
                if (system(mkdir_cmd.c_str()) != 0) {
                    std::cerr << "Error: Failed to create directory: " << dir << std::endl;
                }
            }
        }
        
        std::ofstream file(filename);
        if (file.is_open()) {
            file << std::setw(4) << output << std::endl;
            file.close();
            if (log && verbose()) {
                log->info("Config dumped to " + filename);
            }
        } else {
            std::cerr << "Error: Failed to open file for writing config: " << filename << std::endl;
            if (log) log->error("Failed to open file for writing config: " + filename);
        }
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse config JSON: " << e.what() << std::endl;
        if (log) log->error("Failed to parse config JSON: " + std::string(e.what()));
    } catch (const std::exception& e) {
        std::cerr << "Error: Error dumping config: " << e.what() << std::endl;
        if (log) log->error("Error dumping config: " + std::string(e.what()));
    }
}

void Simulator::dump_stats(const std::string& filename, bool include_traces) {
    try {
        json output = json::parse(stats.serialize(include_traces));
        
        std::ofstream file(filename);
        if (file.is_open()) {
            file << std::setw(4) << output << std::endl;
            file.close();
            if (log && verbose()) {
                log->info("Stats dumped to " + filename);
            }
        } else {
            if (log) {
                log->error("Failed to open file for writing stats: " + filename);
            }
        }
    } catch (const json::parse_error& e) {
        if (log) {
            log->error("Failed to parse stats JSON: " + std::string(e.what()));
        }
    } catch (const std::exception& e) {
        if (log) {
            log->error("Error dumping stats: " + std::string(e.what()));
        }
    }
}

std::string Simulator::serialize_state(const std::string& format, bool include_traces) const {
    try {
        json output;
        output["config"] = json::parse(cfg.serialize());
        output["stats"] = json::parse(stats.serialize(include_traces));
        
        if (format == "json") {
            return output.dump(4);
        } else {
            if (log) {
                log->error("Unsupported format: " + format);
            }
            return "";
        }
    } catch (const json::parse_error& e) {
        if (log) {
            log->error("Failed to parse JSON in serialize_state: " + std::string(e.what()));
        }
        return "";
    } catch (const std::exception& e) {
        if (log) {
            log->error("Error in serialize_state: " + std::string(e.what()));
        }
        return "";
    }
}

void Simulator::dump_state(const std::string& file_path, const std::string& format, bool include_traces) {
    try {
        std::string serialized = serialize_state(format, include_traces);
        
        std::ofstream file(file_path);
        if (file.is_open()) {
            file << serialized << std::endl;
            file.close();
            if (log && verbose()) {
                log->info("State dumped to " + file_path);
            }
        } else {
            if (log) {
                log->error("Failed to open file for writing state: " + file_path);
            }
        }
    } catch (const std::exception& e) {
        if (log) {
            log->error("Error in dump_state: " + std::string(e.what()));
        }
    }
}

void Simulator::run_check() {
    // Runtime correctness checking not yet implemented
}

} // namespace csegfold

