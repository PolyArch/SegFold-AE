// =============================================================================
// segfold_spad_bank.sv — Single CAM bank for C partial sums (destructive read)
// =============================================================================
module segfold_spad_bank
  import segfold_pkg::*;
#(
  parameter DEPTH  = SPAD_DEPTH,
  parameter TAG_W  = IDX_W,          // c_col tag width
  parameter DATA_W_BANK = SPAD_ENTRY_W,  // {val, m, n, last_k, c_col}
  parameter ADDR_W = SPAD_ADDR_W
)(
  input  logic                    clk_i,
  input  logic                    rst_n_i,

  // Store port
  input  logic                    store_req_i,
  input  logic [TAG_W-1:0]       store_tag_i,       // c_col key
  input  logic [DATA_W_BANK-1:0] store_data_i,      // spad_entry_t
  output logic                    store_ack_o,

  // Load port (destructive read)
  input  logic                    load_req_i,
  input  logic [TAG_W-1:0]       load_tag_i,        // c_col key
  output logic                    load_ack_o,
  output logic                    load_hit_o,
  output logic [DATA_W_BANK-1:0] load_data_o,

  // Clear all entries
  input  logic                    clear_i
);

  // -------------------------------------------------------------------------
  // CAM storage
  // -------------------------------------------------------------------------
  logic                    valid_r [0:DEPTH-1];
  logic [TAG_W-1:0]       tag_r   [0:DEPTH-1];
  logic [DATA_W_BANK-1:0] data_r  [0:DEPTH-1];

  // -------------------------------------------------------------------------
  // Port budget counters (reset each cycle)
  // -------------------------------------------------------------------------
  logic load_port_used_r;
  logic store_port_used_r;

  // -------------------------------------------------------------------------
  // Store: find matching tag or first free slot
  // -------------------------------------------------------------------------
  logic [ADDR_W-1:0] store_slot;
  logic               store_found;

  always_comb begin
    store_found = 1'b0;
    store_slot  = '0;
    // First pass: find existing tag match
    for (int i = 0; i < DEPTH; i++) begin
      if (valid_r[i] && (tag_r[i] == store_tag_i) && !store_found) begin
        store_found = 1'b1;
        store_slot  = ADDR_W'(i);
      end
    end
    // Second pass: find first free slot if no tag match
    if (!store_found) begin
      for (int i = 0; i < DEPTH; i++) begin
        if (!valid_r[i] && !store_found) begin
          store_found = 1'b1;
          store_slot  = ADDR_W'(i);
        end
      end
    end
  end

  assign store_ack_o = store_req_i & store_found & ~store_port_used_r;

  // -------------------------------------------------------------------------
  // Load: CAM tag match (destructive read)
  // -------------------------------------------------------------------------
  logic [ADDR_W-1:0] load_slot;
  logic               load_found;

  always_comb begin
    load_found = 1'b0;
    load_slot  = '0;
    load_data_o = '0;
    for (int i = 0; i < DEPTH; i++) begin
      if (valid_r[i] && (tag_r[i] == load_tag_i) && !load_found) begin
        load_found  = 1'b1;
        load_slot   = ADDR_W'(i);
        load_data_o = data_r[i];
      end
    end
  end

  assign load_ack_o = load_req_i & ~load_port_used_r;
  assign load_hit_o = load_ack_o & load_found;

  // -------------------------------------------------------------------------
  // Sequential: update storage, port budget
  // -------------------------------------------------------------------------
  always_ff @(posedge clk_i) begin
    if (!rst_n_i || clear_i) begin
      for (int i = 0; i < DEPTH; i++) begin
        valid_r[i] <= 1'b0;
      end
      load_port_used_r  <= 1'b0;
      store_port_used_r <= 1'b0;
    end else begin
      // Reset port budget each cycle
      load_port_used_r  <= 1'b0;
      store_port_used_r <= 1'b0;

      // Store operation
      if (store_ack_o) begin
        valid_r[store_slot] <= 1'b1;
        tag_r[store_slot]   <= store_tag_i;
        data_r[store_slot]  <= store_data_i;
        store_port_used_r   <= 1'b1;
      end

      // Load operation: destructive read (clear valid on hit)
      if (load_ack_o) begin
        load_port_used_r <= 1'b1;
        if (load_found) begin
          valid_r[load_slot] <= 1'b0;
        end
      end
    end
  end

endmodule
