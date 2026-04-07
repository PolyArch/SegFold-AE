// =============================================================================
// segfold_mem_ctrl.sv — Simplified Memory Controller (B row loader)
// =============================================================================
// Loads B elements from SRAM and issues them to the switch array.
// Simplified: no LUT interaction, no B Loader FIFOs, no tile eviction.
// =============================================================================
module segfold_mem_ctrl
  import segfold_pkg::*;
#(
  parameter II            = MC_II,
  parameter WINDOW_SIZE   = B_LOADER_WINDOW_SIZE,
  parameter MAX_B_ROWS    = B_MAX_ROWS,
  parameter MEM_DEPTH     = B_MEM_DEPTH,
  parameter NUM_ROWS      = VROWS,
  parameter NUM_COLS      = VCOLS
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- Configuration (loaded before compute) ---
  input  logic [B_ROW_ADDR_W-1:0]  total_b_rows_i,

  // --- B SRAM read port ---
  output logic                      b_mem_rd_en_o,
  output logic [B_MEM_ADDR_W-1:0]  b_mem_rd_addr_o,
  input  b_element_t                b_mem_rd_data_i,

  // --- B row start address ROM ---
  output logic [B_ROW_ADDR_W-1:0]  b_row_addr_o,
  input  logic [B_MEM_ADDR_W-1:0]  b_row_start_i,
  input  logic [B_MEM_ADDR_W-1:0]  b_row_len_i,

  // --- Output to switch array (flat) ---
  output logic [NUM_COLS-1:0]          sw_b_load_valid_o  [0:NUM_ROWS-1],
  output logic [B_ELEMENT_W-1:0]       sw_b_load_data_flat_o [0:NUM_ROWS-1][0:NUM_COLS-1],

  // --- Switch status feedback (2-bit encoded per position) ---
  input  logic [1:0]                   sw_status_flat_i [0:NUM_ROWS-1][0:NUM_COLS-1],

  // --- Completion ---
  output logic                     is_done_o,

  // --- Start signal ---
  input  logic                     start_i
);

  // =========================================================================
  // Active window register file
  // =========================================================================
  logic                      win_valid_r  [0:WINDOW_SIZE-1];
  logic [B_ROW_ADDR_W-1:0]  win_row_r    [0:WINDOW_SIZE-1];
  logic [B_MEM_ADDR_W-1:0]  win_offset_r [0:WINDOW_SIZE-1];
  logic [B_MEM_ADDR_W-1:0]  win_limit_r  [0:WINDOW_SIZE-1];
  logic [B_MEM_ADDR_W-1:0]  win_start_r  [0:WINDOW_SIZE-1];

  // =========================================================================
  // Pointers and counters
  // =========================================================================
  logic [B_ROW_ADDR_W-1:0]  lptr_r;
  logic [MC_CNT_W-1:0]      ii_cnt_r;
  logic                      running_r;

  // =========================================================================
  // Window status
  // =========================================================================
  logic window_empty;
  logic all_rows_queued;

  always_comb begin
    window_empty = 1'b1;
    for (int w = 0; w < WINDOW_SIZE; w++) begin
      if (win_valid_r[w])
        window_empty = 1'b0;
    end
  end

  assign all_rows_queued = (lptr_r >= total_b_rows_i);
  assign is_done_o       = running_r & window_empty & all_rows_queued;

  // =========================================================================
  // II counter: ready when counter reaches 0
  // =========================================================================
  logic ready;
  assign ready = running_r & (ii_cnt_r == '0);

  // =========================================================================
  // Find first valid window entry to load
  // =========================================================================
  logic [WIN_ADDR_W-1:0]    sel_win_idx;
  logic                      sel_win_found;
  logic [B_ROW_ADDR_W-1:0]  sel_row;
  logic [B_MEM_ADDR_W-1:0]  sel_addr;

  always_comb begin
    sel_win_found = 1'b0;
    sel_win_idx   = '0;
    sel_row       = '0;
    sel_addr      = '0;

    if (ready) begin
      for (int w = 0; w < WINDOW_SIZE; w++) begin
        if (win_valid_r[w] && !sel_win_found) begin
          sel_win_found = 1'b1;
          sel_win_idx   = WIN_ADDR_W'(w);
          sel_row       = win_row_r[w];
          sel_addr      = win_start_r[w] + win_offset_r[w];
        end
      end
    end
  end

  // =========================================================================
  // Target switch: find first idle switch in target row
  // =========================================================================
  // Parameterized row index width
  localparam ROW_IDX_W = (NUM_ROWS > 1) ? $clog2(NUM_ROWS) : 1;
  localparam COL_IDX_W = (NUM_COLS > 1) ? $clog2(NUM_COLS) : 1;

  logic [ROW_IDX_W-1:0] target_row;
  logic [COL_IDX_W-1:0] target_col;
  logic                  target_valid;

  always_comb begin
    target_row   = sel_row[ROW_IDX_W-1:0];
    target_col   = '0;
    target_valid = 1'b0;

    if (sel_win_found && (sel_row < NUM_ROWS)) begin
      for (int j = 0; j < NUM_COLS; j++) begin
        if ((sw_status_flat_i[target_row][j] == 2'b00) && !target_valid) begin
          target_col   = COL_IDX_W'(j);
          target_valid = 1'b1;
        end
      end
    end
  end

  // =========================================================================
  // B SRAM read
  // =========================================================================
  assign b_mem_rd_en_o   = sel_win_found & target_valid;
  assign b_mem_rd_addr_o = sel_addr;
  assign b_row_addr_o    = lptr_r;

  // =========================================================================
  // Switch output mux
  // =========================================================================
  always_comb begin
    for (int i = 0; i < NUM_ROWS; i++) begin
      sw_b_load_valid_o[i] = '0;
      for (int j = 0; j < NUM_COLS; j++) begin
        sw_b_load_data_flat_o[i][j] = '0;
      end
    end

    if (b_mem_rd_en_o) begin
      sw_b_load_valid_o[target_row][target_col] = 1'b1;
      sw_b_load_data_flat_o[target_row][target_col] = B_ELEMENT_W'(b_mem_rd_data_i);
    end
  end

  // =========================================================================
  // Window management: find first free slot
  // =========================================================================
  logic [WIN_ADDR_W-1:0] free_slot_idx;
  logic                   free_slot_found;

  always_comb begin
    free_slot_found = 1'b0;
    free_slot_idx   = '0;
    for (int w = 0; w < WINDOW_SIZE; w++) begin
      if (!win_valid_r[w] && !free_slot_found) begin
        free_slot_found = 1'b1;
        free_slot_idx   = WIN_ADDR_W'(w);
      end
    end
  end

  logic can_fill;
  assign can_fill = free_slot_found & ~all_rows_queued & running_r;

  // =========================================================================
  // Sequential logic
  // =========================================================================
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      lptr_r    <= '0;
      ii_cnt_r  <= MC_CNT_W'(II);
      running_r <= 1'b0;
      for (int w = 0; w < WINDOW_SIZE; w++) begin
        win_valid_r[w]  <= 1'b0;
        win_row_r[w]    <= '0;
        win_offset_r[w] <= '0;
        win_limit_r[w]  <= '0;
        win_start_r[w]  <= '0;
      end
    end else begin
      if (start_i && !running_r)
        running_r <= 1'b1;

      // II counter
      if (running_r) begin
        if (ii_cnt_r == '0)
          ii_cnt_r <= MC_CNT_W'(II);
        else
          ii_cnt_r <= ii_cnt_r - 1'b1;
      end

      // Advance selected window entry
      if (b_mem_rd_en_o) begin
        win_offset_r[sel_win_idx] <= win_offset_r[sel_win_idx] + 1'b1;
        if (win_offset_r[sel_win_idx] + 1'b1 >= win_limit_r[sel_win_idx])
          win_valid_r[sel_win_idx] <= 1'b0;
      end

      // Fill window
      if (can_fill) begin
        win_valid_r[free_slot_idx]  <= 1'b1;
        win_row_r[free_slot_idx]    <= lptr_r;
        win_offset_r[free_slot_idx] <= '0;
        win_limit_r[free_slot_idx]  <= b_row_len_i;
        win_start_r[free_slot_idx]  <= b_row_start_i;
        lptr_r <= lptr_r + 1'b1;
      end
    end
  end

endmodule
