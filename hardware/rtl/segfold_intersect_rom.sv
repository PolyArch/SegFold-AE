// =============================================================================
// segfold_intersect_rom.sv — FF-based ROM for A-B intersection bitmask
// =============================================================================
// Stores a 2D bitmask: for each B row, which PE rows have intersecting
// non-zero columns. Written at configuration time, read combinationally
// during compute.
// =============================================================================
module segfold_intersect_rom
  import segfold_pkg::*;
#(
  parameter DEPTH  = B_MAX_ROWS,
  parameter MASK_W = VROWS,
  parameter ADDR_W = B_ROW_ADDR_W
)(
  input  logic                clk_i,
  input  logic                rst_n_i,

  // Write port (config-time loading)
  input  logic                wr_en_i,
  input  logic [ADDR_W-1:0]  wr_addr_i,
  input  logic [MASK_W-1:0]  wr_data_i,

  // Read port (combinational, indexed by b_row)
  input  logic [ADDR_W-1:0]  rd_addr_i,
  output logic [MASK_W-1:0]  rd_data_o
);

  // -------------------------------------------------------------------------
  // FF storage
  // -------------------------------------------------------------------------
  logic [MASK_W-1:0] mem_r [0:DEPTH-1];

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
