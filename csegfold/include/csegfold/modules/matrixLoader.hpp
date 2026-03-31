#pragma once

#include "csegfold/modules/module.hpp"
#include "csegfold/modules/mapper.hpp"
#include "csegfold/matrix/generator.hpp"
#include <unordered_map>
#include <vector>
#include <optional>
#include <tuple>

namespace csegfold {

// Forward declaration
class MemoryController;

enum class MatrixOrder {
    ROW_MAJOR,
    COL_MAJOR
};

class MatrixLoader : public BaseModule {
public:
    MatrixLoader(const Matrix<int8_t>& A, const Matrix<int8_t>& B);
    
    int M, N, K;
    Matrix<int8_t> A;
    Matrix<int8_t> B;
    Matrix<int32_t> C;  // Use int32_t to avoid overflow in tiling calculations
    Matrix<int8_t> A_orig;  // Store original A before tiling
    Matrix<int8_t> B_orig;  // Store original B before tiling
    std::optional<Matrix<int8_t>> denseA;
    std::optional<Matrix<int8_t>> denseB;
    std::optional<Matrix<int8_t>> denseC;
    
    int physical_pe_row_num;
    int physical_pe_col_num;
    int group_size;
    int v_cols = 0;  // Maximum number of non-zeros per row across all tiles
    
    std::unordered_map<int, std::vector<std::pair<int, int>>> B_csr;
    std::unordered_map<int, std::vector<bool>> B_csr_load;
    std::unordered_map<int, std::vector<std::pair<int, int>>> C_csr;
    
    int denseM;
    
    void init_indices();
    void generate_offsets();
    void ideal_data_transfer();
    void group_tiling(int group_size = 1);
    void dense_tiling();
    void process_tiles();  // Process tiles and transform matrices
    void init_k_to_tile_id();
    void tile_data_transfer();
    void decompose_a_row();
    void check_indices();
    bool check_size() const;
    int get_vcols();  // Calculate maximum non-zeros per row across all tiles
    
    std::vector<int> B_rows_to_load;
    
    // Indexed CSR matrices store values + original coordinates
    IndexedCSRMatrix A_indexed;  // Replaces A + row_idx_a + col_idx_a
    IndexedCSRMatrix B_indexed;  // Replaces B + row_idx_b + col_idx_b
    
    // CSR format matrices (converted once during initialization)
    CSRMatrix A_orig_csr, B_orig_csr;
    
    // Offset matrices
    Matrix<int> A_nnz_offset, B_nnz_offset, C_nnz_offset;
    
    // Tile information
    std::vector<std::tuple<int, int, int, int>> tiles;  // (m_start, m_end, n_start, n_end)
    std::unordered_map<int, int> k_to_tile_id;
    std::unordered_map<std::pair<int, int>, int, Mapper::PairHash> tile_map;
    
    // Helper methods
    std::pair<int, int> get_original_index_a(int pe_row, int pe_col) const;
    std::pair<int, int> get_original_index_b(int tiled_b_row, int tiled_b_col) const;
    std::tuple<int, int, int> data_transfer(const Matrix<int8_t>& A, const Matrix<int8_t>& B, const Matrix<int32_t>& C) const;
    int num_pes() const { return physical_pe_row_num * physical_pe_col_num; }
    bool intersect_bc(int b_row, int c_row) const;
    bool b_is_same_block(int k1, int k2) const;
    
private:
    void _create_A_bitmask();
    bool _get_bit_from_packed(int row, int col) const;
    
    // Helper method for generating offsets
    template<typename T>
    void _generate_offset_for_matrix(
        const Matrix<T>& matrix,
        Matrix<int>& offset_matrix,
        MatrixOrder order
    );
    
    // Helper methods for group_tiling
    Matrix<int8_t> _get_matrix_slice(const Matrix<int8_t>& mat, int row_start, int row_end) const;
    std::vector<int> _greedy_tile(const Matrix<int32_t>& C_block, int target_nnz) const;
    
    // Helper structures for process_tiles
    struct TileDimensions {
        int max_A_rows;
        int total_A_cols;
        int total_B_rows;
        int max_B_cols;
    };
    
    struct TileNNZCounts {
        int total_rA_nnz;
        int total_cA_nnz;
        int total_rB_nnz;
        int total_cB_nnz;
        int total_A_csr_nnz;
        int total_B_csr_nnz;
    };
    
    // Helper methods for process_tiles
    TileDimensions _compute_tile_dimensions() const;
    TileNNZCounts _compute_tile_nnz_counts() const;
    void _build_A_indexed_from_tiles(IndexedCSRMatrix& A_indexed_out);
    void _build_B_indexed_from_tiles(IndexedCSRMatrix& B_indexed_out);
    void _build_tile_map();
    
    std::vector<std::vector<uint8_t>> A_bitmask;
    std::pair<int, int> A_bitmask_shape;
};

} // namespace csegfold

