// =============================================================================
// segfold_pe_row.sv — One row: VCOLS instances of {Switch + SW-PE FIFO + PE}
// DC 2018 compatible: avoids unpacked array ports in generate blocks.
// =============================================================================
module segfold_pe_row
  import segfold_pkg::*;
#(
  parameter NUM_COLS = VCOLS,
  parameter ROW_IDX  = 0
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- B element load from memory controller (per-column, packed) ---
  input  logic [NUM_COLS-1:0]          b_load_valid_i,
  input  logic [B_ELEMENT_W-1:0]       b_load_data_flat_i [0:NUM_COLS-1],

  // --- SPAD bank interface (shared for this row) ---
  output logic                    spad_store_req_o,
  output logic [IDX_W-1:0]       spad_store_tag_o,
  output logic [SPAD_ENTRY_W-1:0] spad_store_data_flat_o,
  input  logic                    spad_store_ack_i,

  output logic                    spad_load_req_o,
  output logic [IDX_W-1:0]       spad_load_tag_o,
  input  logic                    spad_load_ack_i,
  input  logic                    spad_load_hit_i,
  input  logic [SPAD_ENTRY_W-1:0] spad_load_data_flat_i,

  // --- Accumulator output (from all PEs in row, packed per col) ---
  output logic [NUM_COLS-1:0]     acc_wr_en_o,
  output logic [IDX_W-1:0]       acc_wr_row_o  [0:NUM_COLS-1],
  output logic [IDX_W-1:0]       acc_wr_col_o  [0:NUM_COLS-1],
  output logic [ACC_W-1:0]       acc_wr_data_o [0:NUM_COLS-1],

  // --- Status ---
  output logic [NUM_COLS-1:0]     pe_active_o,
  output logic [NUM_COLS-1:0]     sw_active_o,
  output logic [NUM_COLS-1:0]     fifo_empty_o,
  output logic [NUM_COLS-1:0]     pe_c_valid_o,
  output logic [1:0]              sw_status_o [0:NUM_COLS-1]
);

  // =========================================================================
  // Internal wires
  // =========================================================================

  // Switch chain: move_right from j -> j+1
  logic              sw_move_right_valid [0:NUM_COLS-1];
  b_element_t        sw_move_right_data  [0:NUM_COLS-1];

  // c_col push-right chain
  logic              c_col_push_valid_in  [0:NUM_COLS-1];
  logic [IDX_W-1:0]  c_col_push_data_in   [0:NUM_COLS-1];
  logic              c_col_push_valid_out [0:NUM_COLS-1];
  logic [IDX_W-1:0]  c_col_push_data_out  [0:NUM_COLS-1];

  // Switch status
  sw_status_e        sw_status   [0:NUM_COLS-1];
  logic              sw_c_col_valid [0:NUM_COLS-1];
  logic [IDX_W-1:0]  sw_c_col_data  [0:NUM_COLS-1];

  // FIFO <-> PE wires
  logic              fifo_wr_valid [0:NUM_COLS-1];
  fifo_entry_t       fifo_wr_data  [0:NUM_COLS-1];
  logic              fifo_full     [0:NUM_COLS-1];
  logic              fifo_empty_w  [0:NUM_COLS-1];
  fifo_entry_t       fifo_rd_data  [0:NUM_COLS-1];
  logic              fifo_pop      [0:NUM_COLS-1];

  // FIFO bypass output to PE
  logic [FIFO_ENTRY_W-1:0] fifo_pe_data  [0:NUM_COLS-1];
  logic                     fifo_pe_valid [0:NUM_COLS-1];

  // Per-PE SPAD requests (before arbitration)
  logic              pe_spad_store_req  [0:NUM_COLS-1];
  logic [IDX_W-1:0]  pe_spad_store_tag  [0:NUM_COLS-1];
  spad_entry_t       pe_spad_store_data [0:NUM_COLS-1];
  logic              pe_spad_store_ack  [0:NUM_COLS-1];

  logic              pe_spad_load_req   [0:NUM_COLS-1];
  logic [IDX_W-1:0]  pe_spad_load_tag   [0:NUM_COLS-1];
  logic              pe_spad_load_ack   [0:NUM_COLS-1];
  logic              pe_spad_load_hit   [0:NUM_COLS-1];

  // Cast flat SPAD load data to struct
  spad_entry_t spad_load_data_s;
  assign spad_load_data_s = spad_entry_t'(spad_load_data_flat_i);

  // =========================================================================
  // Pack fifo_empty into output vector and export switch status
  // =========================================================================
  genvar gk;
  generate
    for (gk = 0; gk < NUM_COLS; gk++) begin : g_fifo_empty_pack
      assign fifo_empty_o[gk] = fifo_empty_w[gk];
      assign sw_status_o[gk]  = sw_status[gk];
    end
  endgenerate

  // =========================================================================
  // c_col push chain: leftmost has no input
  // =========================================================================
  assign c_col_push_valid_in[0] = 1'b0;
  assign c_col_push_data_in[0]  = '0;

  genvar gj;
  generate
    for (gj = 1; gj < NUM_COLS; gj++) begin : g_push_chain
      assign c_col_push_valid_in[gj] = c_col_push_valid_out[gj-1];
      assign c_col_push_data_in[gj]  = c_col_push_data_out[gj-1];
    end
  endgenerate

  // =========================================================================
  // Instantiate Switch + FIFO + PE per column
  // =========================================================================
  generate
    for (gj = 0; gj < NUM_COLS; gj++) begin : g_col

      // Cast flat B data to struct
      b_element_t b_load_data_s;
      assign b_load_data_s = b_element_t'(b_load_data_flat_i[gj]);

      // -------------------------------------------------------------------
      // Switch
      // -------------------------------------------------------------------
      segfold_switch u_switch (
        .clk_i               (clk_i),
        .rst_n_i             (rst_n_i),
        .b_load_valid_i      (b_load_valid_i[gj]),
        .b_load_data_i       (b_load_data_s),
        .b_from_left_valid_i (gj > 0 ? sw_move_right_valid[gj-1] : 1'b0),
        .b_from_left_data_i  (gj > 0 ? sw_move_right_data[gj-1]  : b_element_t'(0)),
        .move_right_valid_o  (sw_move_right_valid[gj]),
        .move_right_data_o   (sw_move_right_data[gj]),
        .fifo_wr_valid_o     (fifo_wr_valid[gj]),
        .fifo_wr_data_o      (fifo_wr_data[gj]),
        .fifo_full_i         (fifo_full[gj]),
        .c_col_push_valid_i  (c_col_push_valid_in[gj]),
        .c_col_push_data_i   (c_col_push_data_in[gj]),
        .c_col_push_valid_o  (c_col_push_valid_out[gj]),
        .c_col_push_data_o   (c_col_push_data_out[gj]),
        .next_sw_idle_i      (gj < NUM_COLS-1 ?
                               (sw_status[gj+1] == SW_IDLE) : 1'b0),
        .is_last_col_i       (gj == NUM_COLS-1 ? 1'b1 : 1'b0),
        .status_o            (sw_status[gj]),
        .c_col_valid_o       (sw_c_col_valid[gj]),
        .c_col_data_o        (sw_c_col_data[gj]),
        .active_o            (sw_active_o[gj])
      );

      // -------------------------------------------------------------------
      // SW-PE FIFO (with bypass mux output to PE)
      // -------------------------------------------------------------------
      // PE idle detection for bypass
      logic pe_is_idle;

      segfold_sync_fifo #(
        .WIDTH  (FIFO_ENTRY_W),
        .DEPTH  (FIFO_DEPTH),
        .ADDR_W (FIFO_ADDR_W),
        .PTR_W  (FIFO_PTR_W)
      ) u_fifo (
        .clk_i          (clk_i),
        .rst_n_i        (rst_n_i),
        .wr_en_i        (fifo_wr_valid[gj] & ~(fifo_empty_w[gj] & pe_is_idle)),
        .wr_data_i      (fifo_wr_data[gj]),
        .rd_en_i        (fifo_pop[gj]),
        .rd_data_o      (fifo_rd_data[gj]),
        .bypass_valid_i (fifo_wr_valid[gj] & pe_is_idle),
        .bypass_data_i  (fifo_wr_data[gj]),
        .bypass_active_o(),
        .full_o         (fifo_full[gj]),
        .empty_o        (fifo_empty_w[gj]),
        .pe_data_o      (fifo_pe_data[gj]),
        .pe_valid_o     (fifo_pe_valid[gj])
      );

      // -------------------------------------------------------------------
      // Processing Element
      // The PE reads from the FIFO bypass mux output (pe_data/pe_valid)
      // rather than raw rd_data to benefit from the bypass path.
      // -------------------------------------------------------------------

      // PE sees FIFO as "not empty" when bypass mux has valid data
      logic pe_fifo_empty;
      assign pe_fifo_empty = ~fifo_pe_valid[gj];

      segfold_pe u_pe (
        .clk_i            (clk_i),
        .rst_n_i          (rst_n_i),
        .fifo_empty_i     (pe_fifo_empty),
        .fifo_data_i      (fifo_entry_t'(fifo_pe_data[gj])),
        .fifo_pop_o       (fifo_pop[gj]),
        .spad_store_req_o (pe_spad_store_req[gj]),
        .spad_store_tag_o (pe_spad_store_tag[gj]),
        .spad_store_data_o(pe_spad_store_data[gj]),
        .spad_store_ack_i (pe_spad_store_ack[gj]),
        .spad_load_req_o  (pe_spad_load_req[gj]),
        .spad_load_tag_o  (pe_spad_load_tag[gj]),
        .spad_load_ack_i  (pe_spad_load_ack[gj]),
        .spad_load_hit_i  (pe_spad_load_hit[gj]),
        .spad_load_data_i (spad_load_data_s),
        .acc_wr_en_o      (acc_wr_en_o[gj]),
        .acc_wr_row_o     (acc_wr_row_o[gj]),
        .acc_wr_col_o     (acc_wr_col_o[gj]),
        .acc_wr_data_o    (acc_wr_data_o[gj]),
        .active_o         (pe_active_o[gj]),
        .c_valid_o        (pe_c_valid_o[gj])
      );

      assign pe_is_idle = ~pe_active_o[gj] & pe_fifo_empty;
    end
  endgenerate

  // =========================================================================
  // SPAD port arbitration: fixed priority (lowest column wins)
  // =========================================================================

  // --- Store arbiter ---
  always_comb begin
    spad_store_req_o       = 1'b0;
    spad_store_tag_o       = '0;
    spad_store_data_flat_o = '0;
    for (int j = 0; j < NUM_COLS; j++) begin
      pe_spad_store_ack[j] = 1'b0;
    end

    for (int j = 0; j < NUM_COLS; j++) begin
      if (pe_spad_store_req[j] && !spad_store_req_o) begin
        spad_store_req_o       = 1'b1;
        spad_store_tag_o       = pe_spad_store_tag[j];
        spad_store_data_flat_o = SPAD_ENTRY_W'(pe_spad_store_data[j]);
        pe_spad_store_ack[j]   = spad_store_ack_i;
      end
    end
  end

  // --- Load arbiter ---
  always_comb begin
    spad_load_req_o = 1'b0;
    spad_load_tag_o = '0;
    for (int j = 0; j < NUM_COLS; j++) begin
      pe_spad_load_ack[j] = 1'b0;
      pe_spad_load_hit[j] = 1'b0;
    end

    for (int j = 0; j < NUM_COLS; j++) begin
      if (pe_spad_load_req[j] && !spad_load_req_o) begin
        spad_load_req_o = 1'b1;
        spad_load_tag_o = pe_spad_load_tag[j];
        pe_spad_load_ack[j] = spad_load_ack_i;
        pe_spad_load_hit[j] = spad_load_hit_i;
      end
    end
  end

endmodule
