// The script runtime's garbage-collected heap driven directly, without an
// interpreter, for tests/sdk/test_script_memory.cpp, which compares where objects
// are placed with a model of first-fit allocation. MicroPython keeps its state in
// globals, so these may only be used while no interpreter lives.

#include "py/gc.h"
#include "py/mpstate.h"

#include <stdbool.h>
#include <stddef.h>

void gc_probe_init(void *heap, size_t bytes) {
    gc_init(heap, (char *)heap + bytes);
    // The probe frees what it no longer uses itself: never collect.
    MP_STATE_MEM(gc_auto_collect_enabled) = 0;
}

// The allocator's index of free runs, in memory the caller provides (or none).
size_t gc_probe_index_bytes(void) {
    return gc_run_index_size();
}

void gc_probe_use_index(void *mem) {
    gc_run_index_init(mem);
}

size_t gc_probe_block_bytes(void) {
    return MICROPY_BYTES_PER_GC_BLOCK;
}

size_t gc_probe_blocks(void) {
    return (size_t)(MP_STATE_MEM(area).gc_pool_end - MP_STATE_MEM(area).gc_pool_start) / MICROPY_BYTES_PER_GC_BLOCK;
}

// The block where `p` starts.
size_t gc_probe_block_of(const void *p) {
    return (size_t)((const byte *)p - MP_STATE_MEM(area).gc_pool_start) / MICROPY_BYTES_PER_GC_BLOCK;
}

void *gc_probe_alloc(size_t bytes) {
    return gc_alloc(bytes, 0);
}

void *gc_probe_realloc(void *p, size_t bytes) {
    return gc_realloc(p, bytes, true);
}

void gc_probe_free(void *p) {
    gc_free(p);
}
