#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/modules/module.hpp"
#include "csegfold/matrix/generator.hpp"
#include <iostream>
#include <iomanip>

using namespace csegfold;

void print_matrix(const std::string& name, const Matrix<int8_t>& mat, int max_rows = 16, int max_cols = 16) {
    std::cout << "\n" << name << " (shape: " << mat.rows() << "x" << mat.cols() << "):" << std::endl;
    int rows = std::min(mat.rows(), max_rows);
    int cols = std::min(mat.cols(), max_cols);
    
    for (int i = 0; i < rows; ++i) {
        std::cout << "  [";
        for (int j = 0; j < cols; ++j) {
            std::cout << std::setw(3) << static_cast<int>(mat(i, j));
            if (j < cols - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
}

void print_csr_matrix(const std::string& name, const CSRMatrix& mat, int max_rows = 16, int max_cols = 16) {
    std::cout << "\n" << name << " (shape: " << mat.rows_ << "x" << mat.cols_ << "):" << std::endl;
    int rows = std::min(mat.rows_, max_rows);
    int cols = std::min(mat.cols_, max_cols);
    
    for (int i = 0; i < rows; ++i) {
        std::cout << "  [";
        for (int j = 0; j < cols; ++j) {
            int val = mat.get(i, j);
            std::cout << std::setw(3) << val;
            if (j < cols - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
}

void print_indexed_csr_matrix(const std::string& name, const IndexedCSRMatrix& mat, int max_rows = 16, int max_cols = 16) {
    std::cout << "\n" << name << " (shape: " << mat.rows_ << "x" << mat.cols_ << ", nnz=" << mat.nnz() << "):" << std::endl;
    int rows = std::min(mat.rows_, max_rows);
    int cols = std::min(mat.cols_, max_cols);
    
    for (int i = 0; i < rows; ++i) {
        std::cout << "  Row " << i << ": ";
        int start = mat.indptr_[i];
        int end = mat.indptr_[i + 1];
        for (int idx = start; idx < end && idx < start + 10; ++idx) {  // Show first 10 non-zeros per row
            int j = mat.indices_[idx];
            if (j < cols) {
                std::cout << "col[" << j << "]=" << static_cast<int>(mat.data_[idx]) 
                         << "(orig:[" << mat.orig_row_[idx] << "," << mat.orig_col_[idx] << "]) ";
            }
        }
        if (end - start > 10) std::cout << "...";
        std::cout << std::endl;
    }
}

Matrix<int8_t> create_test_matrix_A_8x8() {
    Matrix<int8_t> A(8, 8, 0);
    // Create tridiagonal pattern
    for (int i = 0; i < 8; ++i) {
        if (i > 0) A(i, i-1) = 1;
        A(i, i) = 1;
        if (i < 7) A(i, i+1) = 1;
    }
    return A;
}

Matrix<int8_t> create_test_matrix_B_8x8() {
    Matrix<int8_t> B(8, 8, 0);
    // B is identity-like
    for (int i = 0; i < 8; ++i) {
        B(i, i) = 1;
    }
    return B;
}

int main() {
    reset();
    update_cfg({
        {"physical_pe_row_num", "4"},
        {"physical_pe_col_num", "4"},
        {"enable_dynamic_tiling", "true"},
        {"is_dense", "true"},
        {"verbose", "true"},
    });
    
    Matrix<int8_t> A = create_test_matrix_A_8x8();
    Matrix<int8_t> B = create_test_matrix_B_8x8();
    
    std::cout << "=" << std::string(60, '=') << std::endl;
    std::cout << "C++ Tiling Output" << std::endl;
    std::cout << "=" << std::string(60, '=') << std::endl;
    std::cout << "\nOriginal A shape: " << A.rows() << "x" << A.cols() << std::endl;
    std::cout << "Original B shape: " << B.rows() << "x" << B.cols() << std::endl;
    
    MatrixLoader loader(A, B);
    
    std::cout << "\nNumber of tiles: " << loader.tiles.size() << std::endl;
    for (size_t i = 0; i < loader.tiles.size(); ++i) {
        auto [m_start, m_end, n_start, n_end] = loader.tiles[i];
        std::cout << "  Tile " << i << ": M[" << m_start << ":" << m_end 
                  << "], N[" << n_start << ":" << n_end << "]" << std::endl;
    }
    
    std::cout << "\nA_indexed shape: " << loader.A_indexed.rows_ << "x" << loader.A_indexed.cols_ 
              << " (nnz=" << loader.A_indexed.nnz() << ")" << std::endl;
    std::cout << "B_indexed shape: " << loader.B_indexed.rows_ << "x" << loader.B_indexed.cols_ 
              << " (nnz=" << loader.B_indexed.nnz() << ")" << std::endl;
    
    // Debug: Check a few values from B_indexed before printing
    std::cout << "\nDebug: B_indexed.get_original_coords(0, 0) = ";
    auto [b_r0, b_c0] = loader.B_indexed.get_original_coords(0, 0);
    std::cout << "(" << b_r0 << "," << b_c0 << ")" << std::endl;
    
    std::cout << "Debug: B_indexed.get_original_coords(1, 0) = ";
    auto [b_r1, b_c1] = loader.B_indexed.get_original_coords(1, 0);
    std::cout << "(" << b_r1 << "," << b_c1 << ")" << std::endl;
    
    std::cout << "Debug: B_indexed.get_original_coords(4, 0) = ";
    auto [b_r4, b_c4] = loader.B_indexed.get_original_coords(4, 0);
    std::cout << "(" << b_r4 << "," << b_c4 << ")" << std::endl;
    
    // Print tiled matrices (if dense versions exist)
    if (loader.denseA.has_value()) {
        print_matrix("A_tiled (denseA)", loader.denseA.value(), 8, 16);
    }
    if (loader.denseB.has_value()) {
        print_matrix("B_tiled (denseB)", loader.denseB.value(), 16, 8);
    }
    
    // Print indexed matrices
    print_indexed_csr_matrix("A_indexed", loader.A_indexed, 8, 16);
    print_indexed_csr_matrix("B_indexed", loader.B_indexed, 16, 8);
    
    // Test get_original_index_a for some key positions
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Testing get_original_index_a:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::vector<std::pair<int, int>> test_cases = {
        {0, 0}, {0, 4}, {0, 8}, {0, 12},
        {1, 0}, {1, 4}, {1, 8}, {1, 12},
        {2, 0}, {2, 4}, {2, 8}, {2, 12},
        {3, 0}, {3, 4}, {3, 8}, {3, 12},
    };
    for (auto [pe_row, pe_col] : test_cases) {
        auto [orig_row, orig_col] = loader.get_original_index_a(pe_row, pe_col);
        std::cout << "  get_original_index_a(" << pe_row << ", " << pe_col 
                  << ") -> (" << orig_row << ", " << orig_col << ")" << std::endl;
    }
    
    return 0;
}

