// =============================================================================
// segfold_sync_fifo.sv — Parameterized synchronous FIFO with bypass mux
// =============================================================================
module segfold_sync_fifo
  import segfold_pkg::*;
#(
  parameter WIDTH = FIFO_ENTRY_W,
  parameter DEPTH = FIFO_DEPTH,
  parameter ADDR_W = FIFO_ADDR_W,    // clog2(DEPTH)
  parameter PTR_W  = FIFO_PTR_W      // ADDR_W + 1
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // Write port (from switch)
  input  logic              wr_en_i,
  input  logic [WIDTH-1:0]  wr_data_i,

  // Read port (to PE)
  input  logic              rd_en_i,
  output logic [WIDTH-1:0]  rd_data_o,

  // Bypass port (switch -> PE when FIFO empty + PE idle)
  input  logic              bypass_valid_i,
  input  logic [WIDTH-1:0]  bypass_data_i,
  output logic              bypass_active_o,

  // Status
  output logic              full_o,
  output logic              empty_o,

  // Muxed output to PE
  output logic [WIDTH-1:0]  pe_data_o,
  output logic              pe_valid_o
);

  // -------------------------------------------------------------------------
  // Storage and pointers
  // -------------------------------------------------------------------------
  logic [WIDTH-1:0] mem [0:DEPTH-1];
  logic [PTR_W-1:0] wr_ptr_r, rd_ptr_r;

  // -------------------------------------------------------------------------
  // Full / empty detection (extra-MSB technique)
  // -------------------------------------------------------------------------
  wire ptr_msb_diff = (wr_ptr_r[PTR_W-1] != rd_ptr_r[PTR_W-1]);
  wire ptr_addr_eq  = (wr_ptr_r[ADDR_W-1:0] == rd_ptr_r[ADDR_W-1:0]);

  assign full_o  = ptr_msb_diff & ptr_addr_eq;
  assign empty_o = (wr_ptr_r == rd_ptr_r);

  // -------------------------------------------------------------------------
  // Read data from head
  // -------------------------------------------------------------------------
  assign rd_data_o = mem[rd_ptr_r[ADDR_W-1:0]];

  // -------------------------------------------------------------------------
  // Bypass logic: active when bypass requested, FIFO empty, and no write
  // -------------------------------------------------------------------------
  assign bypass_active_o = bypass_valid_i & empty_o;

  // -------------------------------------------------------------------------
  // Muxed output: bypass path has priority when active
  // -------------------------------------------------------------------------
  always_comb begin
    if (bypass_active_o) begin
      pe_data_o  = bypass_data_i;
      pe_valid_o = 1'b1;
    end else if (!empty_o) begin
      pe_data_o  = rd_data_o;
      pe_valid_o = 1'b1;
    end else begin
      pe_data_o  = '0;
      pe_valid_o = 1'b0;
    end
  end

  // -------------------------------------------------------------------------
  // Pointer and memory update
  // -------------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      wr_ptr_r <= '0;
      rd_ptr_r <= '0;
    end else begin
      // Write
      if (wr_en_i && !full_o) begin
        mem[wr_ptr_r[ADDR_W-1:0]] <= wr_data_i;
        wr_ptr_r <= wr_ptr_r + 1'b1;
      end
      // Read
      if (rd_en_i && !empty_o) begin
        rd_ptr_r <= rd_ptr_r + 1'b1;
      end
    end
  end

endmodule
