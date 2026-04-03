#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <algorithm>
#include <vector>

using namespace csegfold;

void test_dense_tiling();
void test_group_tiling();

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

void test_matrixLoader_creation() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"virtual_pe_row_num", "4"},
        {"virtual_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader loader(A, B);
    
    test_assert(loader.M == 8, "MatrixLoader M dimension");
    test_assert(loader.N == 8, "MatrixLoader N dimension");
    test_assert(loader.K == 8, "MatrixLoader K dimension");
    test_assert(loader.A.rows() == 8, "MatrixLoader A rows");
    test_assert(loader.B.rows() == 8, "MatrixLoader B rows");
    test_assert(loader.C_csr_result.rows_ == 8, "MatrixLoader C computed");
}

void test_init_k_to_tile_id() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_dynamic_tiling", "false"}  // Use dense_tiling for predictable tile count
    });
    
    Matrix<int8_t> A(8, 16, 0);
    Matrix<int8_t> B(16, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader loader(A, B);
    
    // With dense_tiling: A(8,16) with 4 PEs creates 2x2=4 tiles
    // A_indexed.cols_ = num_tiles * K = 4 * 16 = 64
    // Mapping: k / K = k / 16 gives tile ID
    size_t expected_size = static_cast<size_t>(loader.A_indexed.cols_);  // Should be 4 * 16 = 64
    int num_tiles = static_cast<int>(expected_size / loader.K);
    test_assert(loader.k_to_tile_id.size() == expected_size, 
                std::string("k_to_tile_id has ") + std::to_string(expected_size) + " entries (num_tiles * K)");
    test_assert(loader.k_to_tile_id[0] == 0, "k=0 maps to tile 0");
    test_assert(loader.k_to_tile_id[15] == 0, "k=15 maps to tile 0");
    test_assert(loader.k_to_tile_id[16] == 1, "k=16 maps to tile 1");
    test_assert(loader.k_to_tile_id[31] == 1, "k=31 maps to tile 1");
    if (expected_size >= 64) {
        test_assert(loader.k_to_tile_id[32] == 2, "k=32 maps to tile 2");
        test_assert(loader.k_to_tile_id[47] == 2, "k=47 maps to tile 2");
        test_assert(loader.k_to_tile_id[48] == 3, "k=48 maps to tile 3");
        test_assert(loader.k_to_tile_id[63] == 3, "k=63 maps to tile 3");
    }
    std::cout << "  k_to_tile_id maps " << expected_size << " K indices across " 
              << num_tiles << " tiles (K=" << loader.K << ")" << std::endl;
}

void test_ideal_data_transfer() {
    reset();
    Stats test_stats;
    stats_ = test_stats;
    
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1; A(0, 1) = 1; A(1, 1) = 1;
    B(0, 0) = 1; B(1, 1) = 1; B(2, 2) = 1; B(3, 3) = 1;
    
    MatrixLoader loader(A, B);
    
    test_assert(stats_.ideal_a == 3, "ideal_a = 3");
    test_assert(stats_.ideal_b == 4, "ideal_b = 4");
    std::cout << "  ideal_c = " << stats_.ideal_c << std::endl;
    test_assert(stats_.ideal_c > 0, "ideal_c computed");
}

void test_tile_data_transfer() {
    reset();
    Stats test_stats;
    stats_ = test_stats;
    
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(4, 4, 1);
    Matrix<int8_t> B(4, 4, 1);
    
    MatrixLoader loader(A, B);
    
    test_assert(stats_.tile_a == 16, "tile_a = 16 (all non-zero)");
    test_assert(stats_.tile_b == 16, "tile_b = 16 (all non-zero)");
    test_assert(stats_.tile_c == 16, "tile_c = 16 (all non-zero)");
}

void test_data_transfer_helper() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(3, 3, 0);
    Matrix<int8_t> B(3, 3, 0);
    
    A(0, 0) = 1; A(1, 1) = 1; A(2, 2) = 1;
    B(0, 0) = 1; B(1, 1) = 1;

    MatrixLoader loader(A, B);
    auto [a_nnz, b_nnz, c_nnz] = loader.data_transfer(A, B, loader.C_csr_result.nnz());

    test_assert(a_nnz == 3, "data_transfer A NNZ");
    test_assert(b_nnz == 2, "data_transfer B NNZ");
    test_assert(c_nnz == loader.C_csr_result.nnz(), "data_transfer C NNZ");
}

void test_b_csr_conversion() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(4, 4, 0);
    Matrix<int8_t> B(4, 4, 0);
    
    A(0, 0) = 1;
    B(0, 0) = 2; B(0, 2) = 3;
    B(1, 1) = 4;
    B(2, 3) = 5;
    
    MatrixLoader loader(A, B);
    
    test_assert(loader.B_csr.size() == 4, "B_csr has 4 rows");
    test_assert(loader.B_csr[0].size() == 2, "B row 0 has 2 elements");
    test_assert(loader.B_csr[0][0].first == 0, "B[0][0] col index");
    test_assert(loader.B_csr[0][0].second == 2, "B[0][0] value");
    test_assert(loader.B_csr[0][1].first == 2, "B[0][1] col index");
    test_assert(loader.B_csr[0][1].second == 3, "B[0][1] value");
    
    test_assert(loader.B_csr[1].size() == 1, "B row 1 has 1 element");
    test_assert(loader.B_csr[2].size() == 1, "B row 2 has 1 element");
    test_assert(loader.B_csr[3].size() == 0, "B row 3 is empty");
}

void test_c_csr_conversion() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(3, 3, 0);
    Matrix<int8_t> B(3, 3, 0);
    
    A(0, 0) = 1; A(1, 1) = 1; A(2, 2) = 1;
    B(0, 0) = 2; B(1, 1) = 3; B(2, 2) = 4;
    
    MatrixLoader loader(A, B);
    
    test_assert(loader.C_csr.size() == 3, "C_csr has 3 rows");
    test_assert(loader.C_csr[0].size() == 1, "C row 0 has 1 element");
    test_assert(loader.C_csr[0][0].first == 0, "C[0][0] col");
    test_assert(loader.C_csr[0][0].second == 2, "C[0][0] = 1*2 = 2");
}

void test_get_original_index_a() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    A(0, 0) = 1;
    A(2, 3) = 1;  // Set a non-zero element at (2, 3) for testing
    B(0, 0) = 1;
    
    MatrixLoader loader(A, B);
    
    // Test with non-zero element
    auto [row, col] = loader.get_original_index_a(2, 3);
    test_assert(row == 2 && col == 3, "get_original_index_a returns coords for non-zero element");
    
    // Test with zero element (should return -1, -1)
    auto [row_zero, col_zero] = loader.get_original_index_a(1, 1);
    test_assert(row_zero == -1 && col_zero == -1, "get_original_index_a returns -1,-1 for zero element");
    
    // Test out of bounds
    auto [row_neg, col_neg] = loader.get_original_index_a(-1, 0);
    test_assert(row_neg == -1 && col_neg == -1, "Out of bounds returns -1,-1");
}

void test_init_indices() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"}
    });
    
    Matrix<int8_t> A(3, 4, 0);
    Matrix<int8_t> B(4, 3, 0);
    A(0, 0) = 1;
    A(1, 2) = 1;  // Add another non-zero for testing
    A(2, 3) = 1;  // Add another non-zero for testing
    B(0, 0) = 1;
    
    MatrixLoader loader(A, B);
    
    // Note: After process_tiles(), A_indexed and B_indexed have tiled shapes, not original shapes
    // So we check the original matrices instead
    test_assert(loader.A_orig_csr.rows_ == 3, "A_orig_csr has 3 rows");
    test_assert(loader.A_orig_csr.cols_ == 4, "A_orig_csr has 4 cols");
    test_assert(loader.B_orig_csr.rows_ == 4, "B_orig_csr has 4 rows");
    test_assert(loader.B_orig_csr.cols_ == 3, "B_orig_csr has 3 cols");
    
    // Check that A_indexed and B_indexed have valid shapes (tiled dimensions)
    test_assert(loader.A_indexed.rows_ > 0, "A_indexed has valid rows after tiling");
    test_assert(loader.A_indexed.cols_ > 0, "A_indexed has valid cols after tiling");
    test_assert(loader.B_indexed.rows_ > 0, "B_indexed has valid rows after tiling");
    test_assert(loader.B_indexed.cols_ > 0, "B_indexed has valid cols after tiling");
    
    // Check that we can retrieve original coordinates from indexed matrices
    // Search through A_indexed to find elements with original coordinates (0,0), (1,2), (2,3)
    bool found_orig_0_0 = false, found_orig_1_2 = false, found_orig_2_3 = false;
    for (int i = 0; i < loader.A_indexed.rows_; ++i) {
        int start = loader.A_indexed.indptr_[i];
        int end = loader.A_indexed.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            auto [orig_row, orig_col] = loader.A_indexed.get_original_coords(i, loader.A_indexed.indices_[idx]);
            if (orig_row == 0 && orig_col == 0) found_orig_0_0 = true;
            if (orig_row == 1 && orig_col == 2) found_orig_1_2 = true;
            if (orig_row == 2 && orig_col == 3) found_orig_2_3 = true;
        }
    }
    test_assert(found_orig_0_0, "A_indexed preserves original coords (0,0)");
    test_assert(found_orig_1_2, "A_indexed preserves original coords (1,2)");
    test_assert(found_orig_2_3, "A_indexed preserves original coords (2,3)");
    
    // Check B_indexed
    bool found_b_orig_0_0 = false;
    for (int i = 0; i < loader.B_indexed.rows_; ++i) {
        int start = loader.B_indexed.indptr_[i];
        int end = loader.B_indexed.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            auto [orig_row, orig_col] = loader.B_indexed.get_original_coords(i, loader.B_indexed.indices_[idx]);
            if (orig_row == 0 && orig_col == 0) {
                found_b_orig_0_0 = true;
                break;
            }
        }
        if (found_b_orig_0_0) break;
    }
    test_assert(found_b_orig_0_0, "B_indexed preserves original coords (0,0)");
}

void test_generate_offsets() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_a_csc", "false"}
    });
    
    Matrix<int8_t> A(2, 3, 0);
    Matrix<int8_t> B(3, 2, 0);
    
    A(0, 0) = 1; A(0, 2) = 1;
    A(1, 1) = 1;
    
    B(0, 0) = 1; B(0, 1) = 1;
    B(2, 0) = 1;
    
    MatrixLoader loader(A, B);
    
    test_assert(loader.A_nnz_offset_cols_ == 3, "A_nnz_offset cols");

    std::cout << "  A_nnz_offset[0,0] = " << loader.A_nnz_offset_get(0, 0) << std::endl;
    std::cout << "  A_nnz_offset[0,2] = " << loader.A_nnz_offset_get(0, 2) << std::endl;
    std::cout << "  A_nnz_offset[1,1] = " << loader.A_nnz_offset_get(1, 1) << std::endl;

    test_assert(loader.A_nnz_offset_get(0, 0) > 0, "A offset[0,0] > 0 (NNZ)");
    test_assert(loader.A_nnz_offset_get(0, 1) == 0, "A offset[0,1] = 0 (zero)");
    test_assert(loader.A_nnz_offset_get(0, 2) > 0, "A offset[0,2] > 0 (NNZ)");
    test_assert(loader.A_nnz_offset_get(1, 0) == 0, "A offset[1,0] = 0 (zero)");
    test_assert(loader.A_nnz_offset_get(1, 1) > 0, "A offset[1,1] > 0 (NNZ)");

    test_assert(loader.B_nnz_offset_get(0, 0) == 1, "B offset[0,0] = 1");
    test_assert(loader.B_nnz_offset_get(0, 1) == 2, "B offset[0,1] = 2");
    test_assert(loader.B_nnz_offset_get(1, 0) == 0, "B offset[1,0] = 0 (zero)");
    test_assert(loader.B_nnz_offset_get(2, 0) == 3, "B offset[2,0] = 3");
}

void test_dense_tiling() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_dynamic_tiling", "false"}
    });
    
    Matrix<int8_t> A(8, 8, 0);
    Matrix<int8_t> B(8, 8, 0);
    
    for (int i = 0; i < 8; ++i) {
        A(i, i) = 1;
        B(i, i) = 1;
    }
    
    MatrixLoader loader(A, B);
    
    test_assert(loader.tiles.size() == 4, "Dense tiling creates 4 tiles for 8×8 matrix");
    
    auto [m_start0, m_end0, n_start0, n_end0] = loader.tiles[0];
    test_assert(m_start0 == 0 && m_end0 == 4, "Tile 0: M range [0,4)");
    test_assert(n_start0 == 0 && n_end0 == 4, "Tile 0: N range [0,4)");
    
    auto [m_start3, m_end3, n_start3, n_end3] = loader.tiles[3];
    test_assert(m_start3 == 4 && m_end3 == 8, "Tile 3: M range [4,8)");
    test_assert(n_start3 == 4 && n_end3 == 8, "Tile 3: N range [4,8)");
}

void test_group_tiling() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_dynamic_tiling", "true"}
    });
    
    {
        Matrix<int8_t> A(16, 16, 0);
        Matrix<int8_t> B(16, 16, 0);
        
        for (int i = 0; i < 16; ++i) {
            A(i, i) = 1;
            B(i, i) = 1;
        }
        
        MatrixLoader loader(A, B);
        
        test_assert(loader.tiles.size() > 0, "Group tiling creates tiles");
        
        bool covers_start = false;
        bool covers_end = false;
        for (const auto& tile : loader.tiles) {
            auto [m_start, m_end, n_start, n_end] = tile;
            if (m_start == 0) covers_start = true;
            if (m_end == 16 || m_end >= 16) covers_end = true;
        }
        test_assert(covers_start, "Tiles cover matrix start");
        test_assert(covers_end, "Tiles cover matrix end");
        
        std::cout << "  Group tiling (group_size=1.0) created " << loader.tiles.size() << " tiles" << std::endl;
    }
}

int main() {
    std::cout << "\n=== Testing MatrixLoader Module ===" << std::endl;
    
    std::cout << "\n--- Basic MatrixLoader Tests ---" << std::endl;
    test_matrixLoader_creation();
    test_b_csr_conversion();
    test_c_csr_conversion();
    
    std::cout << "\n--- Simple Function Tests ---" << std::endl;
    test_init_k_to_tile_id();
    test_ideal_data_transfer();
    test_tile_data_transfer();
    test_data_transfer_helper();
    test_get_original_index_a();
    
    std::cout << "\n--- Index Function Tests ---" << std::endl;
    test_init_indices();
    test_generate_offsets();
    
    std::cout << "\n--- Tiling Function Tests ---" << std::endl;
    test_dense_tiling();
    test_group_tiling();
    
    std::cout << "\n=== All MatrixLoader Tests Passed! ===" << std::endl;
    return 0;
}
