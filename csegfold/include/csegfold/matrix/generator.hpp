#pragma once

#include <vector>
#include <cstdint>
#include <random>
#include <string>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <indicators/progress_bar.hpp>

namespace csegfold {

// MatrixParams struct
struct MatrixParams {
    int M;
    int K;
    int N;
    double density_a;
    double density_b;
    double c_density;
    int prow;
    int pcol;
    double m_variance;
    double k_variance;
    bool sparsity_aware = true;
    std::optional<int> random_state;
};

// Forward declaration
template<typename T>
class Matrix;

// Forward declaration of CSRMatrix
class CSRMatrix;

// Forward declaration for helper function
template<typename T>
CSRMatrix matrix_to_csr(const Matrix<T>& mat);

// Special function for dense index matrices (stores all values including zeros)
CSRMatrix matrix_to_csr_dense(const Matrix<int16_t>& mat);

// CSRMatrix class for sparse matrices
class CSRMatrix {
public:
    CSRMatrix() = default;
    CSRMatrix(int rows, int cols);
    
    // Get shape
    std::pair<int, int> shape() const { return {rows_, cols_}; }
    int nnz() const;
    
    // Access elements
    int get(int row, int col) const;
    
    // Get row data (CSR format)
    std::vector<std::pair<int, int>> get_row(int row) const; // Returns (col, val) pairs

    // Friend function for conversion
    template<typename T>
    friend CSRMatrix matrix_to_csr(const Matrix<T>& mat);
    
    // Internal data (made public for helper functions)
    int rows_ = 0;
    int cols_ = 0;
    std::vector<int> indptr_;  // Row pointers
    std::vector<int> indices_; // Column indices
    std::vector<int> data_;    // Values
};

// IndexedCSRMatrix: Extended CSR that stores original coordinates for each non-zero
// More memory efficient than storing 3 separate matrices
class IndexedCSRMatrix {
public:
    IndexedCSRMatrix() = default;
    IndexedCSRMatrix(int rows, int cols);
    
    // Get shape
    std::pair<int, int> shape() const { return {rows_, cols_}; }
    int nnz() const { return static_cast<int>(data_.size()); }
    
    // Access elements (returns value and original coordinates)
    std::tuple<int8_t, int16_t, int16_t> get(int row, int col) const;
    
    // Get original coordinates for a non-zero at (row, col)
    std::pair<int, int> get_original_coords(int row, int col) const;
    
    // Reserve space for efficiency
    void reserve(int n) {
        indices_.reserve(n);
        data_.reserve(n);
        orig_row_.reserve(n);
        orig_col_.reserve(n);
    }
    
    // Internal data (made public for direct access)
    int rows_ = 0;
    int cols_ = 0;
    std::vector<int> indptr_;      // Row pointers (rows_ + 1 elements)
    std::vector<int> indices_;     // Column indices in tiled space
    std::vector<int8_t> data_;     // Actual values
    std::vector<int16_t> orig_row_; // Original row index (parallel to data_)
    std::vector<int16_t> orig_col_; // Original column index (parallel to data_)
};

// Matrix template class for dense matrices
template<typename T>
class Matrix {
public:
    Matrix() = default;
    Matrix(int rows, int cols, T init_val = T{0});
    Matrix(const std::vector<std::vector<T>>& data);
    
    // Shape
    std::pair<int, int> shape() const { return {rows_, cols_}; }
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    
    // Access
    T& operator()(int row, int col);
    const T& operator()(int row, int col) const;
    
    // Count non-zeros
    int nnz() const;
    
    // Matrix multiplication
    Matrix<T> operator*(const Matrix<T>& other) const;
    
    // Convert to CSR - only for int8_t and int16_t
    CSRMatrix to_csr() const;
    
    // Get row/column
    std::vector<T> get_row(int row) const;
    std::vector<T> get_col(int col) const;

private:
    int rows_ = 0;
    int cols_ = 0;
    std::vector<std::vector<T>> data_;
};

// Matrix generation functions
MatrixParams get_matrix_params(const Matrix<int8_t>& A, const Matrix<int8_t>& B);

std::tuple<Matrix<int8_t>, Matrix<int8_t>, Matrix<int32_t>>
gen_uniform_matrix(const MatrixParams& params);

std::tuple<Matrix<int8_t>, Matrix<int8_t>, Matrix<int32_t>>
gen_rand_matrix(const MatrixParams& params);

CSRMatrix gen_diag_n_matrix(int size = 10, int n = 1);

Matrix<int8_t> gen_dense_matrix(int size = 10);

Matrix<int8_t> load_mtx_matrix(const std::string& filepath, bool binary = true);
CSRMatrix load_mtx_to_csr(const std::string& filepath, bool binary = true);
Matrix<int8_t> csr_to_dense_matrix(const CSRMatrix& csr);
void save_mtx_matrix(const Matrix<int8_t>& mat, const std::string& filepath);
Matrix<int32_t> sparse_multiply(const CSRMatrix& A_csr, const CSRMatrix& B_csr);

// Implementation of template methods
template<typename T>
Matrix<T>::Matrix(int rows, int cols, T init_val)
    : rows_(rows), cols_(cols), data_(rows, std::vector<T>(cols, init_val)) {
}

template<typename T>
Matrix<T>::Matrix(const std::vector<std::vector<T>>& data)
    : rows_(static_cast<int>(data.size())), 
      cols_(data.empty() ? 0 : static_cast<int>(data[0].size())),
      data_(data) {
}

template<typename T>
T& Matrix<T>::operator()(int row, int col) {
    return data_[row][col];
}

template<typename T>
const T& Matrix<T>::operator()(int row, int col) const {
    return data_[row][col];
}

template<typename T>
int Matrix<T>::nnz() const {
    int count = 0;
    for (const auto& row : data_) {
        for (const auto& val : row) {
            if (val != T{0}) count++;
        }
    }
    return count;
}

template<typename T>
Matrix<T> Matrix<T>::operator*(const Matrix<T>& other) const {
    if (cols_ != other.rows_) {
        throw std::runtime_error("Matrix dimensions mismatch for multiplication");
    }
    
    // Show progress bar for large matrices (threshold: 1000 rows)
    bool show_progress = rows_ > 1000;
    std::optional<indicators::ProgressBar> pbar;
    
    if (show_progress) {
        pbar.emplace(
            indicators::option::BarWidth{50},
            indicators::option::Start{"["},
            indicators::option::Fill{"="},
            indicators::option::Lead{">"},
            indicators::option::Remainder{" "},
            indicators::option::End{"]"},
            indicators::option::PostfixText{"Matrix multiplication"},
            indicators::option::ForegroundColor{indicators::Color::green},
            indicators::option::ShowPercentage{true},
            indicators::option::ShowElapsedTime{true},
            indicators::option::ShowRemainingTime{true},
            indicators::option::MaxProgress{rows_}
        );
    }
    
    Matrix<T> result(rows_, other.cols_);
    for (int i = 0; i < rows_; ++i) {
        for (int j = 0; j < other.cols_; ++j) {
            int64_t sum = 0;  // Use int64_t to avoid overflow during accumulation
            for (int k = 0; k < cols_; ++k) {
                T a_val = data_[i][k];
                T b_val = other.data_[k][j];
                if (a_val != T{0} && b_val != T{0}) {
                    sum += static_cast<int64_t>(a_val) * static_cast<int64_t>(b_val);
                }
            }
            result(i, j) = static_cast<T>(sum);  // May truncate for small T types
        }
        if (pbar.has_value()) {
            pbar->set_progress(i + 1);
        }
    }
    if (pbar.has_value()) {
        pbar->mark_as_completed();
    }
    return result;
}


template<typename T>
std::vector<T> Matrix<T>::get_row(int row) const {
    return data_[row];
}

template<typename T>
std::vector<T> Matrix<T>::get_col(int col) const {
    std::vector<T> result(rows_);
    for (int i = 0; i < rows_; ++i) {
        result[i] = data_[i][col];
    }
    return result;
}

} // namespace csegfold

