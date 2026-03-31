#include "csegfold/matrix/generator.hpp"
#include "csegfold/modules/module.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace csegfold;

// Test helper
void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

// Test Matrix basic operations
void test_matrix_creation() {
    Matrix<int8_t> mat(5, 5, 0);
    test_assert(mat.rows() == 5, "Matrix rows");
    test_assert(mat.cols() == 5, "Matrix cols");
    test_assert(mat.nnz() == 0, "Empty matrix NNZ");
}

void test_matrix_indexing() {
    Matrix<int8_t> mat(3, 3, 0);
    mat(0, 0) = 1;
    mat(1, 1) = 2;
    mat(2, 2) = 3;
    
    test_assert(mat(0, 0) == 1, "Matrix index (0,0)");
    test_assert(mat(1, 1) == 2, "Matrix index (1,1)");
    test_assert(mat(2, 2) == 3, "Matrix index (2,2)");
    test_assert(mat.nnz() == 3, "Matrix NNZ after setting values");
}

void test_matrix_multiplication() {
    // Test 2x2 identity-like multiplication
    Matrix<int8_t> A(2, 2, 0);
    Matrix<int8_t> B(2, 2, 0);
    
    A(0, 0) = 1; A(0, 1) = 0;
    A(1, 0) = 0; A(1, 1) = 1;
    
    B(0, 0) = 2; B(0, 1) = 3;
    B(1, 0) = 4; B(1, 1) = 5;
    
    Matrix<int8_t> C = A * B;
    
    test_assert(C(0, 0) == 2, "Matrix mult C[0,0]");
    test_assert(C(0, 1) == 3, "Matrix mult C[0,1]");
    test_assert(C(1, 0) == 4, "Matrix mult C[1,0]");
    test_assert(C(1, 1) == 5, "Matrix mult C[1,1]");
}

void test_matrix_multiplication_dense() {
    // Test 3x3 dense multiplication
    Matrix<int8_t> A(3, 3, 1);  // All ones
    Matrix<int8_t> B(3, 3, 1);  // All ones
    
    Matrix<int8_t> C = A * B;
    
    // Each element should be 3 (sum of 1*1 + 1*1 + 1*1)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            test_assert(C(i, j) == 3, "Dense matrix mult C[" + std::to_string(i) + "," + std::to_string(j) + "]");
        }
    }
    test_assert(C.nnz() == 9, "Dense matrix mult NNZ");
}

void test_csr_conversion() {
    Matrix<int8_t> mat(3, 3, 0);
    mat(0, 0) = 1;
    mat(0, 2) = 2;
    mat(1, 1) = 3;
    mat(2, 0) = 4;
    mat(2, 2) = 5;
    
    CSRMatrix csr = mat.to_csr();
    
    test_assert(csr.shape().first == 3, "CSR rows");
    test_assert(csr.shape().second == 3, "CSR cols");
    test_assert(csr.nnz() == 5, "CSR NNZ");
    test_assert(csr.get(0, 0) == 1, "CSR get(0,0)");
    test_assert(csr.get(0, 2) == 2, "CSR get(0,2)");
    test_assert(csr.get(1, 1) == 3, "CSR get(1,1)");
    test_assert(csr.get(2, 0) == 4, "CSR get(2,0)");
    test_assert(csr.get(2, 2) == 5, "CSR get(2,2)");
    test_assert(csr.get(1, 0) == 0, "CSR get(1,0) - should be 0");
}

void test_csr_get_row() {
    Matrix<int8_t> mat(3, 4, 0);
    mat(0, 0) = 1;
    mat(0, 2) = 2;
    mat(1, 1) = 3;
    mat(2, 0) = 4;
    mat(2, 3) = 5;
    
    CSRMatrix csr = mat.to_csr();
    
    auto row0 = csr.get_row(0);
    test_assert(row0.size() == 2, "Row 0 has 2 elements");
    test_assert(row0[0].first == 0 && row0[0].second == 1, "Row 0, element 0");
    test_assert(row0[1].first == 2 && row0[1].second == 2, "Row 0, element 1");
    
    auto row1 = csr.get_row(1);
    test_assert(row1.size() == 1, "Row 1 has 1 element");
    test_assert(row1[0].first == 1 && row1[0].second == 3, "Row 1, element 0");
    
    auto row2 = csr.get_row(2);
    test_assert(row2.size() == 2, "Row 2 has 2 elements");
}

void test_gen_uniform_matrix() {
    MatrixParams params;
    params.M = 10;
    params.K = 10;
    params.N = 10;
    params.density_a = 0.3;
    params.density_b = 0.3;
    params.c_density = 0.5;
    params.prow = 4;
    params.pcol = 4;
    params.m_variance = 0.0;
    params.k_variance = 0.0;
    params.sparsity_aware = true;
    params.random_state = 42;
    
    auto [A, B, C] = gen_uniform_matrix(params);
    
    test_assert(A.rows() == 10, "Uniform matrix A rows");
    test_assert(A.cols() == 10, "Uniform matrix A cols");
    test_assert(B.rows() == 10, "Uniform matrix B rows");
    test_assert(B.cols() == 10, "Uniform matrix B cols");
    test_assert(C.rows() == 10, "Uniform matrix C rows");
    test_assert(C.cols() == 10, "Uniform matrix C cols");
    
    // Check densities are approximately correct
    double actual_density_a = static_cast<double>(A.nnz()) / (A.rows() * A.cols());
    double actual_density_b = static_cast<double>(B.nnz()) / (B.rows() * B.cols());
    
    std::cout << "  A density: " << actual_density_a << " (expected ~0.3)" << std::endl;
    std::cout << "  B density: " << actual_density_b << " (expected ~0.3)" << std::endl;
    std::cout << "  C nnz: " << C.nnz() << std::endl;
    
    // Densities should be within reasonable range (with randomness)
    test_assert(actual_density_a > 0.1 && actual_density_a < 0.5, "A density in range");
    test_assert(actual_density_b > 0.1 && actual_density_b < 0.5, "B density in range");
}

void test_matrix_correctness() {
    // Test that C = A * B is computed correctly
    MatrixParams params;
    params.M = 5;
    params.K = 5;
    params.N = 5;
    params.density_a = 0.4;
    params.density_b = 0.4;
    params.c_density = 0.5;
    params.prow = 4;
    params.pcol = 4;
    params.m_variance = 0.0;
    params.k_variance = 0.0;
    params.sparsity_aware = true;
    params.random_state = 123;
    
    auto [A, B, C_gen] = gen_uniform_matrix(params);
    Matrix<int8_t> C_computed = A * B;
    
    bool match = true;
    for (int i = 0; i < C_gen.rows(); ++i) {
        for (int j = 0; j < C_gen.cols(); ++j) {
            if (C_gen(i, j) != C_computed(i, j)) {
                std::cerr << "  Mismatch at (" << i << "," << j << "): " 
                          << static_cast<int>(C_gen(i, j)) << " vs " 
                          << static_cast<int>(C_computed(i, j)) << std::endl;
                match = false;
            }
        }
    }
    test_assert(match, "Generated C matches computed A*B");
}

void test_gen_dense_matrix() {
    Matrix<int8_t> mat = gen_dense_matrix(5);
    
    test_assert(mat.rows() == 5, "Dense matrix rows");
    test_assert(mat.cols() == 5, "Dense matrix cols");
    test_assert(mat.nnz() == 25, "Dense matrix NNZ");
    
    // Check all elements are 1
    bool all_ones = true;
    for (int i = 0; i < mat.rows(); ++i) {
        for (int j = 0; j < mat.cols(); ++j) {
            if (mat(i, j) != 1) {
                all_ones = false;
            }
        }
    }
    test_assert(all_ones, "Dense matrix all ones");
}

void test_gen_diag_n_matrix() {
    CSRMatrix mat = gen_diag_n_matrix(5, 2);
    
    test_assert(mat.shape().first == 5, "Diag matrix rows");
    test_assert(mat.shape().second == 5, "Diag matrix cols");
    
    // Check diagonal elements
    test_assert(mat.get(0, 0) == 1, "Diag[0,0]");
    test_assert(mat.get(1, 1) == 1, "Diag[1,1]");
    test_assert(mat.get(2, 2) == 1, "Diag[2,2]");
    
    // Check super-diagonal (n=2 means 2 diagonals)
    test_assert(mat.get(0, 1) == 1, "Diag[0,1]");
    test_assert(mat.get(1, 2) == 1, "Diag[1,2]");
    test_assert(mat.get(2, 3) == 1, "Diag[2,3]");
    
    // Check non-diagonal should be 0
    test_assert(mat.get(0, 3) == 0, "Non-diag should be 0");
}

int main() {
    std::cout << "\n=== Testing Matrix Generator Module ===" << std::endl;
    std::cout << "\n--- Basic Matrix Operations ---" << std::endl;
    test_matrix_creation();
    test_matrix_indexing();
    test_matrix_multiplication();
    test_matrix_multiplication_dense();
    
    std::cout << "\n--- CSR Conversion ---" << std::endl;
    test_csr_conversion();
    test_csr_get_row();
    
    std::cout << "\n--- Matrix Generation Functions ---" << std::endl;
    test_gen_uniform_matrix();
    test_matrix_correctness();
    test_gen_dense_matrix();
    test_gen_diag_n_matrix();
    
    std::cout << "\n=== All Generator Tests Passed! ===" << std::endl;
    return 0;
}




