// =============================================================================
// segfold_spad.sv — SPAD top: VROWS CAM banks, routes requests by bank index
// DC 2018 compatible: uses flat packed ports.
// =============================================================================
module segfold_spad
  import segfold_pkg::*;
#(
  parameter NUM_BANKS  = VROWS,
  parameter DEPTH      = SPAD_DEPTH,
  parameter TAG_W      = IDX_W,
  parameter DATA_W_BANK = SPAD_ENTRY_W
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // Per-bank store port
  input  logic [NUM_BANKS-1:0]               store_req_i,
  input  logic [TAG_W-1:0]                   store_tag_i   [0:NUM_BANKS-1],
  input  logic [DATA_W_BANK-1:0]             store_data_i  [0:NUM_BANKS-1],
  output logic [NUM_BANKS-1:0]               store_ack_o,

  // Per-bank load port
  input  logic [NUM_BANKS-1:0]               load_req_i,
  input  logic [TAG_W-1:0]                   load_tag_i    [0:NUM_BANKS-1],
  output logic [NUM_BANKS-1:0]               load_ack_o,
  output logic [NUM_BANKS-1:0]               load_hit_o,
  output logic [DATA_W_BANK-1:0]             load_data_o   [0:NUM_BANKS-1],

  // Global clear
  input  logic                               clear_i
);

  // -------------------------------------------------------------------------
  // Instantiate one bank per PE row
  // -------------------------------------------------------------------------
  genvar gi;
  generate
    for (gi = 0; gi < NUM_BANKS; gi++) begin : g_bank
      segfold_spad_bank #(
        .DEPTH      (DEPTH),
        .TAG_W      (TAG_W),
        .DATA_W_BANK(DATA_W_BANK)
      ) u_bank (
        .clk_i       (clk_i),
        .rst_n_i     (rst_n_i),
        .store_req_i (store_req_i[gi]),
        .store_tag_i (store_tag_i[gi]),
        .store_data_i(store_data_i[gi]),
        .store_ack_o (store_ack_o[gi]),
        .load_req_i  (load_req_i[gi]),
        .load_tag_i  (load_tag_i[gi]),
        .load_ack_o  (load_ack_o[gi]),
        .load_hit_o  (load_hit_o[gi]),
        .load_data_o (load_data_o[gi]),
        .clear_i     (clear_i)
      );
    end
  endgenerate

endmodule
