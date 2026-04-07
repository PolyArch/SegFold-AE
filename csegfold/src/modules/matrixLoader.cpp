#include "csegfold/modules/matrixLoader.hpp"
#include "csegfold/matrix/generator.hpp"
#include <cassert>
#include <algorithm>
#include <climits>

namespace csegfold {

MatrixLoader::MatrixLoader(const Matrix<int8_t>& A, const Matrix<int8_t>& B) : BaseModule() {
    this->A = A;
    this->B = B;
    auto [M, K] = A.shape();
    auto [K2, N] = B.shape();
    assert(K == K2);

    this->M = M;
    this->N = N;
    this->K = K;

    if (verbose()) {
        log->info("MatrixLoader: Initializing with A(" + std::to_string(M) + "x" + std::to_string(K) +
                 ", nnz=" + std::to_string(A.nnz()) + "), B(" + std::to_string(K) + "x" + std::to_string(N) +
                 ", nnz=" + std::to_string(B.nnz()) + ")");
    }

    double A_density = static_cast<double>(A.nnz()) / (M * K);
    double B_density = static_cast<double>(B.nnz()) / (K * N);

    if (verbose()) {
        log->info("MatrixLoader: Converting matrices to CSR format...");
    }

    A_orig_csr = A.to_csr();
    B_orig_csr = B.to_csr();
    if (verbose()) {
        log->info("MatrixLoader: Finished CSR conversion");
    }

    if (verbose()) {
        log->info("MatrixLoader: Computing C = A * B (sparse)...");
    }

    if (verbose()) {
        log->info("Computing C using sparse multiplication (A density: " +
                 std::to_string(A_density) + ", B density: " +
                 std::to_string(B_density) + ")");
    }
    this->C_csr_result = sparse_multiply_csr(A_orig_csr, B_orig_csr);

    if (verbose()) {
        log->info("MatrixLoader: Finished generating C (" + std::to_string(C_csr_result.rows_) + "x" +
                 std::to_string(C_csr_result.cols_) + ", nnz=" + std::to_string(C_csr_result.nnz()) + ")");
    }
    
    physical_pe_row_num = prows();
    physical_pe_col_num = pcols();
    group_size = 1;
    
    if (verbose()) {
        log->info("MatrixLoader: Initializing indices...");
    }
    init_indices();
    if (verbose()) {
        log->info("MatrixLoader: Finished initializing indices");
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Generating offsets...");
    }
    generate_offsets();
    if (verbose()) {
        log->info("MatrixLoader: Finished generating offsets");
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Computing ideal data transfer...");
    }
    ideal_data_transfer();
    if (verbose()) {
        log->info("MatrixLoader: Finished ideal data transfer");
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Starting tiling (" + std::string(cfg.enable_dynamic_tiling ? "group" : "dense") + ")...");
    }
    if (cfg.enable_dynamic_tiling) {
        group_tiling(group_size);
    } else {
        dense_tiling();
    }
    if (verbose()) {
        log->info("MatrixLoader: Finished tiling (" + std::to_string(tiles.size()) + " tiles)");
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Initializing K to tile ID mapping...");
    }
    init_k_to_tile_id();
    if (verbose()) {
        log->info("MatrixLoader: Finished K to tile ID mapping");
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Computing tile data transfer...");
    }
    tile_data_transfer();
    if (verbose()) {
        log->info("MatrixLoader: Finished tile data transfer");
    }
    
    if (cfg.enable_decompose_a_row) {
        if (verbose()) {
            log->info("MatrixLoader: Decomposing A rows...");
        }
        decompose_a_row();
        if (verbose()) {
            log->info("MatrixLoader: Finished decomposing A rows");
        }
    }
    
    if (verbose()) {
        log->info("MatrixLoader: Building CSR formats from IndexedCSRMatrix...");
    }
    B_csr = std::unordered_map<int, std::vector<std::pair<int, int>>>();
    for (int i = 0; i < B_indexed.rows_; ++i) {
        std::vector<std::pair<int, int>> row_data;
        int start = B_indexed.indptr_[i];
        int end = B_indexed.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int col = B_indexed.indices_[idx];
            int8_t val = B_indexed.data_[idx];
            if (val != 0) {
                row_data.push_back({col, val});
            }
        }
        if (!row_data.empty()) {
            B_csr[i] = row_data;
        }
    }
    if (verbose()) {
        log->info("MatrixLoader: B_csr built from B_indexed: " + std::to_string(B_csr.size()) +
                 " rows, nnz=" + std::to_string(B_indexed.nnz()));
    }

    C_csr = std::unordered_map<int, std::vector<std::pair<int, int>>>();
    for (int i = 0; i < C_csr_result.rows_; ++i) {
        auto row_data = C_csr_result.get_row(i);
        if (!row_data.empty()) {
            C_csr[i] = row_data;
        }
    }
    if (verbose()) {
        log->info("MatrixLoader: C_csr built: " + std::to_string(C_csr.size()) + " rows");
    }
    
    cfg.virtual_pe_row_num = prows();
    if (cfg.enable_spatial_folding) {
        cfg.virtual_pe_col_num = std::max(v_cols, pcols());
    } else {
        cfg.virtual_pe_col_num = pcols();
    }
    
    denseM = M;
    check_indices();
    
    if (verbose()) {
        log->info("MatrixLoader: Creating A bitmask...");
    }
    _create_A_bitmask();
    if (verbose()) {
        log->info("MatrixLoader: Finished creating A bitmask");
        log->info("MatrixLoader: Initialization complete!");
    }
}

MatrixLoader::MatrixLoader(const CSRMatrix& A_csr, const CSRMatrix& B_csr) : BaseModule() {
    // CSR-only constructor: no dense matrices allocated
    assert(A_csr.cols_ == B_csr.rows_);

    A_orig_csr = A_csr;
    B_orig_csr = B_csr;

    M = A_csr.rows_;
    K = A_csr.cols_;
    N = B_csr.cols_;

    if (verbose()) {
        log->info("MatrixLoader(CSR): Initializing with A_csr(" + std::to_string(M) + "x" + std::to_string(K) +
                 ", nnz=" + std::to_string(A_csr.nnz()) + "), B_csr(" + std::to_string(K) + "x" + std::to_string(N) +
                 ", nnz=" + std::to_string(B_csr.nnz()) + ")");
    }

    // Compute C = A * B (sparse)
    this->C_csr_result = sparse_multiply_csr(A_orig_csr, B_orig_csr);
    if (verbose()) {
        log->info("MatrixLoader(CSR): Finished generating C (" + std::to_string(C_csr_result.rows_) + "x" +
                 std::to_string(C_csr_result.cols_) + ", nnz=" + std::to_string(C_csr_result.nnz()) + ")");
    }

    physical_pe_row_num = prows();
    physical_pe_col_num = pcols();
    group_size = 1;

    init_indices();
    generate_offsets();
    ideal_data_transfer();

    if (cfg.enable_dynamic_tiling) {
        group_tiling(group_size);
    } else {
        dense_tiling();
    }
    if (verbose()) {
        log->info("MatrixLoader(CSR): Finished tiling (" + std::to_string(tiles.size()) + " tiles)");
    }

    init_k_to_tile_id();
    tile_data_transfer();

    if (cfg.enable_decompose_a_row) {
        decompose_a_row();
    }

    // Build B_csr map from B_indexed
    this->B_csr = std::unordered_map<int, std::vector<std::pair<int, int>>>();
    for (int i = 0; i < B_indexed.rows_; ++i) {
        std::vector<std::pair<int, int>> row_data;
        int start = B_indexed.indptr_[i];
        int end = B_indexed.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int col = B_indexed.indices_[idx];
            int8_t val = B_indexed.data_[idx];
            if (val != 0) {
                row_data.push_back({col, val});
            }
        }
        if (!row_data.empty()) {
            this->B_csr[i] = row_data;
        }
    }

    // Build C_csr map
    C_csr = std::unordered_map<int, std::vector<std::pair<int, int>>>();
    for (int i = 0; i < C_csr_result.rows_; ++i) {
        auto row_data = C_csr_result.get_row(i);
        if (!row_data.empty()) {
            C_csr[i] = row_data;
        }
    }

    cfg.virtual_pe_row_num = prows();
    if (cfg.enable_spatial_folding) {
        cfg.virtual_pe_col_num = std::max(v_cols, pcols());
    } else {
        cfg.virtual_pe_col_num = pcols();
    }

    denseM = M;
    check_indices();
    _create_A_bitmask();

    if (verbose()) {
        log->info("MatrixLoader(CSR): Initialization complete!");
    }
}

void MatrixLoader::init_indices() {
    // Create IndexedCSRMatrix from original matrices A and B
    // Only stores data for non-zero elements
    bool use_csr = (A.rows() == 0);  // Dense A not available (CSR constructor path)

    if (use_csr) {
        // Build IndexedCSRMatrix directly from CSR
        A_indexed = IndexedCSRMatrix(A_orig_csr.rows_, A_orig_csr.cols_);
        A_indexed.indptr_[0] = 0;
        for (int i = 0; i < A_orig_csr.rows_; ++i) {
            int start = A_orig_csr.indptr_[i];
            int end = A_orig_csr.indptr_[i + 1];
            for (int idx = start; idx < end; ++idx) {
                A_indexed.indices_.push_back(A_orig_csr.indices_[idx]);
                A_indexed.data_.push_back(static_cast<int8_t>(A_orig_csr.data_[idx]));
                A_indexed.orig_row_.push_back(static_cast<int32_t>(i));
                A_indexed.orig_col_.push_back(static_cast<int32_t>(A_orig_csr.indices_[idx]));
            }
            A_indexed.indptr_[i + 1] = static_cast<int>(A_indexed.indices_.size());
        }
        // Same for B
        B_indexed = IndexedCSRMatrix(B_orig_csr.rows_, B_orig_csr.cols_);
        B_indexed.indptr_[0] = 0;
        for (int i = 0; i < B_orig_csr.rows_; ++i) {
            int start = B_orig_csr.indptr_[i];
            int end = B_orig_csr.indptr_[i + 1];
            for (int idx = start; idx < end; ++idx) {
                B_indexed.indices_.push_back(B_orig_csr.indices_[idx]);
                B_indexed.data_.push_back(static_cast<int8_t>(B_orig_csr.data_[idx]));
                B_indexed.orig_row_.push_back(static_cast<int32_t>(i));
                B_indexed.orig_col_.push_back(static_cast<int32_t>(B_orig_csr.indices_[idx]));
            }
            B_indexed.indptr_[i + 1] = static_cast<int>(B_indexed.indices_.size());
        }
    } else {
        // Dense path: iterate dense matrix (existing behavior)
        int rows_a = A.rows();
        int cols_a = A.cols();
        int rows_b = B.rows();
        int cols_b = B.cols();

        A_indexed = IndexedCSRMatrix(rows_a, cols_a);
        A_indexed.indptr_[0] = 0;

        for (int i = 0; i < rows_a; ++i) {
            for (int j = 0; j < cols_a; ++j) {
                int8_t val = A(i, j);
                if (val != 0) {
                    A_indexed.indices_.push_back(j);
                    A_indexed.data_.push_back(val);
                    A_indexed.orig_row_.push_back(static_cast<int32_t>(i));
                    A_indexed.orig_col_.push_back(static_cast<int32_t>(j));
                }
            }
            A_indexed.indptr_[i + 1] = static_cast<int>(A_indexed.indices_.size());
        }

        B_indexed = IndexedCSRMatrix(rows_b, cols_b);
        B_indexed.indptr_[0] = 0;

        for (int i = 0; i < rows_b; ++i) {
            for (int j = 0; j < cols_b; ++j) {
                int8_t val = B(i, j);
                if (val != 0) {
                    B_indexed.indices_.push_back(j);
                    B_indexed.data_.push_back(val);
                    B_indexed.orig_row_.push_back(static_cast<int32_t>(i));
                    B_indexed.orig_col_.push_back(static_cast<int32_t>(j));
                }
            }
            B_indexed.indptr_[i + 1] = static_cast<int>(B_indexed.indices_.size());
        }
    }

    if (verbose()) {
        log->info("MatrixLoader: Created A_indexed (nnz=" + std::to_string(A_indexed.nnz()) +
                 ") and B_indexed (nnz=" + std::to_string(B_indexed.nnz()) + ")");
    }
}

template<typename T>
void MatrixLoader::_generate_offset_for_matrix(
    const Matrix<T>& matrix,
    Matrix<int>& offset_matrix,
    MatrixOrder order
) {
    int count = 0;
    
    if (order == MatrixOrder::ROW_MAJOR) {
        for (int i = 0; i < matrix.rows(); ++i) {
            for (int j = 0; j < matrix.cols(); ++j) {
                if (matrix(i, j) != 0) {
                    count++;
                    offset_matrix(i, j) = count;
                }
            }
        }
    } else {
        for (int j = 0; j < matrix.cols(); ++j) {
            for (int i = 0; i < matrix.rows(); ++i) {
                if (matrix(i, j) != 0) {
                    count++;
                    offset_matrix(i, j) = count;
                }
            }
        }
    }
}

void MatrixLoader::generate_offsets() {
    // A and B offsets now use sparse maps
    A_nnz_offset.clear();
    B_nnz_offset.clear();
    A_nnz_offset_cols_ = A_indexed.cols_;
    B_nnz_offset_cols_ = B_indexed.cols_;

    // Generate A offset from A_indexed CSR
    {
        int count = 0;
        if (cfg.enable_a_csc) {
            // Column-major order: sort entries by (col, row)
            // Avoid allocating vector of size A_indexed.cols_ which can be huge
            struct ColEntry { int col; int row; int data_idx; };
            std::vector<ColEntry> entries;
            entries.reserve(A_indexed.nnz());
            for (int i = 0; i < A_indexed.rows_; ++i) {
                int start = A_indexed.indptr_[i];
                int end = A_indexed.indptr_[i + 1];
                for (int idx = start; idx < end; ++idx) {
                    entries.push_back({A_indexed.indices_[idx], i, idx});
                }
            }
            std::sort(entries.begin(), entries.end(), [](const ColEntry& a, const ColEntry& b) {
                return a.col < b.col || (a.col == b.col && a.row < b.row);
            });
            for (const auto& e : entries) {
                if (A_indexed.data_[e.data_idx] != 0) {
                    count++;
                    int64_t key = static_cast<int64_t>(e.row) * A_indexed.cols_ + e.col;
                    A_nnz_offset[key] = count;
                }
            }
        } else {
            // Row-major order
            for (int i = 0; i < A_indexed.rows_; ++i) {
                int start = A_indexed.indptr_[i];
                int end = A_indexed.indptr_[i + 1];
                for (int idx = start; idx < end; ++idx) {
                    if (A_indexed.data_[idx] != 0) {
                        count++;
                        int64_t key = static_cast<int64_t>(i) * A_indexed.cols_ + A_indexed.indices_[idx];
                        A_nnz_offset[key] = count;
                    }
                }
            }
        }
    }

    // Generate B offset (always row-major)
    {
        int count = 0;
        for (int i = 0; i < B_indexed.rows_; ++i) {
            int start = B_indexed.indptr_[i];
            int end = B_indexed.indptr_[i + 1];
            for (int idx = start; idx < end; ++idx) {
                if (B_indexed.data_[idx] != 0) {
                    count++;
                    int64_t key = static_cast<int64_t>(i) * B_indexed.cols_ + B_indexed.indices_[idx];
                    B_nnz_offset[key] = count;
                }
            }
        }
    }

    // Generate C offsets from CSR (sparse map)
    C_nnz_offset.clear();
    int count = 0;
    for (int i = 0; i < C_csr_result.rows_; ++i) {
        int start = C_csr_result.indptr_[i];
        int end = C_csr_result.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int j = C_csr_result.indices_[idx];
            if (C_csr_result.data_[idx] != 0) {
                count++;
                int64_t key = static_cast<int64_t>(i) * N + j;
                C_nnz_offset[key] = count;
            }
        }
    }

    // Auto-compute pointer offsets from nnz counts
    int a_nnz = A_orig_csr.nnz();
    int b_nnz = B_orig_csr.nnz();
    if (cfg.a_pointer_offset < 0)
        cfg.a_pointer_offset = 0;
    if (cfg.b_pointer_offset < 0)
        cfg.b_pointer_offset = cfg.element_size * a_nnz;
    if (cfg.c_pointer_offset < 0)
        cfg.c_pointer_offset = cfg.element_size * (a_nnz + b_nnz);
}

void MatrixLoader::ideal_data_transfer() {
    stats.ideal_a = A_orig_csr.nnz();
    stats.ideal_b = B_orig_csr.nnz();
    stats.ideal_c = C_csr_result.nnz();
}

Matrix<int8_t> MatrixLoader::_get_matrix_slice(const Matrix<int8_t>& mat, int row_start, int row_end) const {
    int slice_rows = row_end - row_start;
    int cols = mat.cols();
    Matrix<int8_t> slice(slice_rows, cols, 0);
    
    for (int i = 0; i < slice_rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            slice(i, j) = mat(row_start + i, j);
        }
    }
    return slice;
}

std::vector<int> MatrixLoader::_greedy_tile(const CSRMatrix& C_block, int target_nnz) const {
    std::vector<int> borders;
    int acc = 0;
    int cols = C_block.cols_;

    // Count nnz per column from CSR data
    std::vector<int> col_nnz_counts(cols, 0);
    for (int i = 0; i < C_block.rows_; ++i) {
        int start = C_block.indptr_[i];
        int end = C_block.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            col_nnz_counts[C_block.indices_[idx]]++;
        }
    }

    for (int j = 0; j < cols; ++j) {
        acc += col_nnz_counts[j];
        if (acc >= target_nnz) {
            borders.push_back(j + 1);
            acc = 0;
        }
    }

    if (borders.empty() || borders.back() != cols) {
        borders.push_back(cols);
    }

    return borders;
}

// Forward declaration (defined below dense_tiling)
static CSRMatrix csr_row_slice(const CSRMatrix& mat, int row_start, int row_end);

void MatrixLoader::group_tiling(int group_size) {
    tiles.clear();
    
    int gh;
    if (group_size >= 1.0) {
        gh = static_cast<int>(physical_pe_row_num * group_size);
    } else {
        gh = physical_pe_row_num * static_cast<int>(1.0 / group_size);
    }
    gh = std::max(1, gh);
    
    int num_groups = (M + gh - 1) / gh;
    int num_pes = physical_pe_row_num * physical_pe_col_num;
    
    if (verbose()) {
        log->info("Group tiling: " + std::to_string(num_groups) + " groups, group height=" + std::to_string(gh));
    }

    for (int g = 0; g < num_groups; ++g) {
        if (verbose() && (g % 10 == 0 || g == num_groups - 1)) {
            log->info("Group tiling: Processing group " + std::to_string(g + 1) + "/" + std::to_string(num_groups));
        }
        
        int m_start = g * gh;
        int m_end = std::min(m_start + gh, M);
        
        CSRMatrix A_block_csr = csr_row_slice(A_orig_csr, m_start, m_end);
        CSRMatrix C_block = sparse_multiply_csr(A_block_csr, B_orig_csr);

        int target_nnz = std::max(1, static_cast<int>(num_pes * cfg.tile_c_multiplier));
        std::vector<int> borders = _greedy_tile(C_block, target_nnz);
    
        int cs = 0;
        for (int ce : borders) {
            tiles.push_back(std::make_tuple(m_start, m_end, cs, ce));
            cs = ce;
        }
    }
    
    if (verbose()) {
        log->info("Group tiling: Computing v_cols...");
    }
    v_cols = get_vcols();
    if (verbose()) {
        log->info("Group tiling: Finished computing v_cols=" + std::to_string(v_cols));
    }
    
    if (verbose()) {
        log->info("Group tiling: Processing tiles...");
    }
    process_tiles();
    if (verbose()) {
        log->info("Group tiling: Finished processing tiles");
        log->info("Group tiling: Created " + std::to_string(tiles.size()) + " tiles");
    }
}

void MatrixLoader::dense_tiling() {
    tiles.clear();

    int M_tiles = (M + physical_pe_row_num - 1) / physical_pe_row_num;

    bool use_full_n = cfg.dense_tiling_full_n;

    if (!use_full_n) {
        // Check if tiling both M and N would overflow total_B_rows in
        // process_tiles(). total_B_rows = num_tiles * K; if that exceeds
        // INT_MAX the indptr vector allocation fails with SIGABRT.
        int N_tiles_check = (N + physical_pe_col_num - 1) / physical_pe_col_num;
        int64_t num_tiles = static_cast<int64_t>(M_tiles) * N_tiles_check;
        int64_t total_B_rows = num_tiles * static_cast<int64_t>(K);
        const int64_t safe_limit = static_cast<int64_t>(INT_MAX) / 2;

        if (total_B_rows > safe_limit) {
            log->info("Dense tiling: M*N tiling would produce " +
                      std::to_string(num_tiles) + " tiles (total_B_rows=" +
                      std::to_string(total_B_rows) +
                      "), exceeding safe limit. Falling back to full-N tiling.");
            use_full_n = true;
        }
    }

    if (use_full_n) {
        // Tile only M, each tile spans full N
        if (verbose()) {
            log->info("Dense tiling: " + std::to_string(M_tiles) + " M-tiles (full N per tile)");
        }

        for (int m_tile = 0; m_tile < M_tiles; ++m_tile) {
            int m_start = m_tile * physical_pe_row_num;
            int m_end = std::min(m_start + physical_pe_row_num, M);

            tiles.push_back(std::make_tuple(m_start, m_end, 0, N));
        }
    } else {
        // Tile both M and N with pe_row_num x pe_col_num
        int N_tiles = (N + physical_pe_col_num - 1) / physical_pe_col_num;

        if (verbose()) {
            log->info("Dense tiling: " + std::to_string(M_tiles) + "x" + std::to_string(N_tiles) + " = " +
                     std::to_string(M_tiles * N_tiles) + " tiles");
        }

        for (int m_tile = 0; m_tile < M_tiles; ++m_tile) {
            int m_start = m_tile * physical_pe_row_num;
            int m_end = std::min(m_start + physical_pe_row_num, M);

            for (int n_tile = 0; n_tile < N_tiles; ++n_tile) {
                int n_start = n_tile * physical_pe_col_num;
                int n_end = std::min(n_start + physical_pe_col_num, N);

                tiles.push_back(std::make_tuple(m_start, m_end, n_start, n_end));
            }
        }
    }
    
    if (cfg.enable_spatial_folding) {
        if (verbose()) {
            log->info("Dense tiling: Computing v_cols...");
        }
        v_cols = get_vcols();
        if (verbose()) {
            log->info("Dense tiling: Finished computing v_cols=" + std::to_string(v_cols));
        }
    }
    
    if (verbose()) {
        log->info("Dense tiling: Processing tiles...");
    }
    process_tiles();
    if (verbose()) {
        log->info("Dense tiling: Finished processing tiles");
        log->info("Dense tiling: Created " + std::to_string(tiles.size()) + " tiles");
    }
}

static CSRMatrix csr_row_slice(const CSRMatrix& mat, int row_start, int row_end) {
    int slice_rows = row_end - row_start;
    int cols = mat.cols_;
    
    CSRMatrix result(slice_rows, cols);
    result.indptr_[0] = 0;
    
    for (int i = row_start; i < row_end; ++i) {
        int start = mat.indptr_[i];
        int end = mat.indptr_[i + 1];
        int result_row = i - row_start;
        
        for (int idx = start; idx < end; ++idx) {
            result.indices_.push_back(mat.indices_[idx]);
            result.data_.push_back(mat.data_[idx]);
        }
        result.indptr_[result_row + 1] = static_cast<int>(result.indices_.size());
    }
    
    return result;
}

static CSRMatrix csr_col_slice(const CSRMatrix& mat, int col_start, int col_end) {
    int rows = mat.rows_;
    int slice_cols = col_end - col_start;
    
    CSRMatrix result(rows, slice_cols);
    result.indptr_[0] = 0;
    
    for (int i = 0; i < rows; ++i) {
        int start = mat.indptr_[i];
        int end = mat.indptr_[i + 1];
        
        for (int idx = start; idx < end; ++idx) {
            int col = mat.indices_[idx];
            if (col >= col_start && col < col_end) {
                result.indices_.push_back(col - col_start);
                result.data_.push_back(mat.data_[idx]);
            }
        }
        result.indptr_[i + 1] = static_cast<int>(result.indices_.size());
    }
    
    return result;
}

void MatrixLoader::process_tiles() {
    if (verbose()) {
        log->info("Process tiles: Processing " + std::to_string(tiles.size()) + " tiles...");
    }
    if (tiles.empty()) {
        return;
    }
    
    // Step 1: Calculate final dimensions
    if (verbose()) {
        log->info("Process tiles: Calculating final dimensions...");
    }
    auto dims = _compute_tile_dimensions();
    
    // Step 2: Pre-compute NNZ counts for memory allocation
    if (verbose()) {
        log->info("Process tiles: Pre-computing NNZ counts...");
    }
    auto nnz = _compute_tile_nnz_counts();
    
    if (verbose()) {
        log->info("Process tiles: Total NNZ - A:" + std::to_string(nnz.total_A_csr_nnz) + 
                 ", B:" + std::to_string(nnz.total_B_csr_nnz));
    }
    
    // Step 3: Build indexed matrices (no padding needed!)
    IndexedCSRMatrix A_indexed_tiled(dims.max_A_rows, dims.total_A_cols);
    IndexedCSRMatrix B_indexed_tiled(dims.total_B_rows, dims.max_B_cols);
    
    A_indexed_tiled.reserve(nnz.total_A_csr_nnz);
    B_indexed_tiled.reserve(nnz.total_B_csr_nnz);
    
    A_indexed_tiled.indptr_[0] = 0;
    B_indexed_tiled.indptr_[0] = 0;
    
    // Step 4: Build matrices from tiles
    _build_A_indexed_from_tiles(A_indexed_tiled);
    _build_B_indexed_from_tiles(B_indexed_tiled);
    
    // Step 5: Update member variables
    A_indexed = A_indexed_tiled;
    B_indexed = B_indexed_tiled;
    
    // Step 6: Build tile map
    _build_tile_map();
    
    if (verbose()) {
        log->info("Process tiles: Finished. A_indexed: " + std::to_string(A_indexed.rows_) + "x" + 
                 std::to_string(A_indexed.cols_) + " (nnz=" + std::to_string(A_indexed.nnz()) + 
                 "), B_indexed: " + std::to_string(B_indexed.rows_) + "x" + 
                 std::to_string(B_indexed.cols_) + " (nnz=" + std::to_string(B_indexed.nnz()) + ")");
    }
}

void MatrixLoader::init_k_to_tile_id() {
    k_to_tile_id.clear();
    // Map K indices after tiling (0 to A.cols()-1) to tile IDs
    // self.A.shape[1] is the k dimension after group_tiling/dense_tiling
    // self.K is the K dimension of the original A
    for (int k = 0; k < A_indexed.cols_; ++k) {  // Use tiled K dimension (A.cols())
        k_to_tile_id[k] = k / K;  // Map to tile ID using original K dimension
    }
}

void MatrixLoader::tile_data_transfer() {
    stats.tile_a = A_indexed.nnz();
    stats.tile_b = B_indexed.nnz();
    stats.tile_c = C_csr_result.nnz();
}

void MatrixLoader::decompose_a_row() {
    // Row decomposition: for each tiled K column of A, if it has more than
    // num_split nonzeros, split it into ceil(nnz/num_split) sub-columns,
    // each with at most num_split nonzeros. Each sub-column gets a copy of
    // the corresponding B row. This reduces the number of PE rows that
    // simultaneously demand the same B row (broadcast factor).
    int n = cfg.num_split;
    if (n <= 0) n = 1;

    int old_K = A_indexed.cols_;
    int old_M = A_indexed.rows_;
    int old_B_cols = B_indexed.cols_;

    // Step 1: Build CSC-like structure from A_indexed (CSR) to iterate by column
    // For each tiled column k, collect (row, data_idx) pairs
    std::vector<std::vector<std::pair<int, int>>> col_entries(old_K);
    for (int row = 0; row < old_M; ++row) {
        int start = A_indexed.indptr_[row];
        int end = A_indexed.indptr_[row + 1];
        for (int idx = start; idx < end; ++idx) {
            int col = A_indexed.indices_[idx];
            col_entries[col].push_back({row, idx});
        }
    }

    // Step 2: Compute new K dimension and build column mapping
    // For each old column k, it expands to ceil(nnz_k / n) sub-columns
    std::vector<int> new_col_start(old_K);  // old col k -> first new col index
    std::vector<int> new_col_count(old_K);  // old col k -> number of new sub-columns
    int new_K = 0;
    for (int k = 0; k < old_K; ++k) {
        int nnz_k = static_cast<int>(col_entries[k].size());
        if (nnz_k == 0) continue;
        new_col_start[k] = new_K;
        int m1 = (nnz_k + n - 1) / n;
        new_col_count[k] = m1;
        new_K += m1;
    }

    if (verbose()) {
        log->info("decompose_a_row: old_K=" + std::to_string(old_K) +
                  " new_K=" + std::to_string(new_K) + " num_split=" + std::to_string(n));
    }

    // Step 3: Build new A_indexed
    IndexedCSRMatrix new_A(old_M, new_K);
    new_A.indptr_.resize(old_M + 1, 0);

    // For each old column k, distribute its nonzeros across sub-columns
    // We need to build row-major output, so collect per-row entries first
    std::vector<std::vector<std::tuple<int, int8_t, int32_t, int32_t>>> new_A_rows(old_M);

    for (int k = 0; k < old_K; ++k) {
        if (col_entries[k].empty()) continue;
        int base_col = new_col_start[k];
        for (int i = 0; i < static_cast<int>(col_entries[k].size()); ++i) {
            int sub_col = base_col + i / n;
            auto [row, data_idx] = col_entries[k][i];
            new_A_rows[row].push_back({
                sub_col,
                A_indexed.data_[data_idx],
                A_indexed.orig_row_[data_idx],
                A_indexed.orig_col_[data_idx]
            });
        }
    }

    // Sort each row by column and build CSR
    new_A.indptr_[0] = 0;
    for (int row = 0; row < old_M; ++row) {
        auto& entries = new_A_rows[row];
        std::sort(entries.begin(), entries.end());
        for (auto& [col, val, orig_r, orig_c] : entries) {
            new_A.indices_.push_back(col);
            new_A.data_.push_back(val);
            new_A.orig_row_.push_back(orig_r);
            new_A.orig_col_.push_back(orig_c);
        }
        new_A.indptr_[row + 1] = static_cast<int>(new_A.indices_.size());
    }

    // Step 4: Build new B_indexed by duplicating B rows for each sub-column
    // Old B row k maps to new B rows [new_col_start[k], new_col_start[k] + new_col_count[k])
    IndexedCSRMatrix new_B(new_K, old_B_cols);
    new_B.indptr_.resize(new_K + 1, 0);
    new_B.indptr_[0] = 0;

    for (int k = 0; k < old_K; ++k) {
        if (col_entries[k].empty()) continue;
        int m1 = new_col_count[k];
        // Get old B row k data
        int b_start = B_indexed.indptr_[k];
        int b_end = B_indexed.indptr_[k + 1];

        for (int sub = 0; sub < m1; ++sub) {
            int new_row = new_col_start[k] + sub;
            // Copy entire B row for each sub-column
            for (int idx = b_start; idx < b_end; ++idx) {
                new_B.indices_.push_back(B_indexed.indices_[idx]);
                new_B.data_.push_back(B_indexed.data_[idx]);
                new_B.orig_row_.push_back(B_indexed.orig_row_[idx]);
                new_B.orig_col_.push_back(B_indexed.orig_col_[idx]);
            }
            new_B.indptr_[new_row + 1] = static_cast<int>(new_B.indices_.size());
        }
    }

    // Fill indptr for empty rows (columns of A that had 0 nnz)
    for (int r = 1; r <= new_K; ++r) {
        if (new_B.indptr_[r] == 0 && r > 0) {
            new_B.indptr_[r] = new_B.indptr_[r - 1];
        }
    }

    // Step 5: Update k_to_tile_id mapping
    std::unordered_map<int, int> new_k_to_tile_id;
    for (int k = 0; k < old_K; ++k) {
        if (col_entries[k].empty()) continue;
        int tile_id = k / K;  // Same logic as init_k_to_tile_id
        for (int sub = 0; sub < new_col_count[k]; ++sub) {
            new_k_to_tile_id[new_col_start[k] + sub] = tile_id;
        }
    }

    // Step 6: Replace member variables
    A_indexed = new_A;
    B_indexed = new_B;
    k_to_tile_id = new_k_to_tile_id;

    // Step 7: Rebuild bitmask (used by intersect_bc)
    _create_A_bitmask();

    if (verbose()) {
        log->info("decompose_a_row: A_indexed " + std::to_string(A_indexed.rows_) + "x" +
                  std::to_string(A_indexed.cols_) + " nnz=" + std::to_string(A_indexed.nnz()) +
                  ", B_indexed " + std::to_string(B_indexed.rows_) + "x" +
                  std::to_string(B_indexed.cols_) + " nnz=" + std::to_string(B_indexed.nnz()));
    }
}

void MatrixLoader::check_indices() {
    // Validate index matrices are within bounds
    // For now, this is a no-op since we'll verify during init_indices()
    // Full implementation would check row_idx_a, col_idx_a, row_idx_b, col_idx_b ranges
}

bool MatrixLoader::check_size() const {
    return M <= 1024 && K <= 1024 && N <= 1024;
}

void MatrixLoader::_create_A_bitmask() {
    // Build bitmask from tiled A matrix (after tiling), matching Python behavior
    // Python: bool_mask = (self.denseA != 0) or (self.A.toarray() != 0) after tiling
    // The bitmask is used by intersect_bc to check if tiled B row b_row intersects with output row c_row
    // It should match the tiled A dimensions: A_indexed.rows_ x A_indexed.cols_
    A_bitmask_shape = {A_indexed.rows_, A_indexed.cols_};
    int nrows = A_bitmask_shape.first;
    int ncols = A_bitmask_shape.second;
    
    int padded_cols = ((ncols + 7) / 8) * 8;
    A_bitmask = std::vector<std::vector<uint8_t>>(nrows, std::vector<uint8_t>((padded_cols + 7) / 8, 0));
    
    // Build bitmask from A_indexed non-zeros
    for (int i = 0; i < nrows; ++i) {
        int start = A_indexed.indptr_[i];
        int end = A_indexed.indptr_[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int j = A_indexed.indices_[idx];
            if (A_indexed.data_[idx] != 0) {
                int byte_idx = j / 8;
                int bit_idx = j % 8;
                A_bitmask[i][byte_idx] |= (1 << (7 - bit_idx));
            }
        }
    }
}

bool MatrixLoader::_get_bit_from_packed(int row, int col) const {
    if (row >= A_bitmask_shape.first || col >= A_bitmask_shape.second) {
        return false;
    }
    int byte_idx = col / 8;
    int bit_idx = col % 8;
    return (A_bitmask[row][byte_idx] >> (7 - bit_idx)) & 1;
}

std::tuple<int, int, int> MatrixLoader::data_transfer(const Matrix<int8_t>& A, const Matrix<int8_t>& B, int c_nnz) const {
    int a_nnz = A.nnz();
    int b_nnz = B.nnz();
    return std::make_tuple(a_nnz, b_nnz, c_nnz);
}

std::pair<int, int> MatrixLoader::get_original_index_a(int pe_row, int pe_col) const {
    return A_indexed.get_original_coords(pe_row, pe_col);
}

std::pair<int, int> MatrixLoader::get_original_index_b(int tiled_b_row, int tiled_b_col) const {
    return B_indexed.get_original_coords(tiled_b_row, tiled_b_col);
}

bool MatrixLoader::intersect_bc(int b_row, int c_row) const {
    // b_row is a tiled B row index (0-15 for 2 tiles)
    // c_row is a PE row index (0-3) when called from b_row_is_valid or filter_intersections
    // Since bitmask is built from tiled A, which has shape (A.rows(), A.cols())
    // where A.rows() = physical_pe_row_num (4) and A.cols() = tiled K dimension (16)
    // We check if PE row c_row has non-zero in tiled A column b_row
    // This matches Python behavior: _get_bit_from_packed(c_row, b_row) where c_row is PE row
    if (c_row >= A_bitmask_shape.first || b_row >= A_bitmask_shape.second) {
        return false;
    }
    return _get_bit_from_packed(c_row, b_row);
}

bool MatrixLoader::b_is_same_block(int k1, int k2) const {
    // k1 and k2 are tiled B row indices (0-15 for 2 tiles)
    // After vstack, B rows are grouped by M tiles:
    //   - B rows 0 to (K-1): first M tile
    //   - B rows K to (2*K-1): second M tile
    //   - etc.
    // So we can determine the M tile block by dividing by original K dimension
    int block1 = k1 / K;
    int block2 = k2 / K;
    return block1 == block2;
}

static std::vector<int> sparse_multiply_row_nnz(const CSRMatrix& A_csr, const CSRMatrix& B_csr) {
    auto [A_rows, A_cols] = A_csr.shape();
    auto [B_rows, B_cols] = B_csr.shape();
    
    if (A_cols != B_rows) {
        throw std::runtime_error("Matrix dimensions mismatch for sparse multiplication");
    }
    
    std::vector<int> row_nnz(A_rows, 0);
    std::vector<bool> col_has_nnz(B_cols, false);
    
    for (int i = 0; i < A_rows; ++i) {
        std::fill(col_has_nnz.begin(), col_has_nnz.end(), false);
        auto A_row = A_csr.get_row(i);
        
        for (const auto& [k, A_val] : A_row) {
            auto B_row = B_csr.get_row(k);
            for (const auto& [j, B_val] : B_row) {
                if (!col_has_nnz[j]) {
                    col_has_nnz[j] = true;
                    row_nnz[i]++;
                }
            }
        }
    }
    
    return row_nnz;
}

int MatrixLoader::get_vcols() {
    int max_v_cols = 0;

    size_t tile_count = 0;
    for (const auto& tile : tiles) {
        if (verbose() && tiles.size() > 10 && (tile_count % (tiles.size() / 10) == 0 || tile_count == tiles.size() - 1)) {
            log->info("get_vcols: Processing tile " + std::to_string(tile_count + 1) + "/" + std::to_string(tiles.size()));
        }
        tile_count++;

        auto [m_start, m_end, n_start, n_end] = tile;

        // Always use sparse path (via CSR) to avoid dense allocation
        CSRMatrix A_slice_csr = csr_row_slice(A_orig_csr, m_start, m_end);
        CSRMatrix B_slice_csr = csr_col_slice(B_orig_csr, n_start, n_end);

        std::vector<int> row_nnz = sparse_multiply_row_nnz(A_slice_csr, B_slice_csr);
        for (int nnz : row_nnz) {
            max_v_cols = std::max(max_v_cols, nnz);
        }
    }
    return max_v_cols;
}

// ============================================================================
// Helper Functions for process_tiles()
// ============================================================================

MatrixLoader::TileDimensions MatrixLoader::_compute_tile_dimensions() const {
    TileDimensions dims{0, 0, 0, 0};
    
    for (const auto& tile : tiles) {
        auto [m_start, m_end, n_start, n_end] = tile;
        int actual_rows = m_end - m_start;
        
        // max_A_rows is the maximum rows in any tile (tiles are hstacked, so all use same row count)
        // This will be padded to physical_pe_row_num if needed for compatibility
        dims.max_A_rows = std::max(dims.max_A_rows, actual_rows);
        dims.total_A_cols += A_orig_csr.cols_;  // Sum of all tile column widths (hstack)
        dims.total_B_rows += B_orig_csr.rows_;  // Sum of all tile row heights (vstack)
        dims.max_B_cols = std::max(dims.max_B_cols, n_end - n_start);
    }
    
    // Ensure max_A_rows matches physical PE row requirement for compatibility
    dims.max_A_rows = std::max(dims.max_A_rows, physical_pe_row_num);
    
    return dims;
}

MatrixLoader::TileNNZCounts MatrixLoader::_compute_tile_nnz_counts() const {
    TileNNZCounts nnz{0, 0, 0, 0, 0, 0};
    
    for (const auto& tile : tiles) {
        auto [m_start, m_end, n_start, n_end] = tile;
        
        // Count A_orig_csr NNZ (rows from m_start to m_end)
        int A_csr_nnz = 0;
        for (int i = m_start; i < m_end; ++i) {
            A_csr_nnz += A_orig_csr.indptr_[i + 1] - A_orig_csr.indptr_[i];
        }
        
        // Count B_orig_csr NNZ (all rows, but only columns from n_start to n_end)
        int B_csr_nnz = 0;
        for (int i = 0; i < B_orig_csr.rows_; ++i) {
            int start = B_orig_csr.indptr_[i];
            int end = B_orig_csr.indptr_[i + 1];
            for (int idx = start; idx < end; ++idx) {
                int col = B_orig_csr.indices_[idx];
                if (col >= n_start && col < n_end) {
                    B_csr_nnz++;
                }
            }
        }
        
        nnz.total_A_csr_nnz += A_csr_nnz;
        nnz.total_B_csr_nnz += B_csr_nnz;
    }
    
    return nnz;
}

void MatrixLoader::_build_A_indexed_from_tiles(IndexedCSRMatrix& A_indexed_out) {
    int max_rows = A_indexed_out.rows_;
    A_indexed_out.indptr_[0] = 0;
    
    // Build row by row: for each output row, process all tiles
    for (int output_row = 0; output_row < max_rows; ++output_row) {
        int A_col_offset = 0;

        for (const auto& tile : tiles) {
            auto [m_start, m_end, n_start, n_end] = tile;
            int tile_rows = m_end - m_start;

            // Only process if this tile has data for this output row
            if (output_row < tile_rows) {
                int src_row = m_start + output_row;

                // Copy non-zeros from A_indexed for this row
                int start = A_indexed.indptr_[src_row];
                int end = A_indexed.indptr_[src_row + 1];

                for (int idx = start; idx < end; ++idx) {
                    int orig_col = A_indexed.indices_[idx];
                    int8_t val = A_indexed.data_[idx];
                    int32_t orig_row = A_indexed.orig_row_[idx];
                    int32_t orig_col_coord = A_indexed.orig_col_[idx];

                    // Add to tiled matrix with offset column (horizontal stacking)
                    A_indexed_out.indices_.push_back(A_col_offset + orig_col);
                    A_indexed_out.data_.push_back(val);
                    A_indexed_out.orig_row_.push_back(orig_row);
                    A_indexed_out.orig_col_.push_back(orig_col_coord);
                }
            }

            A_col_offset += A_orig_csr.cols_;
        }
        
        // Set indptr for next row
        A_indexed_out.indptr_[output_row + 1] = static_cast<int>(A_indexed_out.indices_.size());
        
        if (verbose() && max_rows > 10 && (output_row % (max_rows / 10) == 0 || output_row == max_rows - 1)) {
            log->info("Process tiles: Processing A row " + std::to_string(output_row + 1) + 
                     "/" + std::to_string(max_rows));
        }
    }
}

void MatrixLoader::_build_B_indexed_from_tiles(IndexedCSRMatrix& B_indexed_out) {
    int B_row_offset = 0;
    size_t tile_idx = 0;

    for (const auto& tile : tiles) {
        if (verbose() && tiles.size() > 10 && 
            (tile_idx % (tiles.size() / 10) == 0 || tile_idx == tiles.size() - 1)) {
            log->info("Process tiles: Processing B tile " + std::to_string(tile_idx + 1) + 
                     "/" + std::to_string(tiles.size()));
        }
        
        auto [m_start, m_end, n_start, n_end] = tile;
        
        // For each row in B (all rows participate in each tile)
        for (int src_row = 0; src_row < B_indexed.rows_; ++src_row) {
            // Copy non-zeros that fall within this tile's column range
            int start = B_indexed.indptr_[src_row];
            int end = B_indexed.indptr_[src_row + 1];
            
            for (int idx = start; idx < end; ++idx) {
                int orig_col = B_indexed.indices_[idx];
                
                // Only include if column is in this tile's range
                if (orig_col >= n_start && orig_col < n_end) {
                    int8_t val = B_indexed.data_[idx];
                    int32_t orig_row = B_indexed.orig_row_[idx];
                    int32_t orig_col_coord = B_indexed.orig_col_[idx];
                    
                    // Add to tiled matrix with adjusted column (relative to tile start)
                    B_indexed_out.indices_.push_back(orig_col - n_start);
                    B_indexed_out.data_.push_back(val);
                    B_indexed_out.orig_row_.push_back(orig_row);
                    B_indexed_out.orig_col_.push_back(orig_col_coord);
                }
            }
            
            B_indexed_out.indptr_[B_row_offset + src_row + 1] = static_cast<int>(B_indexed_out.indices_.size());
        }

        B_row_offset += B_indexed.rows_;
        tile_idx++;
    }
}

void MatrixLoader::_build_tile_map() {
    // TODO: Remove tile_map member variable entirely if confirmed unused elsewhere
    
    tile_map.clear();
    
    if (verbose()) {
        log->info("Process tiles: Skipping tile_map build (not needed with IndexedCSRMatrix)");
    }
}

} // namespace csegfold

