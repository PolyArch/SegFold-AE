/*
 * C FFI interface for SegFold's DRAM memory backend.
 *
 * Exposes the Ramulator2-based (or fallback) memory backend for use
 * from non-C++ simulators (e.g., spada-sim via Rust FFI).
 * L1/L2 caches are bypassed (size_kb=0) so the caller can use its own
 * cache hierarchy — only the DRAM timing layer is active.
 */

#ifndef CSEGFOLD_FFI_H
#define CSEGFOLD_FFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to the DRAM backend */
typedef struct DramBackendHandle DramBackendHandle;

/* Configuration for creating a DRAM backend */
typedef struct {
    const char* backend_type;      /* "ideal" or "ramulator2" */
    const char* dram_config_file;  /* Path to DRAM YAML config (for ramulator2) */
    int fallback_latency;          /* Fixed DRAM latency when ramulator2 unavailable */
} DramBackendConfig;

/* Memory request submitted to the DRAM backend */
typedef struct {
    uint64_t req_id;
    uint64_t address;
    int is_write;       /* 0 = load, 1 = store */
    int pe_id;          /* Processing element ID (for routing) */
    uint64_t tag;       /* Opaque tag passed back in response (e.g., encoded a_loc) */
} DramRequest;

/* Memory response returned from tick() */
typedef struct {
    uint64_t req_id;
    uint64_t latency;   /* Total latency in cycles */
    uint64_t tag;       /* Echoed from request */
} DramResponse;

/*
 * Create a DRAM backend with L1/L2 caches bypassed (size=0).
 * Returns NULL on failure.
 */
DramBackendHandle* dram_backend_create(const DramBackendConfig* config);

/* Destroy a DRAM backend and free resources. */
void dram_backend_destroy(DramBackendHandle* handle);

/*
 * Submit a memory request.
 * Returns 1 if accepted, 0 if rejected (backpressure — queue full).
 */
int dram_submit_request(DramBackendHandle* handle, const DramRequest* req);

/*
 * Advance the backend by one cycle.
 * Fills `responses` with up to `max_responses` completed requests.
 * Returns the number of completed responses this cycle.
 */
int dram_tick(DramBackendHandle* handle, DramResponse* responses, int max_responses);

/* Check if the backend can accept more requests. Returns 1 if yes. */
int dram_can_accept(const DramBackendHandle* handle);

/* Get the current cycle count of the backend. */
uint64_t dram_get_cycle(const DramBackendHandle* handle);

#ifdef __cplusplus
}
#endif

#endif /* CSEGFOLD_FFI_H */
