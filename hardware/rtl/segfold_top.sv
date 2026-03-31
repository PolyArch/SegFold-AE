// =============================================================================
// segfold_top.sv — Top-level integration for SegFold accelerator
// DC 2018 compatible: uses flat packed ports, avoids unpacked struct ports.
// =============================================================================
module segfold_top
  import segfold_pkg::*;
#(
  parameter NUM_ROWS = VROWS,
  parameter NUM_COLS = VCOLS
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- Configuration registers ---
  input  logic [B_ROW_ADDR_W-1:0]  cfg_total_b_rows_i,
  input  logic                       cfg_start_i,

  // --- B SRAM interface ---
  output logic                      b_mem_rd_en_o,
  output logic [B_MEM_ADDR_W-1:0]  b_mem_rd_addr_o,
  input  b_element_t                b_mem_rd_data_i,

  // --- B row ROM interface ---
  output logic [B_ROW_ADDR_W-1:0]  b_row_addr_o,
  input  logic [B_MEM_ADDR_W-1:0]  b_row_start_i,
  input  logic [B_MEM_ADDR_W-1:0]  b_row_len_i,

  // --- C output interface ---
  output logic              c_out_valid_o,
  output logic [IDX_W-1:0]  c_out_row_o,
  output logic [IDX_W-1:0]  c_out_col_o,
  output logic [ACC_W-1:0]  c_out_data_o,

  // --- Status ---
  output logic              done_o,
  output logic [31:0]       cycle_count_o
);

  // =========================================================================
  // Top-level FSM
  // =========================================================================
  top_status_e top_state_r, top_state_nxt;

  logic [31:0] cycle_cnt_r;

  // =========================================================================
  // Inter-module wires (flat types for DC 2018 compatibility)
  // =========================================================================

  // MC -> Array: B load
  logic [NUM_COLS-1:0]     mc_b_load_valid    [0:NUM_ROWS-1];
  logic [B_ELEMENT_W-1:0]  mc_b_load_data     [0:NUM_ROWS-1][0:NUM_COLS-1];

  // Array -> SPAD: store
  logic [NUM_ROWS-1:0]              arr_spad_store_req;
  logic [IDX_W-1:0]                arr_spad_store_tag   [0:NUM_ROWS-1];
  logic [SPAD_ENTRY_W-1:0]         arr_spad_store_data  [0:NUM_ROWS-1];
  logic [NUM_ROWS-1:0]              arr_spad_store_ack;

  // Array -> SPAD: load
  logic [NUM_ROWS-1:0]              arr_spad_load_req;
  logic [IDX_W-1:0]                arr_spad_load_tag    [0:NUM_ROWS-1];
  logic [NUM_ROWS-1:0]              arr_spad_load_ack;
  logic [NUM_ROWS-1:0]              arr_spad_load_hit;
  logic [SPAD_ENTRY_W-1:0]         arr_spad_load_data   [0:NUM_ROWS-1];

  // Array: accumulator outputs
  logic [NUM_COLS-1:0]              acc_wr_en   [0:NUM_ROWS-1];
  logic [IDX_W-1:0]                acc_wr_row  [0:NUM_ROWS-1][0:NUM_COLS-1];
  logic [IDX_W-1:0]                acc_wr_col  [0:NUM_ROWS-1][0:NUM_COLS-1];
  logic [ACC_W-1:0]                acc_wr_data [0:NUM_ROWS-1][0:NUM_COLS-1];

  // Array status
  logic no_active_pes;
  logic no_active_switches;
  logic all_fifos_empty;
  logic no_valid_c;

  // MC status
  logic mc_done;

  // Switch status feedback: array -> mem_ctrl (2-bit encoded per position)
  logic [1:0] sw_status [0:NUM_ROWS-1][0:NUM_COLS-1];

  // =========================================================================
  // Completion detection
  // =========================================================================
  logic is_done;
  logic store_is_done;

  assign is_done      = mc_done & no_active_switches & no_active_pes &
                         all_fifos_empty;
  assign store_is_done = no_valid_c;

  // =========================================================================
  // Instantiate: Memory Controller
  // =========================================================================
  segfold_mem_ctrl u_mem_ctrl (
    .clk_i                (clk_i),
    .rst_n_i              (rst_n_i),
    .total_b_rows_i       (cfg_total_b_rows_i),
    .b_mem_rd_en_o        (b_mem_rd_en_o),
    .b_mem_rd_addr_o      (b_mem_rd_addr_o),
    .b_mem_rd_data_i      (b_mem_rd_data_i),
    .b_row_addr_o         (b_row_addr_o),
    .b_row_start_i        (b_row_start_i),
    .b_row_len_i          (b_row_len_i),
    .sw_b_load_valid_o    (mc_b_load_valid),
    .sw_b_load_data_flat_o(mc_b_load_data),
    .sw_status_flat_i     (sw_status),
    .is_done_o            (mc_done),
    .start_i              (cfg_start_i & (top_state_r == TOP_IDLE))
  );

  // =========================================================================
  // Instantiate: PE + Switch Array
  // =========================================================================
  segfold_array #(
    .NUM_ROWS (NUM_ROWS),
    .NUM_COLS (NUM_COLS)
  ) u_array (
    .clk_i                (clk_i),
    .rst_n_i              (rst_n_i),
    // B load
    .b_load_valid_i       (mc_b_load_valid),
    .b_load_data_flat_i   (mc_b_load_data),
    // SPAD store
    .spad_store_req_o     (arr_spad_store_req),
    .spad_store_tag_o     (arr_spad_store_tag),
    .spad_store_data_flat_o(arr_spad_store_data),
    .spad_store_ack_i     (arr_spad_store_ack),
    // SPAD load
    .spad_load_req_o      (arr_spad_load_req),
    .spad_load_tag_o      (arr_spad_load_tag),
    .spad_load_ack_i      (arr_spad_load_ack),
    .spad_load_hit_i      (arr_spad_load_hit),
    .spad_load_data_flat_i(arr_spad_load_data),
    // Accumulator output
    .acc_wr_en_o          (acc_wr_en),
    .acc_wr_row_o         (acc_wr_row),
    .acc_wr_col_o         (acc_wr_col),
    .acc_wr_data_o        (acc_wr_data),
    // Status
    .no_active_pes_o      (no_active_pes),
    .no_active_switches_o (no_active_switches),
    .all_fifos_empty_o    (all_fifos_empty),
    .no_valid_c_o         (no_valid_c),
    // Switch status export to memory controller
    .sw_status_flat_o     (sw_status)
  );

  // =========================================================================
  // Instantiate: SPAD (VROWS banks)
  // =========================================================================
  segfold_spad #(
    .NUM_BANKS  (NUM_ROWS),
    .DEPTH      (SPAD_DEPTH),
    .TAG_W      (IDX_W),
    .DATA_W_BANK(SPAD_ENTRY_W)
  ) u_spad (
    .clk_i       (clk_i),
    .rst_n_i     (rst_n_i),
    .store_req_i (arr_spad_store_req),
    .store_tag_i (arr_spad_store_tag),
    .store_data_i(arr_spad_store_data),
    .store_ack_o (arr_spad_store_ack),
    .load_req_i  (arr_spad_load_req),
    .load_tag_i  (arr_spad_load_tag),
    .load_ack_o  (arr_spad_load_ack),
    .load_hit_o  (arr_spad_load_hit),
    .load_data_o (arr_spad_load_data),
    .clear_i     (1'b0)
  );

  // =========================================================================
  // C output mux: serialize accumulator writes to external port
  // =========================================================================
  // Priority scan: pick first valid acc_wr from the array
  always_comb begin
    c_out_valid_o = 1'b0;
    c_out_row_o   = '0;
    c_out_col_o   = '0;
    c_out_data_o  = '0;

    for (int i = 0; i < NUM_ROWS; i++) begin
      for (int j = 0; j < NUM_COLS; j++) begin
        if (acc_wr_en[i][j] && !c_out_valid_o) begin
          c_out_valid_o = 1'b1;
          c_out_row_o   = acc_wr_row[i][j];
          c_out_col_o   = acc_wr_col[i][j];
          c_out_data_o  = acc_wr_data[i][j];
        end
      end
    end
  end

  // =========================================================================
  // Top-level FSM
  // =========================================================================
  always_comb begin
    top_state_nxt = top_state_r;

    case (top_state_r)
      TOP_IDLE: begin
        if (cfg_start_i)
          top_state_nxt = TOP_COMPUTE;
      end

      TOP_COMPUTE: begin
        if (is_done)
          top_state_nxt = TOP_CLEANUP;
      end

      TOP_CLEANUP: begin
        if (store_is_done)
          top_state_nxt = TOP_DONE;
      end

      TOP_DONE: begin
        // Stay in DONE until reset
        top_state_nxt = TOP_DONE;
      end

      default:
        top_state_nxt = TOP_IDLE;
    endcase
  end

  assign done_o        = (top_state_r == TOP_DONE);
  assign cycle_count_o = cycle_cnt_r;

  // =========================================================================
  // Sequential: FSM and cycle counter
  // =========================================================================
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      top_state_r <= TOP_IDLE;
      cycle_cnt_r <= '0;
    end else begin
      top_state_r <= top_state_nxt;
      if (top_state_r == TOP_COMPUTE || top_state_r == TOP_CLEANUP) begin
        cycle_cnt_r <= cycle_cnt_r + 1'b1;
      end
    end
  end

endmodule
