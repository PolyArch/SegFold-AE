#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/matrix/generator.hpp"
#include "csegfold/modules/module.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S")
            << "_" << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::filesystem::path get_tmp_dir() {
        // Get the path relative to the executable or use a fixed path
        std::filesystem::path tmp_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "tmp";
        std::filesystem::create_directories(tmp_dir);
        return tmp_dir;
    }

    void save_to_file(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path);
        if (file.is_open()) {
            file << content;
            file.close();
            std::cout << "Saved: " << path << std::endl;
        } else {
            std::cerr << "Failed to save: " << path << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    using namespace csegfold;

    // Default config path and matrix parameters
    std::string config_path = "../../exp/config/test.yaml";
    int M = 8, K = 8, N = 8;
    double densityA = 0.5, densityB = 0.5;
    int random_state = 2;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--M" && i + 1 < argc) {
            M = std::stoi(argv[++i]);
        } else if (arg == "--K" && i + 1 < argc) {
            K = std::stoi(argv[++i]);
        } else if (arg == "--N" && i + 1 < argc) {
            N = std::stoi(argv[++i]);
        } else if (arg == "--density-a" && i + 1 < argc) {
            densityA = std::stod(argv[++i]);
        } else if (arg == "--density-b" && i + 1 < argc) {
            densityB = std::stod(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            random_state = std::stoi(argv[++i]);
        }
    }

    // Load config from YAML file
    reset();
    load_cfg(config_path);
    std::cout << "Loaded config from: " << config_path << std::endl;

    // Update config with command line matrix parameters
    update_cfg({
        {"M", std::to_string(M)},
        {"K", std::to_string(K)},
        {"N", std::to_string(N)},
        {"densityA", std::to_string(densityA)},
        {"densityB", std::to_string(densityB)},
        {"random_state", std::to_string(random_state)}
    });

    int prow = config_.physical_pe_row_num;
    int pcol = config_.physical_pe_col_num;

    MatrixParams params{
        M, K, N,
        densityA, densityB,
        std::max(0.0, K * densityA * densityB),
        prow, pcol,
        0.0, 0.0, // m_variance, k_variance
        true, // sparsity_aware
        random_state // random_state
    };
    
    auto [A, B, C] = gen_uniform_matrix(params);

    std::cout << "Generated matrices:" << std::endl;
    std::cout << "  A shape: (" << A.rows() << ", " << A.cols() << "), nnz=" << A.nnz() << std::endl;
    std::cout << "  B shape: (" << B.rows() << ", " << B.cols() << "), nnz=" << B.nnz() << std::endl;
    std::cout << "  C shape: (" << C.rows() << ", " << C.cols() << "), nnz=" << C.nnz() << std::endl;

    SegfoldSimulator sim(A, B);
    
    try {
        sim.run();
        
        if (sim.success) {
            std::cout << "Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;

            // Compare element by element using int64_t to handle large values
            // C was already computed with int32_t above
            bool match = true;
            for (int i = 0; i < C.rows(); ++i) {
                for (int j = 0; j < C.cols(); ++j) {
                    int64_t actual_val = sim.acc_output(i, j);
                    int32_t expected_val = C(i, j);
                    if (actual_val != expected_val) {
                        std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                                  << actual_val << " != " << expected_val << std::endl;
                        match = false;
                    }
                }
            }
            if (match) {
                std::cout << "[INFO] All outputs match!" << std::endl;
            }
        } else {
            std::cout << "Simulation did not complete successfully" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Simulation failed with exception: " << e.what() << std::endl;
        return 1;
    }

    // Save config and stats to tmp directory
    auto tmp_dir = get_tmp_dir();
    std::string timestamp = get_timestamp();
    std::string prefix = "run_" + timestamp;

    // Save config
    save_to_file(tmp_dir / (prefix + "_config.json"), sim.cfg.serialize());

    // Save stats (include traces if save_trace was enabled)
    save_to_file(tmp_dir / (prefix + "_stats.json"), sim.stats.serialize(sim.cfg.save_trace));

    // Save trace data for animation (includes matrices A, B and per-cycle b_positions)
    if (sim.cfg.save_trace) {
        auto trace_path = tmp_dir / (prefix + "_trace.json");
        sim.dump_trace(trace_path.string());
    }

    return 0;
}



