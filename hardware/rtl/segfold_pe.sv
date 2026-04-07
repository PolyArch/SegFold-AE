// =============================================================================
// segfold_pe.sv — Processing Element: 3-state FSM (IDLE / LOAD / MAC)
// =============================================================================
module segfold_pe
  import segfold_pkg::*;
#(
  parameter LOAD_A_CYCLES = NUM_CYCLES_LOAD_A,
  parameter MAC_CYCLES    = NUM_CYCLES_MULT_II,
  parameter STORE_C_CYCLES = NUM_CYCLES_STORE_C
)(
  input  logic              clk_i,
  input  logic              rst_n_i,

  // --- FIFO interface ---
  input  logic              fifo_empty_i,
  input  fifo_entry_t       fifo_data_i,
  output logic              fifo_pop_o,

  // --- SPAD store port ---
  output logic              spad_store_req_o,
  output logic [IDX_W-1:0]  spad_store_tag_o,
  output spad_entry_t       spad_store_data_o,
  input  logic              spad_store_ack_i,

  // --- SPAD load port ---
  output logic              spad_load_req_o,
  output logic [IDX_W-1:0]  spad_load_tag_o,
  input  logic              spad_load_ack_i,
  input  logic              spad_load_hit_i,
  input  spad_entry_t       spad_load_data_i,

  // --- Accumulator output ---
  output logic              acc_wr_en_o,
  output logic [IDX_W-1:0]  acc_wr_row_o,
  output logic [IDX_W-1:0]  acc_wr_col_o,
  output logic [ACC_W-1:0]  acc_wr_data_o,

  // --- Status ---
  output logic              active_o,
  output logic              c_valid_o     // PE holds valid C partial sum
);

  // =========================================================================
  // Registers
  // =========================================================================
  pe_status_e         status_r, status_nxt;

  // A operand
  logic               a_valid_r,  a_valid_nxt;
  logic [DATA_W-1:0]  a_data_r,   a_data_nxt;
  logic [IDX_W-1:0]   a_m_r,      a_m_nxt;

  // B operand
  logic               b_valid_r,  b_valid_nxt;
  logic [DATA_W-1:0]  b_val_r,    b_val_nxt;
  logic [IDX_W-1:0]   b_row_r,    b_row_nxt;
  logic [IDX_W-1:0]   b_col_r,    b_col_nxt;
  logic [IDX_W-1:0]   b_n_r,      b_n_nxt;

  // C accumulator
  logic               c_valid_r,  c_valid_nxt;
  logic [ACC_W-1:0]   c_val_r,    c_val_nxt;
  logic [IDX_W-1:0]   c_m_r,      c_m_nxt;
  logic [IDX_W-1:0]   c_n_r,      c_n_nxt;
  logic [IDX_W-1:0]   c_last_k_r, c_last_k_nxt;
  logic [IDX_W-1:0]   c_col_r,    c_col_nxt;

  // Down-counters
  logic [CNT_LOAD_A_W-1:0] load_a_cnt_r, load_a_cnt_nxt;
  logic [CNT_MAC_W-1:0]    mac_cnt_r,    mac_cnt_nxt;
  logic [CNT_STORE_C_W-1:0] store_c_cnt_r, store_c_cnt_nxt;

  // Control flags
  logic store_c_flag_r, store_c_flag_nxt;
  logic load_c_flag_r,  load_c_flag_nxt;

  // =========================================================================
  // Combinational signals
  // =========================================================================
  logic valid_a;      // A load complete
  logic valid_c;      // C available (no pending store/load)
  logic mac_done;     // MAC counter expired
  logic stall_c;      // C store stall
  logic do_pop;       // Pop FIFO this cycle
  logic is_new_c_store, is_new_c_load;

  // A load done: down-counter reached 0
  assign valid_a = a_valid_r || (status_r == PE_LOAD && load_a_cnt_r == '0);

  // C is available when no pending SPAD operations
  assign valid_c = ~store_c_flag_r & ~load_c_flag_r;

  // MAC done: down-counter reached 0
  assign mac_done = (status_r == PE_MAC) && (mac_cnt_r == '0);

  // C store stall
  assign stall_c = (store_c_cnt_r != '0);

  // Active detection (for completion logic)
  assign active_o = (status_r != PE_IDLE);

  // C valid for cleanup drain
  assign c_valid_o = c_valid_r;

  // =========================================================================
  // is_new_c logic: detect C output column change
  // =========================================================================
  always_comb begin
    is_new_c_store = 1'b0;
    is_new_c_load  = 1'b0;

    if (!c_valid_r) begin
      // First time: no C yet, need to load from SPAD
      is_new_c_store = 1'b0;
      is_new_c_load  = 1'b1;
    end else begin
      // Check if output position changed: (a_m != c_m) || (b_n != c_n)
      if ((a_m_nxt != c_m_r) || (b_n_nxt != c_n_r)) begin
        is_new_c_store = 1'b1;
        is_new_c_load  = 1'b1;    // Load new context from SPAD
      end
    end
  end

  // =========================================================================
  // MAC datapath
  // =========================================================================
  logic signed [ACC_W-1:0] product;
  assign product = ACC_W'(signed'(a_data_r)) * ACC_W'(signed'(b_val_r));

  // =========================================================================
  // Accumulator output
  // =========================================================================
  assign acc_wr_en_o   = mac_done & ~stall_c;
  assign acc_wr_row_o  = c_m_r;
  assign acc_wr_col_o  = c_n_r;
  assign acc_wr_data_o = c_val_r;

  // =========================================================================
  // SPAD store request
  // =========================================================================
  assign spad_store_req_o  = store_c_flag_r;
  assign spad_store_tag_o  = c_col_r;
  assign spad_store_data_o = {c_val_r, c_m_r, c_n_r, c_last_k_r, c_col_r};

  // =========================================================================
  // SPAD load request
  // =========================================================================
  assign spad_load_req_o = load_c_flag_r & ~store_c_flag_r;  // Load after store
  assign spad_load_tag_o = b_col_nxt;

  // =========================================================================
  // FIFO pop
  // =========================================================================
  assign do_pop = !fifo_empty_i &&
                  ((status_r == PE_IDLE) ||
                   (mac_done && !stall_c));
  assign fifo_pop_o = do_pop;

  // =========================================================================
  // Next-state logic
  // =========================================================================
  always_comb begin
    // Default: hold current state
    status_nxt     = status_r;
    a_valid_nxt    = a_valid_r;
    a_data_nxt     = a_data_r;
    a_m_nxt        = a_m_r;
    b_valid_nxt    = b_valid_r;
    b_val_nxt      = b_val_r;
    b_row_nxt      = b_row_r;
    b_col_nxt      = b_col_r;
    b_n_nxt        = b_n_r;
    c_valid_nxt    = c_valid_r;
    c_val_nxt      = c_val_r;
    c_m_nxt        = c_m_r;
    c_n_nxt        = c_n_r;
    c_last_k_nxt   = c_last_k_r;
    c_col_nxt      = c_col_r;
    load_a_cnt_nxt = load_a_cnt_r;
    mac_cnt_nxt    = mac_cnt_r;
    store_c_cnt_nxt = store_c_cnt_r;
    store_c_flag_nxt = store_c_flag_r;
    load_c_flag_nxt  = load_c_flag_r;

    case (status_r)
      // -----------------------------------------------------------------
      PE_IDLE: begin
        if (do_pop) begin
          // Load B from FIFO, set up A load
          b_valid_nxt    = 1'b1;
          b_val_nxt      = fifo_data_i.b_val;
          b_row_nxt      = fifo_data_i.b_row;
          b_col_nxt      = fifo_data_i.b_col;
          b_n_nxt        = fifo_data_i.b_n;
          a_m_nxt        = fifo_data_i.b_row;  // A row = B row (k index)
          a_data_nxt     = DATA_W'(fifo_data_i.b_val); // Simplified: A data from FIFO
          a_valid_nxt    = 1'b0;
          load_a_cnt_nxt = CNT_LOAD_A_W'(LOAD_A_CYCLES);
          status_nxt     = PE_LOAD;

          // Evaluate is_new_c for this new operand pair
          store_c_flag_nxt = is_new_c_store;
          load_c_flag_nxt  = is_new_c_load;
        end
      end

      // -----------------------------------------------------------------
      PE_LOAD: begin
        // Count down A load latency
        if (load_a_cnt_r != '0) begin
          load_a_cnt_nxt = load_a_cnt_r - 1'b1;
        end

        // Mark A valid when counter reaches 0
        if (load_a_cnt_r == '0) begin
          a_valid_nxt = 1'b1;
        end

        // Handle SPAD store (if pending)
        if (store_c_flag_r) begin
          if (spad_store_ack_i) begin
            store_c_flag_nxt = 1'b0;
            // Clear C after successful store
            c_valid_nxt = 1'b0;
            c_val_nxt   = '0;
          end
          // Else: stall, retry next cycle
        end

        // Handle SPAD load (after store completes)
        if (load_c_flag_r && !store_c_flag_r && !store_c_flag_nxt) begin
          if (spad_load_ack_i) begin
            load_c_flag_nxt = 1'b0;
            if (spad_load_hit_i) begin
              // Restore C context from SPAD
              c_valid_nxt  = 1'b1;
              c_val_nxt    = spad_load_data_i.val;
              c_m_nxt      = spad_load_data_i.m;
              c_n_nxt      = spad_load_data_i.n;
              c_last_k_nxt = spad_load_data_i.last_k;
              c_col_nxt    = spad_load_data_i.c_col;
            end else begin
              // SPAD miss: initialize fresh C accumulator
              c_valid_nxt  = 1'b1;
              c_val_nxt    = '0;
              c_m_nxt      = a_m_nxt;
              c_n_nxt      = b_n_nxt;
              c_last_k_nxt = b_row_nxt;
              c_col_nxt    = b_col_nxt;
            end
          end
          // Else: stall, retry next cycle
        end

        // If no pending store/load, update C metadata from current A/B
        if (!store_c_flag_r && !load_c_flag_r &&
            !store_c_flag_nxt && !load_c_flag_nxt) begin
          if (!c_valid_r) begin
            c_valid_nxt  = 1'b1;
            c_val_nxt    = '0;
          end
          c_m_nxt      = a_m_nxt;
          c_n_nxt      = b_n_nxt;
          c_last_k_nxt = b_row_nxt;
          c_col_nxt    = b_col_nxt;
        end

        // Transition to MAC when both A and C are ready
        if (valid_a && valid_c && !store_c_flag_nxt && !load_c_flag_nxt) begin
          // to_mac: compute product, accumulate
          c_val_nxt    = c_val_r + product;
          a_valid_nxt  = 1'b0;
          mac_cnt_nxt  = CNT_MAC_W'(MAC_CYCLES);
          status_nxt   = PE_MAC;
        end
      end

      // -----------------------------------------------------------------
      PE_MAC: begin
        // Count down MAC latency
        if (mac_cnt_r != '0) begin
          mac_cnt_nxt = mac_cnt_r - 1'b1;
        end

        if (mac_done && !stall_c) begin
          // Store C stall counter
          store_c_cnt_nxt = CNT_STORE_C_W'(STORE_C_CYCLES);

          // free_b_val: clear A/B but keep C
          a_valid_nxt = 1'b0;
          b_valid_nxt = 1'b0;

          // MAC-to-LOAD shortcut: skip IDLE if FIFO non-empty
          if (!fifo_empty_i) begin
            b_valid_nxt    = 1'b1;
            b_val_nxt      = fifo_data_i.b_val;
            b_row_nxt      = fifo_data_i.b_row;
            b_col_nxt      = fifo_data_i.b_col;
            b_n_nxt        = fifo_data_i.b_n;
            a_m_nxt        = fifo_data_i.b_row;
            a_data_nxt     = DATA_W'(fifo_data_i.b_val); // Simplified: A data from FIFO
            load_a_cnt_nxt = CNT_LOAD_A_W'(LOAD_A_CYCLES);
            status_nxt     = PE_LOAD;
            store_c_flag_nxt = is_new_c_store;
            load_c_flag_nxt  = is_new_c_load;
          end else begin
            status_nxt = PE_IDLE;
          end
        end

        // Count down store C stall
        if (store_c_cnt_r != '0) begin
          store_c_cnt_nxt = store_c_cnt_r - 1'b1;
        end
      end

      default: begin
        status_nxt = PE_IDLE;
      end
    endcase
  end

  // =========================================================================
  // Register update
  // =========================================================================
  always_ff @(posedge clk_i) begin
    if (!rst_n_i) begin
      status_r       <= PE_IDLE;
      a_valid_r      <= 1'b0;
      a_data_r       <= '0;
      a_m_r          <= '0;
      b_valid_r      <= 1'b0;
      b_val_r        <= '0;
      b_row_r        <= '0;
      b_col_r        <= '0;
      b_n_r          <= '0;
      c_valid_r      <= 1'b0;
      c_val_r        <= '0;
      c_m_r          <= '0;
      c_n_r          <= '0;
      c_last_k_r     <= '0;
      c_col_r        <= '0;
      load_a_cnt_r   <= '0;
      mac_cnt_r      <= '0;
      store_c_cnt_r  <= '0;
      store_c_flag_r <= 1'b0;
      load_c_flag_r  <= 1'b0;
    end else begin
      status_r       <= status_nxt;
      a_valid_r      <= a_valid_nxt;
      a_data_r       <= a_data_nxt;
      a_m_r          <= a_m_nxt;
      b_valid_r      <= b_valid_nxt;
      b_val_r        <= b_val_nxt;
      b_row_r        <= b_row_nxt;
      b_col_r        <= b_col_nxt;
      b_n_r          <= b_n_nxt;
      c_valid_r      <= c_valid_nxt;
      c_val_r        <= c_val_nxt;
      c_m_r          <= c_m_nxt;
      c_n_r          <= c_n_nxt;
      c_last_k_r     <= c_last_k_nxt;
      c_col_r        <= c_col_nxt;
      load_a_cnt_r   <= load_a_cnt_nxt;
      mac_cnt_r      <= mac_cnt_nxt;
      store_c_cnt_r  <= store_c_cnt_nxt;
      store_c_flag_r <= store_c_flag_nxt;
      load_c_flag_r  <= load_c_flag_nxt;
    end
  end

endmodule
