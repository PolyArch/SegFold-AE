#include "csegfold/modules/module.hpp"
#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/matrix/generator.hpp"
#include "csegfold/simulator/segfoldSimulator.hpp"
#include "test_utils.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

using namespace csegfold;
using namespace test_utils;

void test_tiling() {
    reset();
    // Try multiple possible paths for the config file
    std::vector<std::string> possible_paths = {
        "../exp/config/test.yaml",
        "../../exp/config/test.yaml",
        "exp/config/test.yaml"
    };
    
    bool loaded = false;
    for (const auto& path : possible_paths) {
        std::ifstream test_file(path);
        if (test_file.good()) {
            test_file.close();
            load_cfg(path);
            loaded = true;
            std::cout << "Loaded config from: " << path << std::endl;
            break;
        }
        test_file.close();
    }
    
    if (!loaded) {
        std::cerr << "Warning: Could not find test.yaml config file, using defaults" << std::endl;
    }
    update_cfg({{"verbose", "true"}});
    
    Matrix<int8_t> A(256, 256, 0);
    Matrix<int8_t> B(256, 256, 0);
    
    for (int i = 0; i < 256; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader loader(A, B);
    
    std::cout << "Tiles: " << loader.tiles.size() << std::endl;
    for (size_t i = 0; i < loader.tiles.size(); ++i) {
        auto [m_start, m_end, n_start, n_end] = loader.tiles[i];
        std::cout << "Tile " << i << ": M[" << m_start << ":" << m_end << "], N[" << n_start << ":" << n_end << "]" << std::endl;
    }
}

void test_suitesparse() {

    reset();
    // Try multiple possible root paths (works from build/ or project root)
    std::vector<std::string> possible_roots = {"../..", "..", "."};
    std::string root_dir;
    for (const auto& r : possible_roots) {
        std::ifstream test_file(r + "/configs/baseline.yaml");
        if (test_file.good()) { root_dir = r; break; }
    }
    if (root_dir.empty()) root_dir = "../..";
    std::string config_file = root_dir + "/configs/baseline.yaml";
    load_cfg(config_file);
    // update_cfg({{"verbose", "true"}});
    std::string mtx_dir = root_dir + "/benchmarks/data/suitesparse";
    std::string matrix_name = "ca-GrQc";
    Matrix<int8_t> A = load_mtx_matrix(mtx_dir + "/" + matrix_name + "/" + matrix_name + ".mtx");
    Matrix<int8_t> B = load_mtx_matrix(mtx_dir + "/" + matrix_name + "/" + matrix_name + ".mtx");
    // std::cout << "Tiles: " << loader.tiles.size() << std::endl;
    // for (size_t i = 0; i < loader.tiles.size(); ++i) {
    //     auto [m_start, m_end, n_start, n_end] = loader.tiles[i];
    //     std::cout << "Tile " << i << ": M[" << m_start << ":" << m_end << "], N[" << n_start << ":" << n_end << "]" << std::endl;
    // }

    SegfoldSimulator sim(A, B);
    sim.run();
    std::cout << "Stats: " << sim.stats.cycle << " cycles" << std::endl;
    // Use sparse matrix multiplication
    CSRMatrix A_csr = A.to_csr();
    CSRMatrix B_csr = B.to_csr();
    Matrix<int> cpu_output = sparse_multiply(A_csr, B_csr);
    bool output_match = sim.check_output(cpu_output);
    test_assert(output_match, "Output matches expected result");
}
int main() {
    SETUP_SIGNALS();
    
    // test_tiling();
    test_suitesparse();
    return 0;
}