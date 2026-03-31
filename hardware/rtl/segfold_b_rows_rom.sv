// =============================================================================
// segfold_b_rows_rom.sv — FF-based ROM for sorted B row indices
// =============================================================================
// Stores the preprocessed list of B row indices to load.
// Written at configuration time, read combinationally during compute.
// =============================================================================
module segfold_b_rows_rom
  import segfold_pkg::*;
#(
  parameter DEPTH  = B_MAX_ROWS,
  parameter DATA_W_ROM = B_ROW_ADDR_W,
  parameter ADDR_W = B_ROW_ADDR_W
)(
  input  logic                    clk_i,
  input  logic                    rst_n_i,

  // Write port (config-time loading)
  input  logic                    wr_en_i,
  input  logic [ADDR_W-1:0]      wr_addr_i,
  input  logic [DATA_W_ROM-1:0]  wr_data_i,

  // Read port (combinational, addressed by lptr)
  input  logic [ADDR_W-1:0]      rd_addr_i,
  output logic [DATA_W_ROM-1:0]  rd_data_o
);

  // -------------------------------------------------------------------------
  // FF storage
  // -------------------------------------------------------------------------
  logic [DATA_W_ROM-1:0] mem_r [0:DEPTH-1];

  // -------------------------------------------------------------------------
  // Combinational read
  // -------------------------------------------------------------------------
  assign rd_data_o = mem_r[rd_addr_i];

  // -------------------------------------------------------------------------
  // Sequential write with synchronous reset
  // -------------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      for (int i = 0; i < DEPTH; i++) begin
        mem_r[i] <= '0;
      end
    end else if (wr_en_i) begin
      mem_r[wr_addr_i] <= wr_data_i;
    end
  end

endmodule
