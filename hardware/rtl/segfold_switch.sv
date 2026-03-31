// =============================================================================
// segfold_switch.sv — Switch cell: 3-state FSM (IDLE / LOAD_B / MOVE)
// =============================================================================
module segfold_switch
  import segfold_pkg::*;
#(
  parameter LOAD_B_CYCLES = NUM_CYCLES_LOAD_B,
  parameter MAX_PUSH      = 1
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- B element input (from memory controller) ---
  input  logic              b_load_valid_i,
  input  b_element_t        b_load_data_i,

  // --- B from left neighbor (move right) ---
  input  logic              b_from_left_valid_i,
  input  b_element_t        b_from_left_data_i,

  // --- Move-right output to right neighbor ---
  output logic              move_right_valid_o,
  output b_element_t        move_right_data_o,

  // --- FIFO write output (to SW-PE FIFO) ---
  output logic              fifo_wr_valid_o,
  output fifo_entry_t       fifo_wr_data_o,
  input  logic              fifo_full_i,

  // --- c_col push-right chain ---
  input  logic              c_col_push_valid_i,   // push from left
  input  logic [IDX_W-1:0]  c_col_push_data_i,
  output logic              c_col_push_valid_o,   // push to right
  output logic [IDX_W-1:0]  c_col_push_data_o,

  // --- Neighbor status ---
  input  logic              next_sw_idle_i,       // right neighbor is idle
  input  logic              is_last_col_i,        // no right neighbor

  // --- Status ---
  output sw_status_e        status_o,
  output logic              c_col_valid_o,
  output logic [IDX_W-1:0]  c_col_data_o,
  output logic              active_o
);

  // =========================================================================
  // Registers
  // =========================================================================
  sw_status_e          status_r,   status_nxt;

  // B element registers
  logic                b_valid_r,  b_valid_nxt;
  logic [DATA_W-1:0]  b_val_r,    b_val_nxt;
  logic [IDX_W-1:0]   b_row_r,    b_row_nxt;
  logic [IDX_W-1:0]   b_col_r,    b_col_nxt;
  logic [IDX_W-1:0]   b_n_r,      b_n_nxt;

  // c_col register (persists across B elements)
  logic                c_col_valid_r, c_col_valid_nxt;
  logic [IDX_W-1:0]   c_col_data_r,  c_col_data_nxt;

  // Down-counter for LOAD_B
  logic [CNT_LOAD_B_W-1:0] load_b_cnt_r, load_b_cnt_nxt;

  // =========================================================================
  // Combinational: column comparators
  // =========================================================================
  logic c_eq_b, c_lt_b, c_gt_b, b_zero, load_done;

  assign c_eq_b    = b_valid_r & c_col_valid_r & (b_col_r == c_col_data_r);
  assign c_lt_b    = b_valid_r & c_col_valid_r & (b_col_r > c_col_data_r);
  assign c_gt_b    = b_valid_r & c_col_valid_r & (b_col_r < c_col_data_r);
  assign b_zero    = !b_valid_r || (b_val_r == '0);
  assign load_done = (status_r == SW_LOAD_B) && (load_b_cnt_r == '0);

  // =========================================================================
  // Status outputs
  // =========================================================================
  assign status_o     = status_r;
  assign c_col_valid_o = c_col_valid_r;
  assign c_col_data_o  = c_col_data_r;
  assign active_o      = (status_r != SW_IDLE) & b_valid_r & (b_val_r != '0);

  // =========================================================================
  // FIFO write (on c_eq_b in MOVE state)
  // =========================================================================
  logic fifo_wr_fire;
  assign fifo_wr_fire   = (status_r == SW_MOVE) & c_eq_b & ~fifo_full_i;
  assign fifo_wr_valid_o = fifo_wr_fire;
  assign fifo_wr_data_o  = {b_val_r, b_row_r, b_col_r, b_n_r};

  // =========================================================================
  // Move-right output (on c_lt_b in MOVE state)
  // =========================================================================
  logic move_right_fire;
  assign move_right_fire    = (status_r == SW_MOVE) & c_lt_b &
                               ~is_last_col_i & next_sw_idle_i;
  assign move_right_valid_o = move_right_fire;
  assign move_right_data_o  = {b_val_r, b_row_r, b_col_r, b_n_r};

  // =========================================================================
  // c_col push-right chain (MAX_PUSH = 1 per cycle)
  // All driven from always_comb to avoid multi-driver conflict.
  // =========================================================================
  logic do_push;
  assign do_push = (status_r == SW_MOVE || status_r == SW_LOAD_B) &
                    c_gt_b & c_col_valid_r;

  always_comb begin
    if (do_push) begin
      // We push our old c_col to the right
      c_col_push_valid_o = 1'b1;
      c_col_push_data_o  = c_col_data_r;
    end else if (c_col_push_valid_i) begin
      // Pass through push from left (we absorb it by updating our c_col)
      c_col_push_valid_o = 1'b0;  // Absorbed here
      c_col_push_data_o  = '0;
    end else begin
      c_col_push_valid_o = 1'b0;
      c_col_push_data_o  = '0;
    end
  end

  // =========================================================================
  // Next-state logic
  // =========================================================================
  always_comb begin
    // Defaults: hold
    status_nxt      = status_r;
    b_valid_nxt     = b_valid_r;
    b_val_nxt       = b_val_r;
    b_row_nxt       = b_row_r;
    b_col_nxt       = b_col_r;
    b_n_nxt         = b_n_r;
    c_col_valid_nxt = c_col_valid_r;
    c_col_data_nxt  = c_col_data_r;
    load_b_cnt_nxt  = load_b_cnt_r;

    // Accept c_col push from left
    if (c_col_push_valid_i && !do_push) begin
      c_col_valid_nxt = 1'b1;
      c_col_data_nxt  = c_col_push_data_i;
    end

    case (status_r)
      // -----------------------------------------------------------------
      SW_IDLE: begin
        // Accept B from memory controller (direct load)
        if (b_load_valid_i) begin
          b_valid_nxt    = 1'b1;
          b_val_nxt      = b_load_data_i.val;
          b_row_nxt      = b_load_data_i.row;
          b_col_nxt      = b_load_data_i.col;
          b_n_nxt        = b_load_data_i.n;
          load_b_cnt_nxt = CNT_LOAD_B_W'(LOAD_B_CYCLES);
          status_nxt     = SW_LOAD_B;
        end
        // Accept B from left neighbor (move right)
        else if (b_from_left_valid_i) begin
          b_valid_nxt = 1'b1;
          b_val_nxt   = b_from_left_data_i.val;
          b_row_nxt   = b_from_left_data_i.row;
          b_col_nxt   = b_from_left_data_i.col;
          b_n_nxt     = b_from_left_data_i.n;
          // Moved elements enter MOVE directly, skip LOAD_B
          status_nxt  = SW_MOVE;
        end
      end

      // -----------------------------------------------------------------
      SW_LOAD_B: begin
        // Assign c_col if not yet assigned
        if (!c_col_valid_r) begin
          c_col_valid_nxt = 1'b1;
          c_col_data_nxt  = b_col_r;
        end
        // c_gt_b: push c_col right, take B's col
        else if (c_gt_b) begin
          c_col_data_nxt = b_col_r;
        end

        // Count down
        if (load_b_cnt_r != '0) begin
          load_b_cnt_nxt = load_b_cnt_r - 1'b1;
        end

        // Transition when done
        if (load_done) begin
          if (b_zero) begin
            // Zero B: skip, return to IDLE (preserve c_col)
            b_valid_nxt = 1'b0;
            status_nxt  = SW_IDLE;
          end else begin
            status_nxt = SW_MOVE;
          end
        end
      end

      // -----------------------------------------------------------------
      SW_MOVE: begin
        // Assign c_col if not yet assigned
        if (!c_col_valid_r) begin
          c_col_valid_nxt = 1'b1;
          c_col_data_nxt  = b_col_r;
        end
        // c_gt_b: push c_col right, take B's col
        else if (c_gt_b) begin
          c_col_data_nxt = b_col_r;
        end

        // Routing decision
        if (c_eq_b) begin
          if (!fifo_full_i) begin
            // Send to FIFO, go IDLE (preserve c_col)
            b_valid_nxt = 1'b0;
            status_nxt  = SW_IDLE;
          end
          // Else: stall in MOVE (FIFO full)
        end
        else if (c_lt_b) begin
          if (!is_last_col_i && next_sw_idle_i) begin
            // Move B right, go IDLE (preserve c_col)
            b_valid_nxt = 1'b0;
            status_nxt  = SW_IDLE;
          end
          // Else: stall in MOVE (next switch busy or last col)
        end
        // c_gt_b: push already handled above, stay in MOVE
      end

      default: begin
        status_nxt = SW_IDLE;
      end
    endcase
  end

  // =========================================================================
  // Register update
  // =========================================================================
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      status_r      <= SW_IDLE;
      b_valid_r     <= 1'b0;
      b_val_r       <= '0;
      b_row_r       <= '0;
      b_col_r       <= '0;
      b_n_r         <= '0;
      c_col_valid_r <= 1'b0;
      c_col_data_r  <= '0;
      load_b_cnt_r  <= '0;
    end else begin
      status_r      <= status_nxt;
      b_valid_r     <= b_valid_nxt;
      b_val_r       <= b_val_nxt;
      b_row_r       <= b_row_nxt;
      b_col_r       <= b_col_nxt;
      b_n_r         <= b_n_nxt;
      c_col_valid_r <= c_col_valid_nxt;
      c_col_data_r  <= c_col_data_nxt;
      load_b_cnt_r  <= load_b_cnt_nxt;
    end
  end

endmodule
