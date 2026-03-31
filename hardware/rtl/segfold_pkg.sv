// =============================================================================
// segfold_pkg.sv — Shared package for SegFold accelerator
// =============================================================================
package segfold_pkg;

  // ---------------------------------------------------------------------------
  // Array dimensions
  // ---------------------------------------------------------------------------
  parameter VROWS          = 16;
  parameter VCOLS          = 16;

  // ---------------------------------------------------------------------------
  // Data widths
  // ---------------------------------------------------------------------------
  parameter DATA_W         = 8;    // A/B element width
  parameter ACC_W          = 32;   // C accumulator width
  parameter IDX_W          = 16;   // Row/column index width
  parameter FIFO_DEPTH     = 16;   // SW-PE FIFO depth
  parameter SPAD_DEPTH     = 16;   // SPAD CAM entries per bank

  // ---------------------------------------------------------------------------
  // Timing parameters (down-counter init values)
  // ---------------------------------------------------------------------------
  parameter NUM_CYCLES_LOAD_A  = 1;
  parameter NUM_CYCLES_LOAD_B  = 1;
  parameter NUM_CYCLES_STORE_C = 0;
  parameter NUM_CYCLES_MULT_II = 1;

  // ---------------------------------------------------------------------------
  // Memory controller parameters
  // ---------------------------------------------------------------------------
  parameter MC_II                   = 2;    // Initiation interval
  parameter B_LOADER_WINDOW_SIZE    = 32;
  parameter B_MEM_DEPTH             = 1024; // B SRAM depth
  parameter B_MAX_ROWS              = 256;  // Max B rows
  parameter SPAD_LOAD_PORTS         = 1;
  parameter SPAD_STORE_PORTS        = 1;

  // ---------------------------------------------------------------------------
  // Forward LUT parameters
  // ---------------------------------------------------------------------------
  parameter LUT_DEPTH    = VCOLS;       // 16 entries per bank
  parameter LUT_KEY_W    = IDX_W;       // 16-bit b_col_dense key
  parameter LUT_VAL_W    = 4;           // clog2(VCOLS), switch position 0..15
  parameter LUT_ADDR_W   = 4;           // clog2(LUT_DEPTH)
  parameter LUT_ENTRY_W  = LUT_KEY_W + LUT_VAL_W;  // 20 bits

  // ---------------------------------------------------------------------------
  // Derived widths (precomputed for DC 2018 compatibility)
  // ---------------------------------------------------------------------------
  parameter FIFO_ADDR_W    = 4;    // clog2(FIFO_DEPTH)
  parameter FIFO_PTR_W     = 5;    // FIFO_ADDR_W + 1 for full/empty
  parameter SPAD_ADDR_W    = 4;    // clog2(SPAD_DEPTH)
  parameter CNT_LOAD_A_W   = 1;    // clog2(NUM_CYCLES_LOAD_A+1), min 1
  parameter CNT_LOAD_B_W   = 1;    // clog2(NUM_CYCLES_LOAD_B+1), min 1
  parameter CNT_MAC_W      = 1;    // clog2(NUM_CYCLES_MULT_II+1), min 1
  parameter CNT_STORE_C_W  = 1;    // clog2(NUM_CYCLES_STORE_C+1), min 1
  parameter MC_CNT_W       = 2;    // clog2(MC_II+1)
  parameter B_ROW_ADDR_W   = 8;    // clog2(B_MAX_ROWS)
  parameter B_MEM_ADDR_W   = 10;   // clog2(B_MEM_DEPTH)
  parameter WIN_ADDR_W     = 5;    // clog2(B_LOADER_WINDOW_SIZE)

  // ---------------------------------------------------------------------------
  // FIFO entry (SW-PE FIFO data)
  // ---------------------------------------------------------------------------
  typedef struct packed {
    logic [DATA_W-1:0]  b_val;
    logic [IDX_W-1:0]   b_row;
    logic [IDX_W-1:0]   b_col;
    logic [IDX_W-1:0]   b_n;
  } fifo_entry_t;

  parameter FIFO_ENTRY_W = DATA_W + 3*IDX_W;

  // ---------------------------------------------------------------------------
  // SPAD entry (C partial-sum context)
  // ---------------------------------------------------------------------------
  typedef struct packed {
    logic [ACC_W-1:0]   val;
    logic [IDX_W-1:0]   m;
    logic [IDX_W-1:0]   n;
    logic [IDX_W-1:0]   last_k;
    logic [IDX_W-1:0]   c_col;
  } spad_entry_t;

  parameter SPAD_ENTRY_W = ACC_W + 4*IDX_W;

  // ---------------------------------------------------------------------------
  // B element (from memory controller to switches)
  // ---------------------------------------------------------------------------
  typedef struct packed {
    logic [DATA_W-1:0]  val;
    logic [IDX_W-1:0]   row;
    logic [IDX_W-1:0]   col;
    logic [IDX_W-1:0]   n;
  } b_element_t;

  parameter B_ELEMENT_W = DATA_W + 3*IDX_W;

  // ---------------------------------------------------------------------------
  // PE FSM states
  // ---------------------------------------------------------------------------
  typedef enum logic [1:0] {
    PE_IDLE = 2'b00,
    PE_LOAD = 2'b01,
    PE_MAC  = 2'b10
  } pe_status_e;

  // ---------------------------------------------------------------------------
  // Switch FSM states
  // ---------------------------------------------------------------------------
  typedef enum logic [1:0] {
    SW_IDLE   = 2'b00,
    SW_LOAD_B = 2'b01,
    SW_MOVE   = 2'b10
  } sw_status_e;

  // ---------------------------------------------------------------------------
  // Top-level FSM states
  // ---------------------------------------------------------------------------
  typedef enum logic [1:0] {
    TOP_IDLE    = 2'b00,
    TOP_COMPUTE = 2'b01,
    TOP_CLEANUP = 2'b10,
    TOP_DONE    = 2'b11
  } top_status_e;

endpackage
