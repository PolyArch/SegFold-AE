#include "csegfold/modules/module.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>

using namespace csegfold;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --config FILE         Configuration file (required)\n"
              << "  --out_dir DIR         Output directory (required)\n"
              << "  --densityA FLOAT      Density of matrix A (synthetic mode)\n"
              << "  --densityB FLOAT      Density of matrix B (synthetic mode)\n"
              << "  --size INT            Matrix size (square) (synthetic mode)\n"
              << "  --random_state INT    Random seed (synthetic mode)\n"
              << "  --suitesparse         Load from SuiteSparse .mtx file\n"
              << "  --matrix NAME         Matrix name (used with --suitesparse)\n"
              << "  --matrix_dir DIR      Directory containing SuiteSparse matrices\n"
              << "  --help                Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    std::string config_file;
    std::string out_dir;
    double densityA = 0.1;
    double densityB = 0.1;
    int size = 256;
    int random_state = 0;
    bool suitesparse = false;
    std::string matrix_name;
    std::string matrix_dir;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--out_dir" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--densityA" && i + 1 < argc) {
            densityA = std::stod(argv[++i]);
        } else if (arg == "--densityB" && i + 1 < argc) {
            densityB = std::stod(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            size = std::stoi(argv[++i]);
        } else if (arg == "--random_state" && i + 1 < argc) {
            random_state = std::stoi(argv[++i]);
        } else if (arg == "--suitesparse") {
            suitesparse = true;
        } else if (arg == "--matrix" && i + 1 < argc) {
            matrix_name = argv[++i];
        } else if (arg == "--matrix_dir" && i + 1 < argc) {
            matrix_dir = argv[++i];
        }
    }

    // Validate required arguments
    if (config_file.empty() || out_dir.empty()) {
        std::cerr << "Error: --config and --out_dir are required\n";
        print_usage(argv[0]);
        return 1;
    }

    if (suitesparse && matrix_name.empty()) {
        std::cerr << "Error: --matrix is required when using --suitesparse\n";
        print_usage(argv[0]);
        return 1;
    }

    // Create output directory if it doesn't exist
    struct stat info;
    if (stat(out_dir.c_str(), &info) != 0) {
        std::string mkdir_cmd = "mkdir -p " + out_dir;
        if (system(mkdir_cmd.c_str()) != 0) {
            std::cerr << "Error: Failed to create output directory: " << out_dir << std::endl;
            return 1;
        }
    }

    // Print configuration
    std::cout << "\n=== Running C++ Simulator ===" << std::endl;
    std::cout << "Config file: " << config_file << std::endl;
    std::cout << "Output dir: " << out_dir << std::endl;
    if (suitesparse) {
        std::cout << "Mode: SuiteSparse" << std::endl;
        std::cout << "Matrix: " << matrix_name << std::endl;
    } else {
        std::cout << "Mode: Synthetic" << std::endl;
        std::cout << "Matrix size: " << size << "x" << size << std::endl;
        std::cout << "Density A: " << densityA << std::endl;
        std::cout << "Density B: " << densityB << std::endl;
        std::cout << "Random state: " << random_state << std::endl;
    }

    try {
        // Load configuration
        reset();
        load_cfg(config_file);

        Matrix<int8_t> A(1, 1);
        Matrix<int8_t> B(1, 1);

        if (suitesparse) {
            // SuiteSparse mode: load from .mtx file
            std::string mtx_path;
            if (!matrix_dir.empty()) {
                mtx_path = matrix_dir + "/" + matrix_name + "/" + matrix_name + ".mtx";
            } else {
                mtx_path = matrix_name;  // Assume full path given
            }

            std::cout << "\nLoading SuiteSparse matrix: " << mtx_path << std::endl;

            // Load via CSR first (O(nnz) memory), then convert to dense
            CSRMatrix csr = load_mtx_to_csr(mtx_path);
            auto [rows, cols] = csr.shape();
            size = rows;

            std::cout << "  Dimensions: " << rows << "x" << cols << " (nnz=" << csr.nnz() << ")" << std::endl;

            // Check if dense conversion is feasible
            int64_t dense_bytes = static_cast<int64_t>(rows) * cols;
            if (dense_bytes > 500000000LL) {  // > 500M elements
                std::cerr << "Error: Matrix too large for dense conversion ("
                          << rows << "x" << cols << " = " << dense_bytes << " bytes)" << std::endl;
                std::cerr << "Maximum supported: ~22000x22000" << std::endl;
                return 1;
            }

            std::cout << "  Converting to dense format..." << std::endl;
            A = csr_to_dense_matrix(csr);
            B = csr_to_dense_matrix(csr);  // A = B for SuiteSparse (SpMM: A * A^T style)

            // Override config with actual matrix size
            update_cfg({
                {"M", std::to_string(rows)},
                {"N", std::to_string(cols)},
                {"K", std::to_string(cols)},
            });
        } else {
            // Synthetic mode: generate random matrices
            update_cfg({
                {"M", std::to_string(size)},
                {"N", std::to_string(size)},
                {"K", std::to_string(size)},
                {"densityA", std::to_string(densityA)},
                {"densityB", std::to_string(densityB)},
                {"random_state", std::to_string(random_state)},
            });

            std::cout << "\nGenerating matrices..." << std::endl;
            MatrixParams params;
            params.M = size;
            params.K = size;
            params.N = size;
            params.density_a = densityA;
            params.density_b = densityB;
            params.random_state = random_state;

            // Generate A and B without computing C_expected (saves memory)
            std::mt19937 gen(random_state);
            std::uniform_real_distribution<double> dis(0.0, 1.0);

            A = Matrix<int8_t>(size, size);
            B = Matrix<int8_t>(size, size);

            for (int i = 0; i < size; ++i) {
                for (int j = 0; j < size; ++j) {
                    A(i, j) = (dis(gen) < densityA) ? 1 : 0;
                }
            }
            for (int i = 0; i < size; ++i) {
                for (int j = 0; j < size; ++j) {
                    B(i, j) = (dis(gen) < densityB) ? 1 : 0;
                }
            }
        }

        int a_nnz = A.nnz();
        int b_nnz = B.nnz();
        std::cout << "  A: " << A.rows() << "x" << A.cols() << " (nnz=" << a_nnz << ")" << std::endl;
        std::cout << "  B: " << B.rows() << "x" << B.cols() << " (nnz=" << b_nnz << ")" << std::endl;

        // Create simulator
        std::cout << "\nCreating simulator..." << std::endl;
        SegfoldSimulator sim(A, B);

        // Run simulation
        std::cout << "Running simulation..." << std::endl;
        sim.run();

        std::cout << "Simulation completed!" << std::endl;
        std::cout << "  Cycles: " << sim.stats.cycle << std::endl;
        std::cout << "  Success: " << (sim.success ? "true" : "false") << std::endl;
        std::cout << "  MACs: " << sim.stats.macs << std::endl;
        std::cout << "  Avg Util: " << sim.stats.avg_util << std::endl;
        std::cout << "  SPAD loads: " << sim.stats.spad_load_hits << std::endl;
        std::cout << "  SPAD stores: " << sim.stats.spad_stores << std::endl;
        std::cout << "  Avg B on switch: " << sim.stats.avg_b_elements_on_switch << std::endl;
        std::cout << "  Avg PEs waiting SPAD: " << sim.stats.avg_pes_waiting_spad << std::endl;

        // Generate output filenames
        std::string prefix;
        if (suitesparse) {
            prefix = out_dir + "/sim_" + matrix_name;
        } else {
            prefix = out_dir + "/sim_" +
                     "s" + std::to_string(size) +
                     "_dA" + std::to_string(static_cast<int>(densityA * 100)) +
                     "_dB" + std::to_string(static_cast<int>(densityB * 100)) +
                     "_r" + std::to_string(random_state);
        }

        std::string stats_file = prefix + "_stats.json";
        std::string config_out_file = prefix + "_config.json";
        std::string trace_file = prefix + "_trace.json";

        // Save results
        std::cout << "\nSaving results..." << std::endl;
        sim.dump_stats(stats_file, false);
        std::cout << "  Stats: " << stats_file << std::endl;

        sim.dump_config(config_out_file);
        std::cout << "  Config: " << config_out_file << std::endl;

        sim.dump_trace(trace_file);
        std::cout << "  Trace: " << trace_file << std::endl;

        // Verify output
        if (sim.success) {
            std::cout << "\nSimulation completed successfully!" << std::endl;
            return 0;
        } else {
            std::cerr << "\nSimulation completed with warnings/errors" << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << std::endl;
        return 1;
    }
}
