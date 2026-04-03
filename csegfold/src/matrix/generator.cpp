#include "csegfold/matrix/generator.hpp"
#include <stdexcept>
#include <algorithm>
#include <random>
#include <fstream>
#include <sstream>
#include <string>

namespace csegfold {

// CSRMatrix implementation
CSRMatrix::CSRMatrix(int rows, int cols) : rows_(rows), cols_(cols) {
    indptr_.resize(rows + 1, 0);
}

int CSRMatrix::nnz() const {
    return static_cast<int>(data_.size());
}

int CSRMatrix::get(int row, int col) const {
    int start = indptr_[row];
    int end = indptr_[row + 1];
    for (int i = start; i < end; ++i) {
        if (indices_[i] == col) {
            return data_[i];
        }
    }
    return 0;
}

std::vector<std::pair<int, int>> CSRMatrix::get_row(int row) const {
    std::vector<std::pair<int, int>> result;
    int start = indptr_[row];
    int end = indptr_[row + 1];
    for (int i = start; i < end; ++i) {
        result.emplace_back(indices_[i], data_[i]);
    }
    return result;
}

// IndexedCSRMatrix implementation
IndexedCSRMatrix::IndexedCSRMatrix(int rows, int cols) : rows_(rows), cols_(cols) {
    indptr_.resize(rows + 1, 0);
}

std::tuple<int8_t, int32_t, int32_t> IndexedCSRMatrix::get(int row, int col) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        return {0, -1, -1};
    }
    
    int start = indptr_[row];
    int end = indptr_[row + 1];
    for (int i = start; i < end; ++i) {
        if (indices_[i] == col) {
            return {data_[i], orig_row_[i], orig_col_[i]};
        }
    }
    return {0, -1, -1};
}

std::pair<int, int> IndexedCSRMatrix::get_original_coords(int row, int col) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        return {-1, -1};
    }
    
    int start = indptr_[row];
    int end = indptr_[row + 1];
    for (int i = start; i < end; ++i) {
        if (indices_[i] == col) {
            return {orig_row_[i], orig_col_[i]};
        }
    }
    return {-1, -1};
}

// Helper function to convert Matrix to CSR
// Template specialization for int8_t
template<>
CSRMatrix matrix_to_csr<int8_t>(const Matrix<int8_t>& mat) {
    CSRMatrix csr(mat.rows(), mat.cols());
    auto& indptr = csr.indptr_;
    auto& indices = csr.indices_;
    auto& data = csr.data_;
    
    for (int i = 0; i < mat.rows(); ++i) {
        for (int j = 0; j < mat.cols(); ++j) {
            int8_t val = mat(i, j);
            if (val != 0) {
                indices.push_back(j);
                data.push_back(val);
            }
        }
        indptr[i + 1] = static_cast<int>(indices.size());
    }
    return csr;
}

template<>
CSRMatrix matrix_to_csr<int16_t>(const Matrix<int16_t>& mat) {
    CSRMatrix csr(mat.rows(), mat.cols());
    auto& indptr = csr.indptr_;
    auto& indices = csr.indices_;
    auto& data = csr.data_;
    
    for (int i = 0; i < mat.rows(); ++i) {
        for (int j = 0; j < mat.cols(); ++j) {
            int16_t val = mat(i, j);
            if (val != 0) {
                indices.push_back(j);
                data.push_back(val);
            }
        }
        indptr[i + 1] = static_cast<int>(indices.size());
    }
    return csr;
}

// Special function for dense index matrices that need to store all values including zeros
CSRMatrix matrix_to_csr_dense(const Matrix<int16_t>& mat) {
    CSRMatrix csr(mat.rows(), mat.cols());
    auto& indptr = csr.indptr_;
    auto& indices = csr.indices_;
    auto& data = csr.data_;
    
    for (int i = 0; i < mat.rows(); ++i) {
        for (int j = 0; j < mat.cols(); ++j) {
            int16_t val = mat(i, j);
            indices.push_back(j);
            data.push_back(val);
        }
        indptr[i + 1] = static_cast<int>(indices.size());
    }
    return csr;
}

// Matrix generation functions
MatrixParams get_matrix_params(const Matrix<int8_t>& A, const Matrix<int8_t>& B) {
    auto [M, K] = A.shape();
    auto [K2, N] = B.shape();
    if (K != K2) {
        throw std::runtime_error("Matrix dimensions mismatch");
    }
    
    double density_a = static_cast<double>(A.nnz()) / (M * K);
    double density_b = static_cast<double>(B.nnz()) / (K * N);
    Matrix<int8_t> C = A * B;
    double c_density = static_cast<double>(C.nnz()) / (M * N);
    
    return MatrixParams{
        M, K, N,
        density_a, density_b, c_density,
        16, 16,  // prow, pcol
        0.0, 0.0, // m_variance, k_variance
        true, // sparsity_aware
        std::nullopt // random_state
    };
}

std::tuple<Matrix<int8_t>, Matrix<int8_t>, Matrix<int32_t>> 
gen_uniform_matrix(const MatrixParams& params) {
    if (params.M <= 0 || params.N <= 0 || params.K <= 0) {
        throw std::runtime_error("Matrix dimensions must be positive");
    }
    
    std::mt19937 gen(params.random_state.value_or(0));
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    
    Matrix<int8_t> A(params.M, params.K);
    Matrix<int8_t> B(params.K, params.N);
    
    for (int i = 0; i < params.M; ++i) {
        for (int j = 0; j < params.K; ++j) {
            A(i, j) = (dis(gen) < params.density_a) ? 1 : 0;
        }
    }
    
    for (int i = 0; i < params.K; ++i) {
        for (int j = 0; j < params.N; ++j) {
            B(i, j) = (dis(gen) < params.density_b) ? 1 : 0;
        }
    }
    
    CSRMatrix A_csr = matrix_to_csr(A);
    CSRMatrix B_csr = matrix_to_csr(B);
    Matrix<int32_t> C = sparse_multiply(A_csr, B_csr);
    return std::make_tuple(A, B, C);
}

std::tuple<Matrix<int8_t>, Matrix<int8_t>, Matrix<int32_t>>
gen_rand_matrix(const MatrixParams& params) {
    // Simplified version - full implementation would be more complex
    // For now, use uniform generation
    return gen_uniform_matrix(params);
}

CSRMatrix gen_diag_n_matrix(int size, int n) {
    Matrix<int8_t> mat(size, size);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < size - i; ++j) {
            mat(j, j + i) = 1;
        }
    }
    return matrix_to_csr(mat);
}

Matrix<int8_t> gen_dense_matrix(int size) {
    Matrix<int8_t> mat(size, size, 1);
    return mat;
}

// Template specializations for Matrix::to_csr()
template<>
CSRMatrix Matrix<int8_t>::to_csr() const {
    return matrix_to_csr<int8_t>(*this);
}

template<>
CSRMatrix Matrix<int16_t>::to_csr() const {
    return matrix_to_csr<int16_t>(*this);
}

Matrix<int8_t> load_mtx_matrix(const std::string& filepath, bool binary) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filepath);
    }
    
    std::string line;
    int rows = 0, cols = 0, nnz = 0;
    bool is_pattern = false;
    bool is_symmetric = false;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        if (line.find("%%MatrixMarket") == 0) {
            if (line.find("pattern") != std::string::npos) {
                is_pattern = true;
            }
            if (line.find("symmetric") != std::string::npos) {
                is_symmetric = true;
            }
            continue;
        }
        
        if (line[0] == '%') continue;
        
        std::istringstream iss(line);
        if (!(iss >> rows >> cols >> nnz)) {
            throw std::runtime_error("Invalid size line in .mtx file: " + line);
        }
        break;
    }
    
    if (rows <= 0 || cols <= 0) {
        throw std::runtime_error("Invalid matrix dimensions in .mtx file");
    }
    
    Matrix<int8_t> mat(rows, cols, 0);
    
    int entry_count = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '%') continue;
        
        std::istringstream iss(line);
        int row, col;
        double value = 1.0;
        
        if (!(iss >> row >> col)) {
            throw std::runtime_error("Invalid data line in .mtx file: " + line);
        }
        
        row--;
        col--;
        
        if (!is_pattern && !(iss >> value)) {
            value = 1.0;
        }
        
        if (row < 0 || row >= rows || col < 0 || col >= cols) {
            throw std::runtime_error("Index out of bounds in .mtx file: row=" + 
                                    std::to_string(row) + ", col=" + std::to_string(col));
        }
        
        if (binary) {
            mat(row, col) = (value != 0.0) ? 1 : 0;
        } else {
            int8_t val = static_cast<int8_t>(std::max(-128.0, std::min(127.0, value)));
            mat(row, col) = val;
        }
        
        if (is_symmetric && row != col) {
            if (binary) {
                mat(col, row) = (value != 0.0) ? 1 : 0;
            } else {
                int8_t val = static_cast<int8_t>(std::max(-128.0, std::min(127.0, value)));
                mat(col, row) = val;
            }
        }
        
        entry_count++;
        if (entry_count >= nnz) break;
    }
    
    return mat;
}

CSRMatrix load_mtx_to_csr(const std::string& filepath, bool binary) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filepath);
    }

    std::string line;
    int rows = 0, cols = 0, nnz = 0;
    bool is_pattern = false;
    bool is_symmetric = false;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        if (line.find("%%MatrixMarket") == 0) {
            if (line.find("pattern") != std::string::npos) {
                is_pattern = true;
            }
            if (line.find("symmetric") != std::string::npos) {
                is_symmetric = true;
            }
            continue;
        }

        if (line[0] == '%') continue;

        std::istringstream iss(line);
        if (!(iss >> rows >> cols >> nnz)) {
            throw std::runtime_error("Invalid size line in .mtx file: " + line);
        }
        break;
    }

    if (rows <= 0 || cols <= 0) {
        throw std::runtime_error("Invalid matrix dimensions in .mtx file");
    }

    // Collect all (row, col, val) triplets first
    struct Triplet { int row; int col; int val; };
    std::vector<Triplet> triplets;
    triplets.reserve(is_symmetric ? nnz * 2 : nnz);

    int entry_count = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '%') continue;

        std::istringstream iss(line);
        int row, col;
        double value = 1.0;

        if (!(iss >> row >> col)) {
            throw std::runtime_error("Invalid data line in .mtx file: " + line);
        }

        row--;
        col--;

        if (!is_pattern && !(iss >> value)) {
            value = 1.0;
        }

        if (row < 0 || row >= rows || col < 0 || col >= cols) {
            throw std::runtime_error("Index out of bounds in .mtx file: row=" +
                                    std::to_string(row) + ", col=" + std::to_string(col));
        }

        int val = binary ? ((value != 0.0) ? 1 : 0) :
                  static_cast<int>(std::max(-128.0, std::min(127.0, value)));

        if (val != 0) {
            triplets.push_back({row, col, val});
            if (is_symmetric && row != col) {
                triplets.push_back({col, row, val});
            }
        }

        entry_count++;
        if (entry_count >= nnz) break;
    }

    // Sort by row then column
    std::sort(triplets.begin(), triplets.end(), [](const Triplet& a, const Triplet& b) {
        return a.row < b.row || (a.row == b.row && a.col < b.col);
    });

    // Build CSR from sorted triplets
    CSRMatrix csr(rows, cols);
    csr.indices_.reserve(triplets.size());
    csr.data_.reserve(triplets.size());

    for (const auto& t : triplets) {
        csr.indices_.push_back(t.col);
        csr.data_.push_back(t.val);
    }

    // Build indptr
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        csr.indptr_[r] = idx;
        while (idx < static_cast<int>(triplets.size()) && triplets[idx].row == r) {
            idx++;
        }
    }
    csr.indptr_[rows] = static_cast<int>(triplets.size());

    return csr;
}

Matrix<int8_t> csr_to_dense_matrix(const CSRMatrix& csr) {
    Matrix<int8_t> mat(csr.rows_, csr.cols_, 0);
    for (int i = 0; i < csr.rows_; ++i) {
        int start = csr.indptr_[i];
        int end = csr.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            mat(i, csr.indices_[idx]) = static_cast<int8_t>(csr.data_[idx]);
        }
    }
    return mat;
}

void save_mtx_matrix(const Matrix<int8_t>& mat, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + filepath);
    }
    
    auto [rows, cols] = mat.shape();
    int nnz = mat.nnz();
    
    file << "%%MatrixMarket matrix coordinate integer general\n";
    file << "% Generated matrix C = A * B\n";
    file << rows << " " << cols << " " << nnz << "\n";
    
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            int8_t val = mat(i, j);
            if (val != 0) {
                file << (i + 1) << " " << (j + 1) << " " << static_cast<int>(val) << "\n";
            }
        }
    }
    
    file.close();
}

Matrix<int32_t> sparse_multiply(const CSRMatrix& A_csr, const CSRMatrix& B_csr) {
    auto [A_rows, A_cols] = A_csr.shape();
    auto [B_rows, B_cols] = B_csr.shape();

    if (A_cols != B_rows) {
        throw std::runtime_error("Matrix dimensions mismatch for sparse multiplication");
    }

    Matrix<int32_t> result(A_rows, B_cols, 0);

    for (int i = 0; i < A_rows; ++i) {
        auto A_row = A_csr.get_row(i);

        for (const auto& [k, A_val] : A_row) {
            auto B_row = B_csr.get_row(k);

            for (const auto& [j, B_val] : B_row) {
                result(i, j) += static_cast<int32_t>(A_val) * static_cast<int32_t>(B_val);
            }
        }
    }

    return result;
}

CSRMatrix sparse_multiply_csr(const CSRMatrix& A_csr, const CSRMatrix& B_csr) {
    auto [A_rows, A_cols] = A_csr.shape();
    auto [B_rows, B_cols] = B_csr.shape();

    if (A_cols != B_rows) {
        throw std::runtime_error("Matrix dimensions mismatch for sparse multiplication");
    }

    CSRMatrix result(A_rows, B_cols);
    result.indptr_[0] = 0;

    std::vector<int32_t> row_accumulator(B_cols, 0);
    std::vector<bool> col_used(B_cols, false);

    for (int i = 0; i < A_rows; ++i) {
        std::vector<int> used_cols;
        auto A_row = A_csr.get_row(i);

        for (const auto& [k, A_val] : A_row) {
            auto B_row = B_csr.get_row(k);
            for (const auto& [j, B_val] : B_row) {
                int32_t product = static_cast<int32_t>(A_val) * static_cast<int32_t>(B_val);
                if (!col_used[j]) {
                    col_used[j] = true;
                    used_cols.push_back(j);
                }
                row_accumulator[j] += product;
            }
        }

        std::sort(used_cols.begin(), used_cols.end());
        for (int j : used_cols) {
            if (row_accumulator[j] != 0) {
                result.indices_.push_back(j);
                result.data_.push_back(row_accumulator[j]);
            }
            row_accumulator[j] = 0;
            col_used[j] = false;
        }
        result.indptr_[i + 1] = static_cast<int>(result.indices_.size());
    }

    return result;
}

} // namespace csegfold

