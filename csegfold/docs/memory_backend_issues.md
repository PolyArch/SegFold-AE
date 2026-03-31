# Memory Backend Implementation Issues

This document tracks issues found in the memory backend implementation during code review.

## Issue 1: B elements going to FIFO don't generate memory requests

**Location:** `src/simulator/memoryController.cpp`, lines 84-89

**Problem:**
```cpp
if (switchModule->cfg.enable_memory_hierarchy &&
    next_switch.status == SwitchStatus::LOAD_B) {  // ← Only if switch status is LOAD_B
    auto [row_idx, col_idx] = controller->matrix->B_indexed.get_original_coords(r, c);
    int c_col = next_switch.b.col.value();
    controller->submit_load_request(row_idx, col_idx, "B", i, j, "switch", c_col);
}
```

When B elements go to the FIFO (lines 70-75), the switch status isn't set to `LOAD_B`, so **no memory request is generated for FIFO-queued B elements**.

**Verification:**
1. Direct load path (line 67): `update_switch_with_b_data()` sets `next_switch.status = LOAD_B` → request submitted
2. FIFO path (lines 70-78): Element enqueued, but status unchanged → NO request submitted
3. Later, `dequeue_to_switch()` sets status to `LOAD_B` but does NOT call `submit_load_request()`

**Impact:** Memory statistics undercount B accesses when FIFO is enabled.

**Note:** This affects the **B loader FIFO** (`enable_b_loader_fifo`), NOT the switch-to-PE FIFO (`enable_sw_pe_fifo`). The B loader FIFO is disabled in the current test config, so this doesn't affect existing test results.

**Fix:** Submit memory request when:
- Option A: At FIFO enqueue time (need to store `orig_row` in FIFO entry, only `orig_col` is currently stored as `b_n`)
- Option B: At dequeue time in `drain_b_loader_fifos()` after calling `dequeue_to_switch()` (requires passing `controller` to compute coords)

---

## Issue 2: B element address calculation has swapped coordinates

**Location:** `src/modules/memoryController.cpp`, lines 266-268

**Problem:**
```cpp
} else if (type == "b" || type == "B") {
    address = get_b_element_pointer(k, m);  // Passes (k, m)
```

But `get_b_element_pointer` expects `(k, n)`:
```cpp
int MemoryController::get_b_element_pointer(int k, int n) const {
    int offset = matrix->B_nnz_offset(k, n);
```

The call site in `load_b_element_to_switch()` passes:
```cpp
controller->submit_load_request(row_idx, col_idx, "B", i, j, "switch", c_col);
```

Where `row_idx, col_idx` come from `B_indexed.get_original_coords(r, c)`, which returns `(k, n)`.

In `submit_load_request()`, these become `m=row_idx=k` and `k=col_idx=n`, then:
- `get_b_element_pointer(k, m)` = `get_b_element_pointer(n, k)` - **WRONG ORDER**

**Impact:** Incorrect address calculation for B elements, leading to wrong cache behavior.

**Fix:** Change line 267 to `get_b_element_pointer(m, k)` or rename parameters for clarity.

---

## Issue 3: No request deduplication/filtering

**Location:** `src/modules/memoryController.cpp`, `submit_load_request()`

**Problem:** Every element access generates a new request with a unique `req_id`. There's no:
- Coalescing of requests to the same cache line
- Filtering of redundant requests to the same address
- Outstanding request tracking to avoid duplicates
- MSHR (Miss Status Handling Register) behavior

**Impact:**
- Inflated request counts
- Unrealistic memory behavior (real systems coalesce requests)
- Cache statistics may not reflect actual memory traffic

**Fix:** Add request filtering based on:
1. Cache line address (coalesce accesses to same line)
2. Outstanding request tracking (don't re-request pending addresses)

---

## Issue 4: Data flow is decoupled from memory timing

**Location:** `src/simulator/simulator.cpp`, lines 205-238

**Problem:** The actual data is read immediately from matrix structures:
```cpp
// A data read immediately from matrix (line 207)
val = controller->matrix->A_orig(orig_idx_a.first, orig_idx_a.second);

// ... data is already available ...

// THEN memory request submitted (line 238)
if (controller->cfg.enable_memory_hierarchy) {
    controller->submit_load_request(orig_idx_a.first, orig_idx_a.second, "A", i, j, "pe", c_col);
}
```

Similarly for B data in `load_b_element_to_switch()`:
```cpp
// B data already available from B_csr[r][b_col]
int v = controller->B_csr.at(r)[b_col].second;  // Data read here
// ...
controller->submit_load_request(...);  // Request submitted after
```

**Impact:** Memory requests are for latency tracking only, not gating data availability. The simulation is correct but the model doesn't truly simulate memory-dependent data flow.

**Note:** This is by design for the current implementation. True memory-gated data flow would require significant restructuring.

---

## Issue 5: Confusing parameter naming in submit_load_request

**Location:** `src/modules/memoryController.cpp`, line 253

**Problem:**
```cpp
void MemoryController::submit_load_request(int m, int k, const std::string& type, ...)
```

Parameters are named `(m, k)` but:
- For A loads: called with `(m, k)` - correct naming
- For B loads: called with `(row_idx, col_idx)` which is `(k, n)` - misleading

**Impact:** Code is confusing and error-prone.

**Fix:** Rename parameters to be more generic like `(coord1, coord2)` or add separate functions for each matrix type.

---

## Summary

| Issue | Severity | Impact |
|-------|----------|--------|
| 1. FIFO B elements no request | Medium | Undercounted B accesses |
| 2. Swapped B coordinates | High | Wrong cache addresses |
| 3. No request deduplication | Medium | Inflated statistics |
| 4. Decoupled data flow | Low | By design, documented |
| 5. Confusing naming | Low | Maintainability |

## Test Results (for reference)

With memory hierarchy enabled:
- Baseline (no memory): 525 cycles
- With memory hierarchy: 1072 cycles (~2x overhead)
- L1 hits: 2199 (98.7%)
- L1 misses: 29
- DRAM accesses: 29
- Avg memory latency: 4.52 cycles

The 2x overhead is reasonable given the 1-cycle minimum latency for L1 hits in a pipelined memory model.
