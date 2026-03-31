// =============================================================================
// segfold_lut_bank.sv — Single CAM bank for forward LUT (non-destructive read)
// =============================================================================
module segfold_lut_bank
  import segfold_pkg::*;
#(
  parameter DEPTH  = LUT_DEPTH,
  parameter KEY_W  = LUT_KEY_W,
  parameter VAL_W  = LUT_VAL_W,
  parameter ADDR_W = LUT_ADDR_W
)(
  input  logic                clk_i,
  input  logic                rst_n_i,

  // Lookup port (non-destructive)
  input  logic                lookup_req_i,
  input  logic [KEY_W-1:0]   lookup_key_i,
  output logic                lookup_hit_o,
  output logic [VAL_W-1:0]   lookup_val_o,

  // Update port (overwrite-or-insert)
  input  logic                upd_valid_i,
  input  logic [KEY_W-1:0]   upd_key_i,
  input  logic [VAL_W-1:0]   upd_val_i,

  // Clear all entries
  input  logic                clear_i
);

  // -------------------------------------------------------------------------
  // CAM storage
  // -------------------------------------------------------------------------
  logic                valid_r [0:DEPTH-1];
  logic [KEY_W-1:0]   key_r   [0:DEPTH-1];
  logic [VAL_W-1:0]   val_r   [0:DEPTH-1];

  // -------------------------------------------------------------------------
  // Lookup: combinational parallel key match (non-destructive)
  // -------------------------------------------------------------------------
  logic               lookup_found;

  always_comb begin
    lookup_found = 1'b0;
    lookup_val_o = '0;
    for (int i = 0; i < DEPTH; i++) begin
      if (valid_r[i] && (key_r[i] == lookup_key_i) && !lookup_found) begin
        lookup_found = 1'b1;
        lookup_val_o = val_r[i];
      end
    end
  end

  assign lookup_hit_o = lookup_req_i & lookup_found;

  // -------------------------------------------------------------------------
  // Update: find matching key or first free slot
  // -------------------------------------------------------------------------
  logic [ADDR_W-1:0]  upd_slot;
  logic                upd_found;

  always_comb begin
    upd_found = 1'b0;
    upd_slot  = '0;
    // First pass: find existing key match
    for (int i = 0; i < DEPTH; i++) begin
      if (valid_r[i] && (key_r[i] == upd_key_i) && !upd_found) begin
        upd_found = 1'b1;
        upd_slot  = ADDR_W'(i);
      end
    end
    // Second pass: find first free slot if no key match
    if (!upd_found) begin
      for (int i = 0; i < DEPTH; i++) begin
        if (!valid_r[i] && !upd_found) begin
          upd_found = 1'b1;
          upd_slot  = ADDR_W'(i);
        end
      end
    end
  end

  // -------------------------------------------------------------------------
  // Sequential: update storage
  // -------------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i || clear_i) begin
      for (int i = 0; i < DEPTH; i++) begin
        valid_r[i] <= 1'b0;
      end
    end else begin
      // Update operation
      if (upd_valid_i && upd_found) begin
        valid_r[upd_slot] <= 1'b1;
        key_r[upd_slot]   <= upd_key_i;
        val_r[upd_slot]   <= upd_val_i;
      end
    end
  end

endmodule
