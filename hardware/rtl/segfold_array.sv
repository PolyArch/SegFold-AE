// =============================================================================
// segfold_array.sv — 2D array: VROWS x VCOLS PE rows with SPAD
// DC 2018 compatible: uses flat packed ports, avoids unpacked array ports.
// =============================================================================
module segfold_array
  import segfold_pkg::*;
#(
  parameter NUM_ROWS = VROWS,
  parameter NUM_COLS = VCOLS
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- B element load from memory controller ---
  // Flattened: per row, per col
  input  logic [NUM_COLS-1:0]          b_load_valid_i    [0:NUM_ROWS-1],
  input  logic [B_ELEMENT_W-1:0]       b_load_data_flat_i [0:NUM_ROWS-1][0:NUM_COLS-1],

  // --- SPAD interface (per bank = per row) ---
  output logic [NUM_ROWS-1:0]              spad_store_req_o,
  output logic [IDX_W-1:0]                spad_store_tag_o  [0:NUM_ROWS-1],
  output logic [SPAD_ENTRY_W-1:0]          spad_store_data_flat_o [0:NUM_ROWS-1],
  input  logic [NUM_ROWS-1:0]              spad_store_ack_i,

  output logic [NUM_ROWS-1:0]              spad_load_req_o,
  output logic [IDX_W-1:0]                spad_load_tag_o   [0:NUM_ROWS-1],
  input  logic [NUM_ROWS-1:0]              spad_load_ack_i,
  input  logic [NUM_ROWS-1:0]              spad_load_hit_i,
  input  logic [SPAD_ENTRY_W-1:0]          spad_load_data_flat_i [0:NUM_ROWS-1],

  // --- Accumulator output (from all PEs) ---
  output logic [NUM_COLS-1:0]              acc_wr_en_o   [0:NUM_ROWS-1],
  output logic [IDX_W-1:0]                acc_wr_row_o  [0:NUM_ROWS-1][0:NUM_COLS-1],
  output logic [IDX_W-1:0]                acc_wr_col_o  [0:NUM_ROWS-1][0:NUM_COLS-1],
  output logic [ACC_W-1:0]                acc_wr_data_o [0:NUM_ROWS-1][0:NUM_COLS-1],

  // --- Status ---
  output logic                             no_active_pes_o,
  output logic                             no_active_switches_o,
  output logic                             all_fifos_empty_o,
  output logic                             no_valid_c_o,

  // --- Switch status export (for memory controller feedback) ---
  output logic [1:0]                       sw_status_flat_o [0:NUM_ROWS-1][0:NUM_COLS-1]
);

  // =========================================================================
  // Per-row status
  // =========================================================================
  logic [NUM_COLS-1:0] pe_active  [0:NUM_ROWS-1];
  logic [NUM_COLS-1:0] sw_active  [0:NUM_ROWS-1];
  logic [NUM_COLS-1:0] fifo_empty [0:NUM_ROWS-1];
  logic [NUM_COLS-1:0] pe_c_valid [0:NUM_ROWS-1];
  logic [1:0]          row_sw_status [0:NUM_ROWS-1][0:NUM_COLS-1];

  // =========================================================================
  // Instantiate rows
  // =========================================================================
  genvar gi;
  generate
    for (gi = 0; gi < NUM_ROWS; gi++) begin : g_row

      segfold_pe_row #(
        .NUM_COLS (NUM_COLS),
        .ROW_IDX  (gi)
      ) u_row (
        .clk_i                (clk_i),
        .rst_n_i              (rst_n_i),
        .b_load_valid_i       (b_load_valid_i[gi]),
        .b_load_data_flat_i   (b_load_data_flat_i[gi]),
        .spad_store_req_o     (spad_store_req_o[gi]),
        .spad_store_tag_o     (spad_store_tag_o[gi]),
        .spad_store_data_flat_o(spad_store_data_flat_o[gi]),
        .spad_store_ack_i     (spad_store_ack_i[gi]),
        .spad_load_req_o      (spad_load_req_o[gi]),
        .spad_load_tag_o      (spad_load_tag_o[gi]),
        .spad_load_ack_i      (spad_load_ack_i[gi]),
        .spad_load_hit_i      (spad_load_hit_i[gi]),
        .spad_load_data_flat_i(spad_load_data_flat_i[gi]),
        .acc_wr_en_o          (acc_wr_en_o[gi]),
        .acc_wr_row_o         (acc_wr_row_o[gi]),
        .acc_wr_col_o         (acc_wr_col_o[gi]),
        .acc_wr_data_o        (acc_wr_data_o[gi]),
        .pe_active_o          (pe_active[gi]),
        .sw_active_o          (sw_active[gi]),
        .fifo_empty_o         (fifo_empty[gi]),
        .pe_c_valid_o         (pe_c_valid[gi]),
        .sw_status_o          (row_sw_status[gi])
      );

      // Export switch status
      genvar gj_s;
      for (gj_s = 0; gj_s < NUM_COLS; gj_s++) begin : g_sw_export
        assign sw_status_flat_o[gi][gj_s] = row_sw_status[gi][gj_s];
      end
    end
  endgenerate

  // =========================================================================
  // Global status: reduction across all rows and columns
  // =========================================================================
  always_comb begin
    logic any_pe_active, any_sw_active, any_fifo_not_empty, any_c_valid;
    any_pe_active     = 1'b0;
    any_sw_active     = 1'b0;
    any_fifo_not_empty = 1'b0;
    any_c_valid       = 1'b0;

    for (int i = 0; i < NUM_ROWS; i++) begin
      any_pe_active     = any_pe_active     | (|pe_active[i]);
      any_sw_active     = any_sw_active     | (|sw_active[i]);
      any_fifo_not_empty = any_fifo_not_empty | (~(&fifo_empty[i]));
      any_c_valid       = any_c_valid       | (|pe_c_valid[i]);
    end

    no_active_pes_o      = ~any_pe_active;
    no_active_switches_o = ~any_sw_active;
    all_fifos_empty_o    = ~any_fifo_not_empty;
    no_valid_c_o         = ~any_c_valid;
  end

endmodule
