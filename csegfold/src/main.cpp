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
#include <tuple>
#include <cstdint>
#include <algorithm>

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
    std::string matrix_file;
    std::string mtx_file;
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
        } else if (arg == "--matrix-file" && i + 1 < argc) {
            matrix_file = argv[++i];
        } else if (arg == "--mtx-file" && i + 1 < argc) {
            mtx_file = argv[++i];
        }
    }

    // Load config from YAML file
    reset();
    load_cfg(config_path);
    std::cout << "Loaded config from: " << config_path << std::endl;

    if (!mtx_file.empty()) {
        // Load MTX directly into CSR (no dense intermediary)
        std::cerr << "[load] Loading MTX file: " << mtx_file << std::endl;
        CSRMatrix A_csr = load_mtx_to_csr(mtx_file);
        M = A_csr.rows_;
        K = A_csr.cols_;
        N = M;  // B = A^T, so N = M

        std::cerr << "[load] A: " << M << "x" << K << " nnz=" << A_csr.nnz() << std::endl;

        // Compute B = A^T (transpose)
        CSRMatrix B_csr(K, M);
        {
            // Count entries per row of B (= per column of A)
            std::vector<int> row_counts(K, 0);
            for (int i = 0; i < A_csr.rows_; ++i) {
                int start = A_csr.indptr_[i];
                int end = A_csr.indptr_[i + 1];
                for (int idx = start; idx < end; ++idx) {
                    row_counts[A_csr.indices_[idx]]++;
                }
            }
            // Build indptr
            B_csr.indptr_[0] = 0;
            for (int i = 0; i < K; ++i) {
                B_csr.indptr_[i + 1] = B_csr.indptr_[i] + row_counts[i];
            }
            // Fill data
            B_csr.indices_.resize(A_csr.nnz());
            B_csr.data_.resize(A_csr.nnz());
            std::vector<int> pos(K, 0);
            for (int i = 0; i < K; ++i) pos[i] = B_csr.indptr_[i];
            for (int i = 0; i < A_csr.rows_; ++i) {
                int start = A_csr.indptr_[i];
                int end = A_csr.indptr_[i + 1];
                for (int idx = start; idx < end; ++idx) {
                    int j = A_csr.indices_[idx];
                    int p = pos[j]++;
                    B_csr.indices_[p] = i;
                    B_csr.data_[p] = A_csr.data_[idx];
                }
            }
        }
        std::cerr << "[load] B (=A^T): " << K << "x" << M << " nnz=" << B_csr.nnz() << std::endl;

        // Compute C = A * B (sparse)
        std::cerr << "[load] Computing C = A * B (sparse)..." << std::endl;
        CSRMatrix C_csr = sparse_multiply_csr(A_csr, B_csr);
        std::cerr << "[load] C: " << C_csr.rows_ << "x" << C_csr.cols_ << " nnz=" << C_csr.nnz() << std::endl;

        // Update config
        update_cfg({
            {"M", std::to_string(M)},
            {"K", std::to_string(K)},
            {"N", std::to_string(N)},
        });

        std::cout << "Matrices:" << std::endl;
        std::cout << "  A shape: (" << M << ", " << K << "), nnz=" << A_csr.nnz() << std::endl;
        std::cout << "  B shape: (" << K << ", " << N << "), nnz=" << B_csr.nnz() << std::endl;
        std::cout << "  C nnz=" << C_csr.nnz() << std::endl;

        std::cerr << "[init] Creating simulator (tiling/preprocessing)..." << std::endl;
        SegfoldSimulator sim(A_csr, B_csr);
        std::cerr << "[init] Preprocessing done, starting simulation..." << std::endl;

        try {
            sim.run();
            if (sim.success) {
                std::cout << "Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;

                // Verify against C_csr
                bool match = true;
                sim.acc_output.for_each([&](int i, int j, int64_t actual_val) {
                    int32_t expected_val = C_csr.get(i, j);
                    if (actual_val != expected_val) {
                        std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                                  << actual_val << " != " << expected_val << std::endl;
                        match = false;
                    }
                });
                for (int i = 0; i < C_csr.rows_; ++i) {
                    int start = C_csr.indptr_[i];
                    int end = C_csr.indptr_[i + 1];
                    for (int idx = start; idx < end; ++idx) {
                        int j = C_csr.indices_[idx];
                        int32_t expected_val = C_csr.data_[idx];
                        int64_t actual_val = sim.acc_output.get(i, j);
                        if (actual_val != expected_val) {
                            std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                                      << actual_val << " != " << expected_val << std::endl;
                            match = false;
                        }
                    }
                }
                if (match) std::cout << "[INFO] All outputs match!" << std::endl;
            } else {
                std::cout << "Simulation did not complete successfully" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Simulation failed with exception: " << e.what() << std::endl;
            return 1;
        }

        // Save config and stats
        auto tmp_dir = get_tmp_dir();
        std::string timestamp = get_timestamp();
        std::string prefix = "run_" + timestamp;
        save_to_file(tmp_dir / (prefix + "_config.json"), sim.cfg.serialize());
        save_to_file(tmp_dir / (prefix + "_stats.json"), sim.stats.serialize(sim.cfg.save_trace));
        if (sim.cfg.save_trace) {
            sim.dump_trace((tmp_dir / (prefix + "_trace.json")).string());
        }

        return 0;
    }

    Matrix<int8_t> A;
    Matrix<int8_t> B;
    CSRMatrix C_csr;

    if (!matrix_file.empty()) {
        // Load matrices from binary file
        std::ifstream fin(matrix_file, std::ios::binary);
        if (!fin.is_open()) {
            std::cerr << "Failed to open matrix file: " << matrix_file << std::endl;
            return 1;
        }

        int32_t header[5];
        fin.read(reinterpret_cast<char*>(header), 5 * sizeof(int32_t));
        if (!fin) {
            std::cerr << "Failed to read header from matrix file" << std::endl;
            return 1;
        }
        M = header[0];
        K = header[1];
        N = header[2];

        std::cerr << "[load] A: " << M << "x" << K << ", B: " << K << "x" << N << std::endl;

        // Read A matrix (M x K, int8_t, row-major) — bulk read per row
        A = Matrix<int8_t>(M, K);
        {
            std::vector<int8_t> rowbuf(K);
            for (int i = 0; i < M; ++i) {
                fin.read(reinterpret_cast<char*>(rowbuf.data()), K);
                for (int j = 0; j < K; ++j) A(i, j) = rowbuf[j];
            }
        }
        std::cerr << "[load] A loaded" << std::endl;

        // Read B matrix (K x N, int8_t, row-major) — bulk read per row
        B = Matrix<int8_t>(K, N);
        {
            std::vector<int8_t> rowbuf(N);
            for (int i = 0; i < K; ++i) {
                fin.read(reinterpret_cast<char*>(rowbuf.data()), N);
                for (int j = 0; j < N; ++j) B(i, j) = rowbuf[j];
            }
        }
        std::cerr << "[load] B loaded" << std::endl;

        if (!fin) {
            std::cerr << "Failed to read matrix data from file" << std::endl;
            return 1;
        }
        fin.close();

        // Compute C = A * B using sparse multiplication (O(nnz) instead of O(N³))
        std::cerr << "[load] Converting to CSR..." << std::endl;
        CSRMatrix A_csr = A.to_csr();
        CSRMatrix B_csr = B.to_csr();
        std::cerr << "[load] Computing C = A * B (sparse)..." << std::endl;
        C_csr = sparse_multiply_csr(A_csr, B_csr);

        std::cerr << "[load] Done. A_nnz=" << A_csr.nnz() << " B_nnz=" << B_csr.nnz()
                  << " C_nnz=" << C_csr.nnz() << std::endl;
        std::cout << "Loaded matrices from: " << matrix_file << std::endl;
    }

    // Update config with matrix parameters (from CLI or binary header)
    update_cfg({
        {"M", std::to_string(M)},
        {"K", std::to_string(K)},
        {"N", std::to_string(N)},
        {"densityA", std::to_string(densityA)},
        {"densityB", std::to_string(densityB)},
        {"random_state", std::to_string(random_state)}
    });

    if (matrix_file.empty()) {
        // Generate matrices when no binary file is provided
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

        Matrix<int32_t> C_dense;
        std::tie(A, B, C_dense) = gen_uniform_matrix(params);
        C_csr = sparse_multiply_csr(A.to_csr(), B.to_csr());
    }

    std::cout << "Matrices:" << std::endl;
    std::cout << "  A shape: (" << A.rows() << ", " << A.cols() << "), nnz=" << A.nnz() << std::endl;
    std::cout << "  B shape: (" << B.rows() << ", " << B.cols() << "), nnz=" << B.nnz() << std::endl;
    std::cout << "  C nnz=" << C_csr.nnz() << std::endl;

    std::cerr << "[init] Creating simulator (tiling/preprocessing)..." << std::endl;
    SegfoldSimulator sim(A, B);
    std::cerr << "[init] Preprocessing done, starting simulation..." << std::endl;

    try {
        sim.run();
        
        if (sim.success) {
            std::cout << "Simulation completed successfully in " << sim.stats.cycle << " cycles" << std::endl;

            // Verify: iterate sparse acc_output and check against C_csr
            bool match = true;
            sim.acc_output.for_each([&](int i, int j, int64_t actual_val) {
                int32_t expected_val = C_csr.get(i, j);
                if (actual_val != expected_val) {
                    std::cerr << "Output mismatch at (" << i << ", " << j << "): "
                              << actual_val << " != " << expected_val << std::endl;
                    match = false;
                }
            });
            // Also check that all C_csr non-zeros are accounted for
            for (int i = 0; i < C_csr.rows_; ++i) {
                int start = C_csr.indptr_[i];
                int end = C_csr.indptr_[i + 1];
                for (int idx = start; idx < end; ++idx) {
                    int j = C_csr.indices_[idx];
                    int32_t expected_val = C_csr.data_[idx];
                    int64_t actual_val = sim.acc_output.get(i, j);
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



