/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2014 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "py/gc.h"
#include "py/runtime.h"

#if MICROPY_DEBUG_VALGRIND
#include <valgrind/memcheck.h>
#endif

#if MICROPY_ENABLE_GC

#if MICROPY_DEBUG_VERBOSE // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

// make this 1 to dump the heap each time it changes
#define EXTENSIVE_HEAP_PROFILING (0)

// make this 1 to zero out swept memory to more eagerly
// detect untraced object still in use
#define CLEAR_ON_SWEEP (0)

#define WORDS_PER_BLOCK ((MICROPY_BYTES_PER_GC_BLOCK) / MP_BYTES_PER_OBJ_WORD)
#define BYTES_PER_BLOCK (MICROPY_BYTES_PER_GC_BLOCK)

// ATB = allocation table byte
// 0b00 = FREE -- free block
// 0b01 = HEAD -- head of a chain of blocks
// 0b10 = TAIL -- in the tail of a chain of blocks
// 0b11 = MARK -- marked head block

#define AT_FREE (0)
#define AT_HEAD (1)
#define AT_TAIL (2)
#define AT_MARK (3)

#define BLOCKS_PER_ATB (4)
#define ATB_MASK_0 (0x03)
#define ATB_MASK_1 (0x0c)
#define ATB_MASK_2 (0x30)
#define ATB_MASK_3 (0xc0)

#define ATB_0_IS_FREE(a) (((a) & ATB_MASK_0) == 0)
#define ATB_1_IS_FREE(a) (((a) & ATB_MASK_1) == 0)
#define ATB_2_IS_FREE(a) (((a) & ATB_MASK_2) == 0)
#define ATB_3_IS_FREE(a) (((a) & ATB_MASK_3) == 0)

#if MICROPY_GC_SPLIT_HEAP
#define NEXT_AREA(area) ((area)->next)
#else
#define NEXT_AREA(area) (NULL)
#endif

#define BLOCK_SHIFT(block) (2 * ((block) & (BLOCKS_PER_ATB - 1)))
#define ATB_GET_KIND(area, block) (((area)->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] >> BLOCK_SHIFT(block)) & 3)
#define ATB_ANY_TO_FREE(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] &= (~(AT_MARK << BLOCK_SHIFT(block))); } while (0)
#define ATB_FREE_TO_HEAD(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_HEAD << BLOCK_SHIFT(block)); } while (0)
#define ATB_FREE_TO_TAIL(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_TAIL << BLOCK_SHIFT(block)); } while (0)
#define ATB_HEAD_TO_MARK(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_MARK << BLOCK_SHIFT(block)); } while (0)
#define ATB_MARK_TO_HEAD(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] &= (~(AT_TAIL << BLOCK_SHIFT(block))); } while (0)

#define BLOCK_FROM_PTR(area, ptr) (((byte *)(ptr) - area->gc_pool_start) / BYTES_PER_BLOCK)
#define PTR_FROM_BLOCK(area, block) (((block) * BYTES_PER_BLOCK + (uintptr_t)area->gc_pool_start))

// After the ATB, there must be a byte filled with AT_FREE so that gc_mark_tree
// cannot erroneously conclude that a block extends past the end of the GC heap
// due to bit patterns in the FTB (or first block, if finalizers are disabled)
// being interpreted as AT_TAIL.
#define ALLOC_TABLE_GAP_BYTE (1)

#if MICROPY_ENABLE_FINALISER
// FTB = finaliser table byte
// if set, then the corresponding block may have a finaliser
#define BLOCKS_PER_FTB (8)
#define FTB_GET(area, block) ((area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] >> ((block) & 7)) & 1)
#define FTB_SET(area, block) do { area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] |= (1 << ((block) & 7)); } while (0)
#define FTB_CLEAR(area, block) do { area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] &= (~(1 << ((block) & 7))); } while (0)
#endif

#if MICROPY_PY_WEAKREF
// WTB = weakref table byte
// if set, then the corresponding block may have a weakref in MP_STATE_VM(mp_weakref_map).
#define BLOCKS_PER_WTB (8)
#define WTB_GET(area, block) ((area->gc_weakref_table_start[(block) / BLOCKS_PER_WTB] >> ((block) & 7)) & 1)
#define WTB_SET(area, block) do { area->gc_weakref_table_start[(block) / BLOCKS_PER_WTB] |= (1 << ((block) & 7)); } while (0)
#define WTB_CLEAR(area, block) do { area->gc_weakref_table_start[(block) / BLOCKS_PER_WTB] &= (~(1 << ((block) & 7))); } while (0)
#endif

#if MICROPY_PY_THREAD && !MICROPY_PY_THREAD_GIL
#define GC_MUTEX_INIT() mp_thread_recursive_mutex_init(&MP_STATE_MEM(gc_mutex))
#define GC_ENTER() mp_thread_recursive_mutex_lock(&MP_STATE_MEM(gc_mutex), 1)
#define GC_EXIT() mp_thread_recursive_mutex_unlock(&MP_STATE_MEM(gc_mutex))
#else
// Either no threading, or assume callers to gc_collect() hold the GIL
#define GC_MUTEX_INIT()
#define GC_ENTER()
#define GC_EXIT()
#endif

#if MICROPY_GC_ALLOC_HINTS
#if MICROPY_GC_SPLIT_HEAP
#error "MICROPY_GC_ALLOC_HINTS needs a heap of one area"
#endif

// Allocation is first fit: n blocks go to the start of the lowest run of at least n
// free blocks. Searching from the bottom of the heap each time would read every
// block in use below that run, again and again, so the search starts at a hint.
// Requests fall into size classes: class c serves gc_hint_size[c] blocks or more
// (up to the next class's size), and no run of gc_hint_size[c] free blocks starts
// below block gc_alloc_hint[c]. Nor does a longer one, and no free block at all is
// below the hint of class 0, so a search for class c starts at the higher of those
// two hints and finds the run a search from the bottom would. An allocation moves
// up the hint of the class it searched (and that of class 0 when it took the blocks
// there), freeing moves hints down, and a collection puts them all at the bottom.
// (Without hints only allocations of one block move the start of the search up, and
// a heap whose objects take two blocks or more is searched from its first gap every
// time.)
//
// Most searches end within a few steps of their hint. The others, when the port has
// given the heap an index of its free runs (gc_run_index_init), continue through the
// index, which leads them past the parts of the heap that hold no run long enough:
// runs a little shorter than the request that hold its class's hint back, and the
// part between a freed gap that a request just filled and the next free run.
static const uint16_t gc_hint_size[MP_GC_ALLOC_HINT_CLASSES] = {
    1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 20, 24, 32, 48, 64,
};

#define GC_HINT_NONE ((size_t)-1)

// Steps (an ATB byte, or a word of ATB bytes in use) a search takes from its hint
// before it continues through the index.
#define GC_HINT_STEPS (16)

// The index of free runs: a binary tree over leaves of GC_RUN_LEAF blocks. For the
// blocks under it, each node knows how long the free run at their start (pre), the
// one at their end (suf) and the longest (max) may be. Node 1 is the root, the
// children of node i are 2i and 2i+1, and the leaves are nodes P to 2P-1, P being a
// power of two; blocks past the end of the heap count as in use. The lengths are
// upper bounds, stored as how many blocks short of the node's length they are, so
// that zeroed memory promises every block free: allocating leaves them as they are,
// freeing raises them, a search lowers those it finds too high to what is there,
// and a collection zeroes them.
#define GC_RUN_LEAF (128)

typedef struct _gc_runs_t {
    uint32_t pre, suf, max;
} gc_runs_t;

static size_t gc_blocks(const mp_state_mem_area_t *area) {
    return area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
}

// Whether all four blocks of an ATB byte are in use.
#define ATB_ALL_USED(a) ((((a) | ((a) >> 1)) & 0x55) == 0x55)

// Searches a leaf for the end of a run of n_blocks free blocks, *carry free blocks
// being just below it: returns the run's start, or GC_HINT_NONE and then sets *carry
// to the free blocks at the leaf's end and *runs to the leaf's runs, exactly.
static size_t gc_leaf_search(const mp_state_mem_area_t *area, size_t leaf, size_t n_blocks, size_t *carry, gc_runs_t *runs) {
    const size_t first = leaf * GC_RUN_LEAF;
    const size_t end = MIN(first + GC_RUN_LEAF, gc_blocks(area));
    size_t c = *carry;
    size_t run = 0, pre = 0, longest = 0;
    bool at_start = true;
    for (size_t b = first; b < end; b += BLOCKS_PER_ATB) {
        byte a = area->gc_alloc_table_start[b / BLOCKS_PER_ATB];
        if (a == 0) {
            if (c + BLOCKS_PER_ATB >= n_blocks) {
                return b - c;
            }
            run += BLOCKS_PER_ATB;
            c += BLOCKS_PER_ATB;
            continue;
        }
        for (size_t k = 0; k < BLOCKS_PER_ATB; k++, a >>= 2) {
            if ((a & 3) == AT_FREE) {
                if (++c >= n_blocks) {
                    return b + k + 1 - n_blocks;
                }
                run++;
            } else {
                if (at_start) {
                    pre = run;
                    at_start = false;
                }
                longest = MAX(longest, run);
                run = 0;
                c = 0;
            }
        }
    }
    if (end < first + GC_RUN_LEAF) {
        // past the end of the heap
        if (at_start) {
            pre = run;
            at_start = false;
        }
        longest = MAX(longest, run);
        run = 0;
        c = 0;
    }
    if (at_start) {
        pre = run;
    }
    longest = MAX(longest, run);
    *carry = c;
    runs->pre = (uint32_t)(GC_RUN_LEAF - pre);
    runs->suf = (uint32_t)(GC_RUN_LEAF - run);
    runs->max = (uint32_t)(GC_RUN_LEAF - longest);
    return GC_HINT_NONE;
}

// The runs of a node from those of its children, each of `half` blocks.
static gc_runs_t gc_runs_join(gc_runs_t l, gc_runs_t r, uint32_t half) {
    // in lengths: pre = l.pre, or all of l and r.pre; suf likewise; max = the
    // longest of l.max, r.max and l.suf + r.pre
    gc_runs_t j;
    j.pre = l.pre != 0 ? l.pre + half : r.pre;
    j.suf = r.suf != 0 ? r.suf + half : l.suf;
    j.max = MIN(MIN(l.max, r.max) + half, l.suf + r.pre);
    return j;
}

static size_t gc_run_index_leaves(const mp_state_mem_area_t *area) {
    size_t p = 1;
    while (p * GC_RUN_LEAF < gc_blocks(area)) {
        p *= 2;
    }
    return p;
}

size_t gc_run_index_size(void) {
    const mp_state_mem_area_t *area = &MP_STATE_MEM(area);
    if (gc_blocks(area) >= ((size_t)1 << 30)) {
        // too many blocks for the index's numbers
        return 0;
    }
    return 2 * gc_run_index_leaves(area) * sizeof(gc_runs_t);
}

void gc_run_index_init(void *mem) {
    // zeroed: every block may be free
    mp_state_mem_area_t *area = &MP_STATE_MEM(area);
    area->gc_run_index = mem != NULL && gc_run_index_size() != 0 ? mem : NULL;
    area->gc_run_index_leaves = gc_run_index_leaves(area);
}

// After the blocks [block, block + n_blocks) became free, part of the free run
// [start, end): raises what the leaves under those blocks promise, and the nodes
// above them as long as they promise more.
static void gc_run_index_freed(mp_state_mem_area_t *area, size_t block, size_t n_blocks, size_t start, size_t end) {
    gc_runs_t *t = area->gc_run_index;
    const size_t p = area->gc_run_index_leaves;
    for (size_t leaf = block / GC_RUN_LEAF; leaf <= (block + n_blocks - 1) / GC_RUN_LEAF; leaf++) {
        const size_t lo = MAX(start, leaf * GC_RUN_LEAF);
        const size_t hi = MIN(end, leaf * GC_RUN_LEAF + GC_RUN_LEAF);
        const uint32_t short_by = (uint32_t)(GC_RUN_LEAF - (hi - lo));
        size_t i = p + leaf;
        gc_runs_t r = t[i];
        r.max = MIN(r.max, short_by);
        if (lo == leaf * GC_RUN_LEAF) {
            r.pre = MIN(r.pre, short_by);
        }
        if (hi == leaf * GC_RUN_LEAF + GC_RUN_LEAF) {
            r.suf = MIN(r.suf, short_by);
        }
        uint32_t half = GC_RUN_LEAF;
        while (r.pre != t[i].pre || r.suf != t[i].suf || r.max != t[i].max) {
            t[i] = r;
            if (i == 1) {
                break;
            }
            i /= 2;
            r = gc_runs_join(t[2 * i], t[2 * i + 1], half);
            half *= 2;
        }
    }
}

// The number of free blocks just below `block`, up to `limit`.
static size_t gc_free_below(const mp_state_mem_area_t *area, size_t block, size_t limit) {
    size_t n = 0;
    while (n < limit && block > n) {
        const size_t b = block - n;
        if (b % BLOCKS_PER_ATB == 0 && area->gc_alloc_table_start[b / BLOCKS_PER_ATB - 1] == 0) {
            n += BLOCKS_PER_ATB;
        } else if (ATB_GET_KIND(area, b - 1) == AT_FREE) {
            n++;
        } else {
            break;
        }
    }
    return MIN(n, limit);
}

// The start of the first run of n_blocks free blocks that ends in leaf `leaf` or
// above it (none ends below it), or GC_HINT_NONE. It walks the tree from that leaf
// to the right, going down only into nodes that may hold the end of such a run.
static size_t gc_run_index_find(mp_state_mem_area_t *area, size_t n_blocks, size_t leaf) {
    gc_runs_t *t = area->gc_run_index;
    const size_t p = area->gc_run_index_leaves;
    size_t i = p + leaf;
    size_t len = GC_RUN_LEAF;
    size_t first = leaf * GC_RUN_LEAF;
    // the free blocks just below node i, exactly or (after skipping a node) at most
    size_t carry = gc_free_below(area, first, n_blocks);
    bool exact = true;
    for (;;) {
        // in lengths: carry + pre >= n_blocks or max >= n_blocks
        if (carry + len >= n_blocks + t[i].pre || len >= n_blocks + t[i].max) {
            if (i < p) {
                i *= 2;
                len /= 2;
                continue;
            }
            if (!exact) {
                carry = gc_free_below(area, first, n_blocks);
                exact = true;
            }
            const size_t found = gc_leaf_search(area, i - p, n_blocks, &carry, &t[i]);
            if (found != GC_HINT_NONE) {
                return found;
            }
        } else {
            carry = t[i].pre == 0 ? carry + len : len - t[i].suf;
            exact = false;
        }
        // the next node to the right: up while this one is a right child, making the
        // nodes passed promise no more than their children
        while (i & 1) {
            if (i == 1) {
                return GC_HINT_NONE;
            }
            i /= 2;
            len *= 2;
            first -= len / 2;
            t[i] = gc_runs_join(t[2 * i], t[2 * i + 1], (uint32_t)(len / 2));
        }
        i += 1;
        first += len;
    }
}

// Puts the hints at the bottom of the heap, and makes the index promise every block.
static void gc_hints_reset(mp_state_mem_area_t *area) {
    for (size_t c = 0; c < MP_GC_ALLOC_HINT_CLASSES; c++) {
        area->gc_alloc_hint[c] = 0;
    }
    if (area->gc_run_index != NULL) {
        memset(area->gc_run_index, 0, 2 * area->gc_run_index_leaves * sizeof(gc_runs_t));
    }
}

static size_t gc_hint_class(size_t n_blocks) {
    if (n_blocks <= 8) {
        return n_blocks - 1;
    }
    size_t c = 7;
    while (c + 1 < MP_GC_ALLOC_HINT_CLASSES && gc_hint_size[c + 1] <= n_blocks) {
        c++;
    }
    return c;
}

// The start of the lowest run of n_blocks free blocks, for a request of class c, or
// GC_HINT_NONE. *passed: where the first run of at least gc_hint_size[c] (but fewer
// than n_blocks) free blocks that the search went past starts, or where the search
// handed over to the index; else GC_HINT_NONE.
static size_t gc_hint_search(mp_state_mem_area_t *area, size_t n_blocks, size_t c, size_t *passed) {
    const size_t from = MAX(area->gc_alloc_hint[0], area->gc_alloc_hint[c]);
    const byte *atb = area->gc_alloc_table_start;
    const size_t len = area->gc_alloc_table_byte_len;
    // Most often the blocks at the hint are free (no run of the class's size starts
    // below it, so the run they are in starts there).
    if (from % BLOCKS_PER_ATB + n_blocks <= BLOCKS_PER_ATB && from / BLOCKS_PER_ATB < len
        && (atb[from / BLOCKS_PER_ATB] >> (2 * (from % BLOCKS_PER_ATB)) & ((1u << (2 * n_blocks)) - 1)) == 0) {
        *passed = GC_HINT_NONE;
        return from;
    }
    const size_t min_run = gc_hint_size[c];
    const size_t max_steps = area->gc_run_index != NULL ? GC_HINT_STEPS : (size_t)-1;
    size_t steps = 0;
    size_t n_free = 0;
    size_t first_passed = GC_HINT_NONE;
    size_t found = GC_HINT_NONE;
    for (size_t i = from / BLOCKS_PER_ATB; i < len; i++) {
        MICROPY_GC_HOOK_LOOP(i);
        if (++steps > max_steps) {
            if (first_passed == GC_HINT_NONE) {
                first_passed = i * BLOCKS_PER_ATB - n_free;
            }
            found = gc_run_index_find(area, n_blocks, i * BLOCKS_PER_ATB / GC_RUN_LEAF);
            break;
        }
        byte a = atb[i];
        if (a == 0) {
            // four free blocks
            if (n_free + BLOCKS_PER_ATB >= n_blocks) {
                found = i * BLOCKS_PER_ATB - n_free;
                break;
            }
            n_free += BLOCKS_PER_ATB;
            continue;
        }
        if (ATB_ALL_USED(a)) {
            // four blocks in use: a run ends below them, and whole words of the table
            // whose blocks are all in use are skipped
            if (n_free >= min_run && first_passed == GC_HINT_NONE) {
                first_passed = i * BLOCKS_PER_ATB - n_free;
            }
            n_free = 0;
            while (i + 1 + sizeof(uint32_t) <= len && steps < max_steps) {
                uint32_t w;
                memcpy(&w, atb + i + 1, sizeof(w));
                if (((w | (w >> 1)) & 0x55555555u) != 0x55555555u) {
                    break;
                }
                i += sizeof(uint32_t);
                steps++;
            }
            continue;
        }
        for (size_t k = 0; k < BLOCKS_PER_ATB; k++, a >>= 2) {
            if ((a & 3) == AT_FREE) {
                if (++n_free >= n_blocks) {
                    found = i * BLOCKS_PER_ATB + k + 1 - n_blocks;
                    break;
                }
            } else {
                if (n_free >= min_run && first_passed == GC_HINT_NONE) {
                    first_passed = i * BLOCKS_PER_ATB + k - n_free;
                }
                n_free = 0;
            }
        }
        if (found != GC_HINT_NONE) {
            break;
        }
    }
    *passed = first_passed;
    return found;
}

// After a search for class c took n_blocks at block `start`.
static void gc_hints_allocated(mp_state_mem_area_t *area, size_t c, size_t start, size_t n_blocks, size_t passed) {
    // No run of n_blocks free blocks starts below the end of the new allocation: the
    // search found none below `start`, and the rest of the run it took (if any)
    // starts at that end. For a class that serves larger sizes too, no run of its own
    // size starts below where the search passed one or handed over to the index.
    const size_t after = start + n_blocks;
    const size_t h = gc_hint_size[c] == n_blocks || passed == GC_HINT_NONE ? after : passed;
    if (area->gc_alloc_hint[c] < h) {
        area->gc_alloc_hint[c] = h;
    }
    // The next class is longer than n_blocks (else c would be it).
    if (gc_hint_size[c] < n_blocks && c + 1 < MP_GC_ALLOC_HINT_CLASSES && area->gc_alloc_hint[c + 1] < after) {
        area->gc_alloc_hint[c + 1] = after;
    }
    // No free block is below the hint of single blocks: if it was among those just
    // taken, none is below their end now. (It keeps up with allocations of every size
    // made where the heap's free space begins, and every search starts at it or above.)
    if (area->gc_alloc_hint[0] >= start && area->gc_alloc_hint[0] < after) {
        area->gc_alloc_hint[0] = after;
    }
}

// After the n_blocks blocks at `block` became free.
static void gc_hints_freed(mp_state_mem_area_t *area, size_t block, size_t n_blocks) {
    // The free run they joined, [start, end): measured as far as the longest class
    // size, and (for the index) to the ends of the leaves they are in.
    const size_t longest = gc_hint_size[MP_GC_ALLOC_HINT_CLASSES - 1];
    const size_t below = gc_free_below(area, block, MAX(longest, GC_RUN_LEAF));
    const size_t start = block - below;
    const size_t leaf_end = ((block + n_blocks - 1) / GC_RUN_LEAF + 1) * GC_RUN_LEAF;
    const size_t limit = MAX(start + longest, leaf_end);
    const size_t n_total = gc_blocks(area);
    size_t end = block + n_blocks;
    while (end < limit && end < n_total) {
        if (end % BLOCKS_PER_ATB == 0 && area->gc_alloc_table_start[end / BLOCKS_PER_ATB] == 0) {
            end += BLOCKS_PER_ATB;
        } else if (ATB_GET_KIND(area, end) == AT_FREE) {
            end++;
        } else {
            break;
        }
    }
    if (area->gc_run_index != NULL) {
        gc_run_index_freed(area, block, n_blocks, start, end);
    }
    // Classes no longer than the part below the freed blocks knew of the run already
    // (their hints are at or below its start); those longer than it can't use it.
    for (size_t c = 0; c < MP_GC_ALLOC_HINT_CLASSES && gc_hint_size[c] <= end - start; c++) {
        if (gc_hint_size[c] > below && area->gc_alloc_hint[c] > start) {
            area->gc_alloc_hint[c] = start;
        }
    }
}
#endif // MICROPY_GC_ALLOC_HINTS

// Static functions for individual steps of the GC mark/sweep sequence
static void gc_collect_start_common(void);
static void *gc_get_ptr(void **ptrs, int i);
#if MICROPY_GC_SPLIT_HEAP
static void gc_mark_subtree(mp_state_mem_area_t *area, size_t block);
#else
static void gc_mark_subtree(size_t block);
#endif
static void gc_deal_with_stack_overflow(void);
static void gc_sweep_run_finalisers(void);
static void gc_sweep_free_blocks(void);

// TODO waste less memory; currently requires that all entries in alloc_table have a corresponding block in pool
static void gc_setup_area(mp_state_mem_area_t *area, void *start, void *end) {
    // calculate parameters for GC (T=total, A=alloc table, F=finaliser table, P=pool; all in bytes):
    // T = A + F + W + P
    //     F = A * BLOCKS_PER_ATB / BLOCKS_PER_FTB
    //     W = A * BLOCKS_PER_ATB / BLOCKS_PER_WTB
    //     P = A * BLOCKS_PER_ATB * BYTES_PER_BLOCK
    // => T = A * (1 + BLOCKS_PER_ATB / BLOCKS_PER_FTB + BLOCKS_PER_ATB / BLOCKS_PER_WTB + BLOCKS_PER_ATB * BYTES_PER_BLOCK)
    size_t total_byte_len = (byte *)end - (byte *)start;
    #if MICROPY_ENABLE_FINALISER || MICROPY_PY_WEAKREF
    area->gc_alloc_table_byte_len = (total_byte_len - ALLOC_TABLE_GAP_BYTE)
        * MP_BITS_PER_BYTE
        / (
            MP_BITS_PER_BYTE
            #if MICROPY_ENABLE_FINALISER
            + MP_BITS_PER_BYTE * BLOCKS_PER_ATB / BLOCKS_PER_FTB
            #endif
            #if MICROPY_PY_WEAKREF
            + MP_BITS_PER_BYTE * BLOCKS_PER_ATB / BLOCKS_PER_WTB
            #endif
            + MP_BITS_PER_BYTE * BLOCKS_PER_ATB * BYTES_PER_BLOCK
            );
    #else
    area->gc_alloc_table_byte_len = (total_byte_len - ALLOC_TABLE_GAP_BYTE) / (1 + MP_BITS_PER_BYTE / 2 * BYTES_PER_BLOCK);
    #endif

    area->gc_alloc_table_start = (byte *)start;

    // Allocate FTB and WTB blocks if they are enabled.
    byte *next_table = area->gc_alloc_table_start + area->gc_alloc_table_byte_len + ALLOC_TABLE_GAP_BYTE;
    (void)next_table;
    #if MICROPY_ENABLE_FINALISER
    size_t gc_finaliser_table_byte_len = (area->gc_alloc_table_byte_len * BLOCKS_PER_ATB + BLOCKS_PER_FTB - 1) / BLOCKS_PER_FTB;
    area->gc_finaliser_table_start = next_table;
    next_table += gc_finaliser_table_byte_len;
    #endif
    #if MICROPY_PY_WEAKREF
    size_t gc_weakref_table_byte_len = (area->gc_alloc_table_byte_len * BLOCKS_PER_ATB + BLOCKS_PER_WTB - 1) / BLOCKS_PER_WTB;
    area->gc_weakref_table_start = next_table;
    next_table += gc_weakref_table_byte_len;
    #endif

    // Allocate the GC pool of heap blocks.
    size_t gc_pool_block_len = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    area->gc_pool_start = (byte *)end - gc_pool_block_len * BYTES_PER_BLOCK;
    area->gc_pool_end = end;
    assert(area->gc_pool_start >= next_table);

    // Clear ATB's, and FTB's and WTB's if they are enabled.
    memset(area->gc_alloc_table_start, 0,
        area->gc_alloc_table_byte_len + ALLOC_TABLE_GAP_BYTE
        #if MICROPY_ENABLE_FINALISER
        + gc_finaliser_table_byte_len
        #endif
        #if MICROPY_PY_WEAKREF
        + gc_weakref_table_byte_len
        #endif
        );

    #if MICROPY_GC_ALLOC_HINTS
    area->gc_run_index = NULL;
    gc_hints_reset(area);
    #else
    area->gc_last_free_atb_index = 0;
    #endif
    area->gc_last_used_block = 0;

    #if MICROPY_GC_SPLIT_HEAP
    area->next = NULL;

    // Update the global min/max region that covers all heaps
    MP_STATE_MEM(area_pool_min) = MIN(MP_STATE_MEM(area_pool_min), area->gc_pool_start);
    MP_STATE_MEM(area_pool_max) = MAX(MP_STATE_MEM(area_pool_max), area->gc_pool_end);
    #endif

    DEBUG_printf("GC layout:\n");
    DEBUG_printf("  alloc table at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n",
        area->gc_alloc_table_start, area->gc_alloc_table_byte_len,
        area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
    #if MICROPY_ENABLE_FINALISER
    DEBUG_printf("  finaliser table at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n", area->gc_finaliser_table_start,
        gc_finaliser_table_byte_len,
        gc_finaliser_table_byte_len * BLOCKS_PER_FTB);
    #endif
    #if MICROPY_PY_WEAKREF
    DEBUG_printf("  weakref table at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n", area->gc_weakref_table_start,
        gc_weakref_table_byte_len,
        gc_weakref_table_byte_len * BLOCKS_PER_WTB);
    #endif
    DEBUG_printf("  pool at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n", area->gc_pool_start,
        gc_pool_block_len * BYTES_PER_BLOCK, gc_pool_block_len);
}

void gc_init(void *start, void *end) {
    // align end pointer on block boundary
    end = (void *)((uintptr_t)end & (~(BYTES_PER_BLOCK - 1)));
    DEBUG_printf("Initializing GC heap: %p..%p = " UINT_FMT " bytes\n", start, end, (byte *)end - (byte *)start);

    #if MICROPY_GC_SPLIT_HEAP
    // Note: min/max are deliberately swapped here, gc_setup_area() will update them to
    // the correct values for the min/max of the first actual pool region
    MP_STATE_MEM(area_pool_min) = end;
    MP_STATE_MEM(area_pool_max) = start;
    #endif

    gc_setup_area(&MP_STATE_MEM(area), start, end);

    // set last free ATB index to start of heap
    #if MICROPY_GC_SPLIT_HEAP
    MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
    #endif

    // unlock the GC
    MP_STATE_THREAD(gc_lock_depth) = 0;

    // allow auto collection
    MP_STATE_MEM(gc_auto_collect_enabled) = 1;

    #if MICROPY_GC_ALLOC_THRESHOLD
    // by default, maxuint for gc threshold, effectively turning gc-by-threshold off
    MP_STATE_MEM(gc_alloc_threshold) = (size_t)-1;
    MP_STATE_MEM(gc_alloc_amount) = 0;
    #endif

    GC_MUTEX_INIT();
}

#if MICROPY_GC_SPLIT_HEAP
void gc_add(void *start, void *end) {
    // Place the area struct at the start of the area.
    mp_state_mem_area_t *area = (mp_state_mem_area_t *)start;
    start = (void *)((uintptr_t)start + sizeof(mp_state_mem_area_t));

    end = (void *)((uintptr_t)end & (~(BYTES_PER_BLOCK - 1)));
    DEBUG_printf("Adding GC heap: %p..%p = " UINT_FMT " bytes\n", start, end, (byte *)end - (byte *)start);

    // Init this area
    gc_setup_area(area, start, end);

    // Find the last registered area in the linked list
    mp_state_mem_area_t *prev_area = &MP_STATE_MEM(area);
    while (prev_area->next != NULL) {
        prev_area = prev_area->next;
    }

    // Add this area to the linked list
    prev_area->next = area;
}

#if MICROPY_GC_SPLIT_HEAP_AUTO
// Try to automatically add a heap area large enough to fulfill 'failed_alloc'.
static bool gc_try_add_heap(size_t failed_alloc) {
    // 'needed' is the size of a heap large enough to hold failed_alloc, with
    // the additional metadata overheads as calculated in gc_setup_area().
    //
    // Rather than reproduce all of that logic here, we approximate that adding
    // (13/512) is enough overhead for sufficiently large heap areas (the
    // overhead converges to 3/128, but there's some fixed overhead and some
    // rounding up of partial block sizes).
    size_t needed = failed_alloc + MAX(2048, failed_alloc * 13 / 512);

    size_t avail = gc_get_max_new_split();

    DEBUG_printf("gc_try_add_heap failed_alloc " UINT_FMT ", "
        "needed " UINT_FMT ", avail " UINT_FMT " bytes \n",
        failed_alloc,
        needed,
        avail);

    if (avail < needed) {
        // Can't fit this allocation, or system heap has nearly run out anyway
        return false;
    }

    // Deciding how much to grow the total heap by each time is tricky:
    //
    // - Grow by too small amounts, leads to heap fragmentation issues.
    //
    // - Grow by too large amounts, may lead to system heap running out of
    //   space.
    //
    // Currently, this implementation is:
    //
    // - At minimum, aim to double the total heap size each time we add a new
    //   heap.  i.e. without any large single allocations, total size will be
    //   64KB -> 128KB -> 256KB -> 512KB -> 1MB, etc
    //
    // - If the failed allocation is too large to fit in that size, the new
    //   heap is made exactly large enough for that allocation. Future growth
    //   will double the total heap size again.
    //
    // - If the new heap won't fit in the available free space, add the largest
    //   new heap that will fit (this may lead to failed system heap allocations
    //   elsewhere, but some allocation will likely fail in this circumstance!)

    // Compute total number of blocks in the current heap.
    size_t total_blocks = 0;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area);
         area != NULL;
         area = NEXT_AREA(area)) {
        total_blocks += area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    }

    // Compute bytes needed to build a heap with total_blocks blocks.
    size_t total_heap =
        total_blocks / BLOCKS_PER_ATB
        #if MICROPY_ENABLE_FINALISER
        + total_blocks / BLOCKS_PER_FTB
        #endif
        #if MICROPY_PY_WEAKREF
        + total_blocks / BLOCKS_PER_WTB
        #endif
        + total_blocks * BYTES_PER_BLOCK
        + ALLOC_TABLE_GAP_BYTE
        + sizeof(mp_state_mem_area_t);

    // Round up size to the nearest multiple of BYTES_PER_BLOCK.
    total_heap = (total_heap + BYTES_PER_BLOCK - 1) & (~(BYTES_PER_BLOCK - 1));

    DEBUG_printf("total_heap " UINT_FMT " bytes\n", total_heap);

    size_t to_alloc = MIN(avail, MAX(total_heap, needed));

    mp_state_mem_area_t *new_heap = MP_PLAT_ALLOC_HEAP(to_alloc);

    DEBUG_printf("MP_PLAT_ALLOC_HEAP " UINT_FMT " = %p\n",
        to_alloc, new_heap);

    if (new_heap == NULL) {
        // This should only fail:
        // - In a threaded environment if another thread has
        //   allocated while this function ran.
        // - If there is a bug in gc_get_max_new_split().
        return false;
    }

    gc_add(new_heap, (void *)new_heap + to_alloc);

    return true;
}
#endif

#endif

void gc_lock(void) {
    // This does not need to be atomic or have the GC mutex because:
    // - each thread has its own gc_lock_depth so there are no races between threads;
    // - a hard interrupt will only change gc_lock_depth during its execution, and
    //   upon return will restore the value of gc_lock_depth.
    MP_STATE_THREAD(gc_lock_depth) += (1 << GC_LOCK_DEPTH_SHIFT);
}

void gc_unlock(void) {
    // This does not need to be atomic, See comment above in gc_lock.
    MP_STATE_THREAD(gc_lock_depth) -= (1 << GC_LOCK_DEPTH_SHIFT);
}

bool gc_is_locked(void) {
    return MP_STATE_THREAD(gc_lock_depth) != 0;
}

#if MICROPY_GC_SPLIT_HEAP
static mp_state_mem_area_t *gc_get_ptr_area(const void *ptr);

// Returns the area to which this arbitrary pointer belongs, or NULL if it isn't
// allocated on the GC-managed heap. Contains "fast path" inline checks for invalid
// data which isn't a pointer to the heap. Equivalent of VERIFY_PTR for the non-split-heap case.
static inline MP_ALWAYSINLINE mp_state_mem_area_t *gc_verify_ptr_get_area(const void *ptr) {
    // These inline checks are similar to VERIFY_PTR macro, below
    if ((byte *)ptr < MP_STATE_MEM(area_pool_min) || (byte *)ptr > MP_STATE_MEM(area_pool_max)) {
        return NULL;  // not in the overall pool region
    }
    if (((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) != 0) {
        return NULL;  // not aligned on a block boundary
    }
    return gc_get_ptr_area(ptr);
}

// Returns the area to which a pointer belongs. Assumes pointer is valid to a heap block.
static mp_state_mem_area_t *gc_get_ptr_area(const void *ptr) {
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        if (ptr >= (void *)area->gc_pool_start   // must be above start of pool
            && ptr < (void *)area->gc_pool_end) {   // must be below end of pool
            return area;
        }
    }
    return NULL;
}
#else

// ptr should be of type void*
#define VERIFY_PTR(ptr) ( \
    ((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) == 0          /* must be aligned on a block */ \
    && ptr >= (void *)MP_STATE_MEM(area).gc_pool_start      /* must be above start of pool */ \
    && ptr < (void *)MP_STATE_MEM(area).gc_pool_end         /* must be below end of pool */ \
    )

#endif

#ifdef TRACE_MARK
#error "TRACE_MARK is replaced by TRACE_MARK_R and TRACE_MARK_S"
#endif
// R for root pointer, S for subtree.
#ifndef TRACE_MARK_R
#if DEBUG_PRINT
#define TRACE_MARK_R(block, ptr) DEBUG_printf("gc_mark_r(%p)\n", ptr)
#else
#define TRACE_MARK_R(block, ptr)
#endif
#endif
#ifndef TRACE_MARK_S
#if DEBUG_PRINT
#define TRACE_MARK_S(block, ptr) DEBUG_printf("gc_mark_s(%p)\n", ptr)
#else
#define TRACE_MARK_S(block, ptr)
#endif
#endif

void gc_collect_start(void) {
    gc_collect_start_common();
    #if MICROPY_GC_ALLOC_THRESHOLD
    MP_STATE_MEM(gc_alloc_amount) = 0;
    #endif

    // Trace root pointers.  This relies on the root pointers being organised
    // correctly in the mp_state_ctx structure.  We scan nlr_top, dict_locals,
    // dict_globals, then the root pointer section of mp_state_vm.
    void **ptrs = (void **)(void *)&mp_state_ctx;
    size_t root_start = offsetof(mp_state_ctx_t, thread.dict_locals);
    size_t root_end = offsetof(mp_state_ctx_t, vm.qstr_last_chunk);
    gc_collect_root(ptrs + root_start / sizeof(void *), (root_end - root_start) / sizeof(void *));

    #if MICROPY_ENABLE_PYSTACK
    // Trace root pointers from the Python stack.
    ptrs = (void **)(void *)MP_STATE_THREAD(pystack_start);
    gc_collect_root(ptrs, (MP_STATE_THREAD(pystack_cur) - MP_STATE_THREAD(pystack_start)) / sizeof(void *));
    #endif
}

static void gc_collect_start_common(void) {
    GC_ENTER();
    assert((MP_STATE_THREAD(gc_lock_depth) & GC_COLLECT_FLAG) == 0);
    MP_STATE_THREAD(gc_lock_depth) |= GC_COLLECT_FLAG;
    MP_STATE_MEM(gc_stack_overflow) = 0;
}

void gc_collect_root(void **ptrs, size_t len) {
    #if !MICROPY_GC_SPLIT_HEAP
    mp_state_mem_area_t *area = &MP_STATE_MEM(area);
    #endif
    for (size_t i = 0; i < len; i++) {
        MICROPY_GC_HOOK_LOOP(i);
        void *ptr = gc_get_ptr(ptrs, i);
        #if MICROPY_GC_SPLIT_HEAP
        mp_state_mem_area_t *area = gc_verify_ptr_get_area(ptr);
        if (!area) {
            continue;
        }
        #else
        if (!VERIFY_PTR(ptr)) {
            continue;
        }
        #endif
        size_t block = BLOCK_FROM_PTR(area, ptr);
        if (ATB_GET_KIND(area, block) == AT_HEAD) {
            // An unmarked head: mark it, and mark all its children
            TRACE_MARK_R(ptr_block, ptr);
            ATB_HEAD_TO_MARK(area, block);
            #if MICROPY_GC_SPLIT_HEAP
            gc_mark_subtree(area, block);
            #else
            gc_mark_subtree(block);
            #endif
        }
    }
}

// Take the given block as the topmost block on the stack. Check all it's
// children: mark the unmarked child blocks and put those newly marked
// blocks on the stack. When all children have been checked, pop off the
// topmost block on the stack and repeat with that one.
#if MICROPY_GC_SPLIT_HEAP
static void gc_mark_subtree(mp_state_mem_area_t *area, size_t block)
#else
static void gc_mark_subtree(size_t block)
#endif
{
    // Start with the block passed in the argument.
    size_t sp = 0;
    for (;;) {
        #if !MICROPY_GC_SPLIT_HEAP
        mp_state_mem_area_t *area = &MP_STATE_MEM(area);
        #endif

        // work out number of consecutive blocks in the chain starting with this one
        size_t n_blocks = 0;
        do {
            n_blocks += 1;
        } while (ATB_GET_KIND(area, block + n_blocks) == AT_TAIL);

        // check that the consecutive blocks didn't overflow past the end of the area
        assert(area->gc_pool_start + (block + n_blocks) * BYTES_PER_BLOCK <= area->gc_pool_end);

        // check this block's children
        void **ptrs = (void **)PTR_FROM_BLOCK(area, block);
        for (size_t i = n_blocks * BYTES_PER_BLOCK / sizeof(void *); i > 0; i--, ptrs++) {
            MICROPY_GC_HOOK_LOOP(i);
            void *ptr = *ptrs;
            // If this is a heap pointer that hasn't been marked, mark it and push
            // it's children to the stack.
            #if MICROPY_GC_SPLIT_HEAP
            mp_state_mem_area_t *ptr_area = gc_verify_ptr_get_area(ptr);
            if (!ptr_area) {
                // Not a heap-allocated pointer (might even be random data).
                continue;
            }
            #else
            if (!VERIFY_PTR(ptr)) {
                continue;
            }
            mp_state_mem_area_t *ptr_area = area;
            #endif
            size_t ptr_block = BLOCK_FROM_PTR(ptr_area, ptr);
            if (ATB_GET_KIND(ptr_area, ptr_block) != AT_HEAD) {
                // This block is already marked.
                continue;
            }
            // An unmarked head. Mark it, and push it on gc stack.
            TRACE_MARK_S(ptr_block, ptr);
            ATB_HEAD_TO_MARK(ptr_area, ptr_block);
            if (sp < MICROPY_ALLOC_GC_STACK_SIZE) {
                MP_STATE_MEM(gc_block_stack)[sp] = ptr_block;
                #if MICROPY_GC_SPLIT_HEAP
                MP_STATE_MEM(gc_area_stack)[sp] = ptr_area;
                #endif
                sp += 1;
            } else {
                MP_STATE_MEM(gc_stack_overflow) = 1;
            }
        }

        // Are there any blocks on the stack?
        if (sp == 0) {
            break; // No, stack is empty, we're done.
        }

        // pop the next block off the stack
        sp -= 1;
        block = MP_STATE_MEM(gc_block_stack)[sp];
        #if MICROPY_GC_SPLIT_HEAP
        area = MP_STATE_MEM(gc_area_stack)[sp];
        #endif
    }
}

void gc_sweep_all(void) {
    gc_collect_start_common();
    gc_collect_end();
}

void gc_collect_end(void) {
    gc_deal_with_stack_overflow();
    gc_sweep_run_finalisers();
    gc_sweep_free_blocks();
    #if MICROPY_GC_SPLIT_HEAP
    MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
    #endif
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        #if MICROPY_GC_ALLOC_HINTS
        gc_hints_reset(area);
        #else
        area->gc_last_free_atb_index = 0;
        #endif
    }
    MP_STATE_THREAD(gc_lock_depth) &= ~GC_COLLECT_FLAG;
    GC_EXIT();
    #if MICROPY_PY_WEAKREF
    gc_weakref_sweep();
    #endif
}

static void gc_deal_with_stack_overflow(void) {
    while (MP_STATE_MEM(gc_stack_overflow)) {
        MP_STATE_MEM(gc_stack_overflow) = 0;

        // scan entire memory looking for blocks which have been marked but not their children
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            for (size_t block = 0; block < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; block++) {
                MICROPY_GC_HOOK_LOOP(block);
                // trace (again) if mark bit set
                if (ATB_GET_KIND(area, block) == AT_MARK) {
                    #if MICROPY_GC_SPLIT_HEAP
                    gc_mark_subtree(area, block);
                    #else
                    gc_mark_subtree(block);
                    #endif
                }
            }
        }
    }
}

// Run finalisers for all to-be-freed blocks
static void gc_sweep_run_finalisers(void) {
    #if MICROPY_ENABLE_FINALISER || MICROPY_PY_WEAKREF
    #if MICROPY_ENABLE_FINALISER && MICROPY_PY_WEAKREF
    MP_STATIC_ASSERT(BLOCKS_PER_FTB == BLOCKS_PER_WTB);
    #endif
    for (const mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        assert(area->gc_last_used_block <= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
        // Small speed optimisation: skip over empty FTB blocks
        size_t ftb_end = area->gc_last_used_block / BLOCKS_PER_FTB; // index is inclusive
        for (size_t ftb_idx = 0; ftb_idx <= ftb_end; ftb_idx++) {
            #if MICROPY_ENABLE_FINALISER
            byte ftb = area->gc_finaliser_table_start[ftb_idx];
            size_t block = ftb_idx * BLOCKS_PER_FTB;
            while (ftb) {
                MICROPY_GC_HOOK_LOOP(block);
                if (ftb & 1) { // FTB_GET(area, block) shortcut
                    if (ATB_GET_KIND(area, block) == AT_HEAD) {
                        mp_obj_base_t *obj = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
                        if (obj->type != NULL) {
                            // if the object has a type then see if it has a __del__ method
                            mp_obj_t dest[2];
                            mp_load_method_maybe(MP_OBJ_FROM_PTR(obj), MP_QSTR___del__, dest);
                            if (dest[0] != MP_OBJ_NULL) {
                                // load_method returned a method, execute it in a protected environment
                                #if MICROPY_ENABLE_SCHEDULER
                                mp_sched_lock();
                                #endif
                                mp_call_function_1_protected(dest[0], dest[1]);
                                #if MICROPY_ENABLE_SCHEDULER
                                mp_sched_unlock();
                                #endif
                            }
                        }
                        // clear finaliser flag
                        FTB_CLEAR(area, block);
                    }
                }
                ftb >>= 1;
                block++;
            }
            #endif
            #if MICROPY_PY_WEAKREF
            byte wtb = area->gc_weakref_table_start[ftb_idx];
            block = ftb_idx * BLOCKS_PER_WTB;
            while (wtb) {
                MICROPY_GC_HOOK_LOOP(block);
                if (wtb & 1) { // WTB_GET(area, block) shortcut
                    if (ATB_GET_KIND(area, block) == AT_HEAD) {
                        mp_obj_base_t *obj = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
                        gc_weakref_about_to_be_freed(obj);
                        WTB_CLEAR(area, block);
                    }
                }
                wtb >>= 1;
                block++;
            }
            #endif
        }
    }
    #endif // MICROPY_ENABLE_FINALISER || MICROPY_PY_WEAKREF
}

// Free unmarked heads and their tails
static void gc_sweep_free_blocks(void) {
    #if MICROPY_PY_GC_COLLECT_RETVAL
    MP_STATE_MEM(gc_collected) = 0;
    #endif
    int free_tail = 0;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    mp_state_mem_area_t *prev_area = NULL;
    #endif

    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        size_t last_used_block = 0;
        assert(area->gc_last_used_block <= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);

        for (size_t block = 0; block <= area->gc_last_used_block; block++) {
            MICROPY_GC_HOOK_LOOP(block);
            switch (ATB_GET_KIND(area, block)) {
                case AT_HEAD:
                    free_tail = 1;
                    DEBUG_printf("gc_sweep_free_blocks(%p)\n", (void *)PTR_FROM_BLOCK(area, block));
                    #if MICROPY_PY_GC_COLLECT_RETVAL
                    MP_STATE_MEM(gc_collected)++;
                    #endif
                    // fall through to free the head
                    MP_FALLTHROUGH

                case AT_TAIL:
                    if (free_tail) {
                        ATB_ANY_TO_FREE(area, block);
                        #if CLEAR_ON_SWEEP
                        memset((void *)PTR_FROM_BLOCK(area, block), 0, BYTES_PER_BLOCK);
                        #endif
                    } else {
                        last_used_block = block;
                    }
                    break;

                case AT_MARK:
                    ATB_MARK_TO_HEAD(area, block);
                    free_tail = 0;
                    last_used_block = block;
                    break;
            }
        }

        area->gc_last_used_block = last_used_block;

        #if MICROPY_GC_SPLIT_HEAP_AUTO
        // Free any empty area, aside from the first one
        if (last_used_block == 0 && prev_area != NULL) {
            DEBUG_printf("gc_sweep_free_blocks free empty area %p\n", area);
            NEXT_AREA(prev_area) = NEXT_AREA(area);
            MP_PLAT_FREE_HEAP(area);
            area = prev_area;
        }
        prev_area = area;
        #endif
    }
}

// Address sanitizer needs to know that the access to ptrs[i] must always be
// considered OK, even if it's a load from an address that would normally be
// prohibited (due to being undefined, in a red zone, etc).
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
__attribute__((no_sanitize_address))
#endif
static void *gc_get_ptr(void **ptrs, int i) {
    #if MICROPY_DEBUG_VALGRIND
    if (!VALGRIND_CHECK_MEM_IS_ADDRESSABLE(&ptrs[i], sizeof(*ptrs))) {
        return NULL;
    }
    #endif
    return ptrs[i];
}

void gc_info(gc_info_t *info) {
    GC_ENTER();
    info->total = 0;
    info->used = 0;
    info->free = 0;
    info->max_free = 0;
    info->num_1block = 0;
    info->num_2block = 0;
    info->max_block = 0;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        bool finish = false;
        info->total += area->gc_pool_end - area->gc_pool_start;
        for (size_t block = 0, len = 0, len_free = 0; !finish;) {
            MICROPY_GC_HOOK_LOOP(block);
            size_t kind = ATB_GET_KIND(area, block);
            switch (kind) {
                case AT_FREE:
                    info->free += 1;
                    len_free += 1;
                    len = 0;
                    break;

                case AT_HEAD:
                    info->used += 1;
                    len = 1;
                    break;

                case AT_TAIL:
                    info->used += 1;
                    len += 1;
                    break;

                case AT_MARK:
                    // shouldn't happen
                    break;
            }

            block++;
            finish = (block == area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
            // Get next block type if possible
            if (!finish) {
                kind = ATB_GET_KIND(area, block);
            }

            if (finish || kind == AT_FREE || kind == AT_HEAD) {
                if (len == 1) {
                    info->num_1block += 1;
                } else if (len == 2) {
                    info->num_2block += 1;
                }
                if (len > info->max_block) {
                    info->max_block = len;
                }
                if (finish || kind == AT_HEAD) {
                    if (len_free > info->max_free) {
                        info->max_free = len_free;
                    }
                    len_free = 0;
                }
            }
        }
    }

    info->used *= BYTES_PER_BLOCK;
    info->free *= BYTES_PER_BLOCK;

    #if MICROPY_GC_SPLIT_HEAP_AUTO
    info->max_new_split = gc_get_max_new_split();
    #endif

    GC_EXIT();
}

// Fast version of gc_info that only computes total/used/free.
void gc_info_fast(gc_info_t *info) {
    GC_ENTER();
    memset(info, 0, sizeof(*info));
    const uint8_t lut[16] = {2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        size_t free_blocks = 0;
        info->total += area->gc_pool_end - area->gc_pool_start;
        for (size_t i = 0; i < area->gc_alloc_table_byte_len; i++) {
            uint8_t atb = area->gc_alloc_table_start[i];
            free_blocks += lut[atb & 0xF] + lut[atb >> 4];
        }
        info->free += free_blocks;
    }
    info->free *= BYTES_PER_BLOCK;
    info->used = info->total - info->free;
    GC_EXIT();
}

#if MICROPY_PY_WEAKREF
// Mark the GC heap pointer as having a weakref.
void gc_weakref_mark(void *ptr) {
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    assert(area);
    #else
    assert(VERIFY_PTR(ptr));
    area = &MP_STATE_MEM(area);
    #endif

    size_t block = BLOCK_FROM_PTR(area, ptr);
    assert(ATB_GET_KIND(area, block) == AT_HEAD);

    WTB_SET(area, block);
}
#endif

void *gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    bool has_finaliser = alloc_flags & GC_ALLOC_FLAG_HAS_FINALISER;
    size_t n_blocks = ((n_bytes + BYTES_PER_BLOCK - 1) & (~(BYTES_PER_BLOCK - 1))) / BYTES_PER_BLOCK;
    DEBUG_printf("gc_alloc(" UINT_FMT " bytes -> " UINT_FMT " blocks)\n", n_bytes, n_blocks);

    // check for 0 allocation
    if (n_blocks == 0) {
        return NULL;
    }

    // check if GC is locked
    if (MP_STATE_THREAD(gc_lock_depth) > 0) {
        return NULL;
    }

    GC_ENTER();

    mp_state_mem_area_t *area;
    size_t i;
    size_t end_block;
    size_t start_block;
    size_t n_free;
    #if MICROPY_GC_ALLOC_HINTS
    size_t hint_class = gc_hint_class(n_blocks);
    size_t hint_passed;
    #endif
    int collected = !MP_STATE_MEM(gc_auto_collect_enabled);
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    bool added = false;
    #endif

    #if MICROPY_GC_ALLOC_THRESHOLD
    if (!collected && MP_STATE_MEM(gc_alloc_amount) >= MP_STATE_MEM(gc_alloc_threshold)) {
        GC_EXIT();
        gc_collect();
        collected = 1;
        GC_ENTER();
    }
    #endif

    for (;;) {

        #if MICROPY_GC_SPLIT_HEAP
        area = MP_STATE_MEM(gc_last_free_area);
        #else
        area = &MP_STATE_MEM(area);
        #endif

        #if MICROPY_GC_ALLOC_HINTS
        start_block = gc_hint_search(area, n_blocks, hint_class, &hint_passed);
        if (start_block != GC_HINT_NONE) {
            i = start_block + n_blocks - 1;
            n_free = n_blocks;
            goto found;
        }
        #else
        // look for a run of n_blocks available blocks
        for (; area != NULL; area = NEXT_AREA(area), i = 0) {
            n_free = 0;
            for (i = area->gc_last_free_atb_index; i < area->gc_alloc_table_byte_len; i++) {
                MICROPY_GC_HOOK_LOOP(i);
                byte a = area->gc_alloc_table_start[i];
                // *FORMAT-OFF*
                if (ATB_0_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 0; goto found; } } else { n_free = 0; }
                if (ATB_1_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 1; goto found; } } else { n_free = 0; }
                if (ATB_2_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 2; goto found; } } else { n_free = 0; }
                if (ATB_3_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 3; goto found; } } else { n_free = 0; }
                // *FORMAT-ON*
            }

            // No free blocks found on this heap. Mark this heap as
            // filled, so we won't try to find free space here again until
            // space is freed.
            #if MICROPY_GC_SPLIT_HEAP
            if (n_blocks == 1) {
                area->gc_last_free_atb_index = (i + 1) / BLOCKS_PER_ATB; // or (size_t)-1
            }
            #endif
        }
        #endif

        GC_EXIT();
        // nothing found!
        if (collected) {
            #if MICROPY_GC_SPLIT_HEAP_AUTO
            if (!added && gc_try_add_heap(n_bytes)) {
                added = true;
                continue;
            }
            #endif
            return NULL;
        }
        DEBUG_printf("gc_alloc(" UINT_FMT "): no free mem, triggering GC\n", n_bytes);
        gc_collect();
        collected = 1;
        GC_ENTER();
    }

    // found, ending at block i inclusive
found:
    // get starting and end blocks, both inclusive
    end_block = i;
    start_block = i - n_free + 1;

    #if MICROPY_GC_ALLOC_HINTS
    gc_hints_allocated(area, hint_class, start_block, n_blocks, hint_passed);
    #else
    // Set last free ATB index to block after last block we found, for start of
    // next scan.  To reduce fragmentation, we only do this if we were looking
    // for a single free block, which guarantees that there are no free blocks
    // before this one.  Also, whenever we free or shink a block we must check
    // if this index needs adjusting (see gc_realloc and gc_free).
    if (n_free == 1) {
        #if MICROPY_GC_SPLIT_HEAP
        MP_STATE_MEM(gc_last_free_area) = area;
        #endif
        area->gc_last_free_atb_index = (i + 1) / BLOCKS_PER_ATB;
    }
    #endif

    area->gc_last_used_block = MAX(area->gc_last_used_block, end_block);

    // mark first block as used head
    ATB_FREE_TO_HEAD(area, start_block);

    // mark rest of blocks as used tail
    // TODO for a run of many blocks can make this more efficient
    for (size_t bl = start_block + 1; bl <= end_block; bl++) {
        ATB_FREE_TO_TAIL(area, bl);
    }

    // get pointer to first block
    // we must create this pointer before unlocking the GC so a collection can find it
    void *ret_ptr = (void *)(area->gc_pool_start + start_block * BYTES_PER_BLOCK);
    DEBUG_printf("gc_alloc(%p)\n", ret_ptr);

    #if MICROPY_GC_ALLOC_THRESHOLD
    MP_STATE_MEM(gc_alloc_amount) += n_blocks;
    #endif

    GC_EXIT();

    #if MICROPY_GC_CONSERVATIVE_CLEAR
    // be conservative and zero out all the newly allocated blocks
    memset((byte *)ret_ptr, 0, (end_block - start_block + 1) * BYTES_PER_BLOCK);
    #else
    // zero out the additional bytes of the newly allocated blocks
    // This is needed because the blocks may have previously held pointers
    // to the heap and will not be set to something else if the caller
    // doesn't actually use the entire block.  As such they will continue
    // to point to the heap and may prevent other blocks from being reclaimed.
    memset((byte *)ret_ptr + n_bytes, 0, (end_block - start_block + 1) * BYTES_PER_BLOCK - n_bytes);
    #endif

    #if MICROPY_ENABLE_FINALISER
    if (has_finaliser) {
        // clear type pointer in case it is never set
        ((mp_obj_base_t *)ret_ptr)->type = NULL;
        // set mp_obj flag only if it has a finaliser
        GC_ENTER();
        FTB_SET(area, start_block);
        GC_EXIT();
    }
    #else
    (void)has_finaliser;
    #endif

    #if EXTENSIVE_HEAP_PROFILING
    gc_dump_alloc_table(&mp_plat_print);
    #endif

    return ret_ptr;
}

// force the freeing of a piece of memory
// TODO: freeing here does not call finaliser
void gc_free(void *ptr) {
    // Cannot free while the GC is locked, unless we're only doing a gc sweep.
    // However free is an optimisation to reclaim the memory immediately, this
    // means it will now be left until the next collection.
    //
    // (We have the optimisation to free immediately from inside a gc sweep so
    // that finalisers can free more memory when trying to avoid MemoryError.)
    if (MP_STATE_THREAD(gc_lock_depth) & ~GC_COLLECT_FLAG) {
        return;
    }

    GC_ENTER();

    DEBUG_printf("gc_free(%p)\n", ptr);

    if (ptr == NULL) {
        // free(NULL) is a no-op
        GC_EXIT();
        return;
    }

    // get the GC block number corresponding to this pointer
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    assert(area);
    #else
    assert(VERIFY_PTR(ptr));
    area = &MP_STATE_MEM(area);
    #endif

    size_t block = BLOCK_FROM_PTR(area, ptr);
    assert(ATB_GET_KIND(area, block) == AT_HEAD
        || (ATB_GET_KIND(area, block) == AT_MARK && (MP_STATE_THREAD(gc_lock_depth) & GC_COLLECT_FLAG)));

    #if MICROPY_ENABLE_FINALISER
    FTB_CLEAR(area, block);
    #endif

    #if MICROPY_PY_WEAKREF
    // Objects that have a weak reference should not be explicitly freed.
    assert(!WTB_GET(area, block));
    #endif

    #if MICROPY_GC_SPLIT_HEAP
    if (MP_STATE_MEM(gc_last_free_area) != area) {
        // We freed something but it isn't the current area. Reset the
        // last free area to the start for a rescan. Note that this won't
        // give much of a performance hit, since areas that are completely
        // filled will likely be skipped (the gc_last_free_atb_index
        // points to the last block).
        // The reason why this is necessary is because it is not possible
        // to see which area came first (like it is possible to adjust
        // gc_last_free_atb_index based on whether the freed block is
        // before the last free block).
        MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
    }
    #endif

    #if MICROPY_GC_ALLOC_HINTS
    // free head and all of its tail blocks, then lower the hints that can use them
    size_t first_block = block;
    do {
        ATB_ANY_TO_FREE(area, block);
        block += 1;
    } while (ATB_GET_KIND(area, block) == AT_TAIL);
    gc_hints_freed(area, first_block, block - first_block);
    #else
    // set the last_free pointer to this block if it's earlier in the heap
    if (block / BLOCKS_PER_ATB < area->gc_last_free_atb_index) {
        area->gc_last_free_atb_index = block / BLOCKS_PER_ATB;
    }

    // free head and all of its tail blocks
    do {
        ATB_ANY_TO_FREE(area, block);
        block += 1;
    } while (ATB_GET_KIND(area, block) == AT_TAIL);
    #endif

    GC_EXIT();

    #if EXTENSIVE_HEAP_PROFILING
    gc_dump_alloc_table(&mp_plat_print);
    #endif
}

size_t gc_nbytes(const void *ptr) {
    GC_ENTER();

    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    #else
    if (VERIFY_PTR(ptr)) {
        area = &MP_STATE_MEM(area);
    } else {
        area = NULL;
    }
    #endif

    if (area) {
        size_t block = BLOCK_FROM_PTR(area, ptr);
        if (ATB_GET_KIND(area, block) == AT_HEAD) {
            // work out number of consecutive blocks in the chain starting with this on
            size_t n_blocks = 0;
            do {
                n_blocks += 1;
            } while (ATB_GET_KIND(area, block + n_blocks) == AT_TAIL);
            GC_EXIT();
            return n_blocks * BYTES_PER_BLOCK;
        }
    }

    // invalid pointer
    GC_EXIT();
    return 0;
}

void *gc_realloc(void *ptr_in, size_t n_bytes, bool allow_move) {
    // check for pure allocation
    if (ptr_in == NULL) {
        return gc_alloc(n_bytes, false);
    }

    // check for pure free
    if (n_bytes == 0) {
        gc_free(ptr_in);
        return NULL;
    }

    if (MP_STATE_THREAD(gc_lock_depth) > 0) {
        return NULL;
    }

    void *ptr = ptr_in;

    GC_ENTER();

    // get the GC block number corresponding to this pointer
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    assert(area);
    #else
    assert(VERIFY_PTR(ptr));
    area = &MP_STATE_MEM(area);
    #endif
    size_t block = BLOCK_FROM_PTR(area, ptr);
    assert(ATB_GET_KIND(area, block) == AT_HEAD);

    // compute number of new blocks that are requested
    size_t new_blocks = (n_bytes + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK;

    // Get the total number of consecutive blocks that are already allocated to
    // this chunk of memory, and then count the number of free blocks following
    // it.  Stop if we reach the end of the heap, or if we find enough extra
    // free blocks to satisfy the realloc.  Note that we need to compute the
    // total size of the existing memory chunk so we can correctly and
    // efficiently shrink it (see below for shrinking code).
    size_t n_free = 0;
    size_t n_blocks = 1; // counting HEAD block
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    for (size_t bl = block + n_blocks; bl < max_block; bl++) {
        byte block_type = ATB_GET_KIND(area, bl);
        if (block_type == AT_TAIL) {
            n_blocks++;
            continue;
        }
        if (block_type == AT_FREE) {
            n_free++;
            if (n_blocks + n_free >= new_blocks) {
                // stop as soon as we find enough blocks for n_bytes
                break;
            }
            continue;
        }
        break;
    }

    // return original ptr if it already has the requested number of blocks
    if (new_blocks == n_blocks) {
        GC_EXIT();
        return ptr_in;
    }

    // check if we can shrink the allocated area
    if (new_blocks < n_blocks) {
        // free unneeded tail blocks
        for (size_t bl = block + new_blocks, count = n_blocks - new_blocks; count > 0; bl++, count--) {
            ATB_ANY_TO_FREE(area, bl);
        }

        #if MICROPY_GC_SPLIT_HEAP
        if (MP_STATE_MEM(gc_last_free_area) != area) {
            // See comment in gc_free.
            MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
        }
        #endif

        #if MICROPY_GC_ALLOC_HINTS
        gc_hints_freed(area, block + new_blocks, n_blocks - new_blocks);
        #else
        // set the last_free pointer to end of this block if it's earlier in the heap
        if ((block + new_blocks) / BLOCKS_PER_ATB < area->gc_last_free_atb_index) {
            area->gc_last_free_atb_index = (block + new_blocks) / BLOCKS_PER_ATB;
        }
        #endif

        GC_EXIT();

        #if EXTENSIVE_HEAP_PROFILING
        gc_dump_alloc_table(&mp_plat_print);
        #endif

        return ptr_in;
    }

    // check if we can expand in place
    if (new_blocks <= n_blocks + n_free) {
        // mark few more blocks as used tail
        size_t end_block = block + new_blocks;
        for (size_t bl = block + n_blocks; bl < end_block; bl++) {
            assert(ATB_GET_KIND(area, bl) == AT_FREE);
            ATB_FREE_TO_TAIL(area, bl);
        }

        area->gc_last_used_block = MAX(area->gc_last_used_block, end_block);

        GC_EXIT();

        #if MICROPY_GC_CONSERVATIVE_CLEAR
        // be conservative and zero out all the newly allocated blocks
        memset((byte *)ptr_in + n_blocks * BYTES_PER_BLOCK, 0, (new_blocks - n_blocks) * BYTES_PER_BLOCK);
        #else
        // zero out the additional bytes of the newly allocated blocks (see comment above in gc_alloc)
        memset((byte *)ptr_in + n_bytes, 0, new_blocks * BYTES_PER_BLOCK - n_bytes);
        #endif

        #if EXTENSIVE_HEAP_PROFILING
        gc_dump_alloc_table(&mp_plat_print);
        #endif

        return ptr_in;
    }

    #if MICROPY_ENABLE_FINALISER
    bool ftb_state = FTB_GET(area, block);
    #else
    bool ftb_state = false;
    #endif

    GC_EXIT();

    if (!allow_move) {
        // not allowed to move memory block so return failure
        return NULL;
    }

    // can't resize inplace; try to find a new contiguous chain
    void *ptr_out = gc_alloc(n_bytes, ftb_state);

    // check that the alloc succeeded
    if (ptr_out == NULL) {
        return NULL;
    }

    DEBUG_printf("gc_realloc(%p -> %p)\n", ptr_in, ptr_out);
    memcpy(ptr_out, ptr_in, n_blocks * BYTES_PER_BLOCK);
    gc_free(ptr_in);
    return ptr_out;
}

void gc_dump_info(const mp_print_t *print) {
    gc_info_t info;
    gc_info(&info);
    mp_printf(print, "GC: total: %u, used: %u, free: %u",
        (uint)info.total, (uint)info.used, (uint)info.free);
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    mp_printf(print, ", max new split: %u", (uint)info.max_new_split);
    #endif
    mp_printf(print, "\n No. of 1-blocks: %u, 2-blocks: %u, max blk sz: %u, max free sz: %u\n",
        (uint)info.num_1block, (uint)info.num_2block, (uint)info.max_block, (uint)info.max_free);
}

void gc_dump_alloc_table(const mp_print_t *print) {
    GC_ENTER();
    static const size_t DUMP_BYTES_PER_LINE = 64;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        #if !EXTENSIVE_HEAP_PROFILING
        // When comparing heap output we don't want to print the starting
        // pointer of the heap because it changes from run to run.
        mp_printf(print, "GC memory layout; from %p:", area->gc_pool_start);
        #endif
        for (size_t bl = 0; bl < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; bl++) {
            if (bl % DUMP_BYTES_PER_LINE == 0) {
                // a new line of blocks
                {
                    // check if this line contains only free blocks
                    size_t bl2 = bl;
                    while (bl2 < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB && ATB_GET_KIND(area, bl2) == AT_FREE) {
                        bl2++;
                    }
                    if (bl2 - bl >= 2 * DUMP_BYTES_PER_LINE) {
                        // there are at least 2 lines containing only free blocks, so abbreviate their printing
                        mp_printf(print, "\n       (%u lines all free)", (uint)((bl2 - bl) / DUMP_BYTES_PER_LINE));
                        bl = bl2 & (~(DUMP_BYTES_PER_LINE - 1));
                        if (bl >= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB) {
                            // got to end of heap
                            break;
                        }
                    }
                }
                // print header for new line of blocks
                // (the cast to uint32_t is for 16-bit ports)
                mp_printf(print, "\n%08x: ", (uint)(bl * BYTES_PER_BLOCK));
            }
            int c = ' ';
            switch (ATB_GET_KIND(area, bl)) {
                case AT_FREE:
                    c = '.';
                    break;
                /* this prints out if the object is reachable from BSS or STACK (for unix only)
                case AT_HEAD: {
                    c = 'h';
                    void **ptrs = (void**)(void*)&mp_state_ctx;
                    mp_uint_t len = offsetof(mp_state_ctx_t, vm.stack_top) / sizeof(mp_uint_t);
                    for (mp_uint_t i = 0; i < len; i++) {
                        mp_uint_t ptr = (mp_uint_t)ptrs[i];
                        if (gc_get_ptr_area(ptr) && BLOCK_FROM_PTR(ptr) == bl) {
                            c = 'B';
                            break;
                        }
                    }
                    if (c == 'h') {
                        ptrs = (void**)&c;
                        len = ((mp_uint_t)MP_STATE_THREAD(stack_top) - (mp_uint_t)&c) / sizeof(mp_uint_t);
                        for (mp_uint_t i = 0; i < len; i++) {
                            mp_uint_t ptr = (mp_uint_t)ptrs[i];
                            if (gc_get_ptr_area(ptr) && BLOCK_FROM_PTR(ptr) == bl) {
                                c = 'S';
                                break;
                            }
                        }
                    }
                    break;
                }
                */
                /* this prints the MicroPython object type of the head block */
                case AT_HEAD: {
                    void **ptr = (void **)(area->gc_pool_start + bl * BYTES_PER_BLOCK);
                    if (*ptr == &mp_type_tuple) {
                        c = 'T';
                    } else if (*ptr == &mp_type_list) {
                        c = 'L';
                    } else if (*ptr == &mp_type_dict) {
                        c = 'D';
                    } else if (*ptr == &mp_type_str || *ptr == &mp_type_bytes) {
                        c = 'S';
                    }
                    #if MICROPY_PY_BUILTINS_BYTEARRAY
                    else if (*ptr == &mp_type_bytearray) {
                        c = 'A';
                    }
                    #endif
                    #if MICROPY_PY_ARRAY
                    else if (*ptr == &mp_type_array) {
                        c = 'A';
                    }
                    #endif
                    #if MICROPY_PY_BUILTINS_FLOAT
                    else if (*ptr == &mp_type_float) {
                        c = 'F';
                    }
                    #endif
                    else if (*ptr == &mp_type_fun_bc) {
                        c = 'B';
                    } else if (*ptr == &mp_type_module) {
                        c = 'M';
                    } else {
                        c = 'h';
                        #if 0
                        // This code prints "Q" for qstr-pool data, and "q" for qstr-str
                        // data.  It can be useful to see how qstrs are being allocated,
                        // but is disabled by default because it is very slow.
                        for (qstr_pool_t *pool = MP_STATE_VM(last_pool); c == 'h' && pool != NULL; pool = pool->prev) {
                            if ((qstr_pool_t *)ptr == pool) {
                                c = 'Q';
                                break;
                            }
                            for (const byte **q = pool->qstrs, **q_top = pool->qstrs + pool->len; q < q_top; q++) {
                                if ((const byte *)ptr == *q) {
                                    c = 'q';
                                    break;
                                }
                            }
                        }
                        #endif
                    }
                    break;
                }
                case AT_TAIL:
                    c = '=';
                    break;
                case AT_MARK:
                    c = 'm';
                    break;
            }
            mp_printf(print, "%c", c);
        }
        mp_print_str(print, "\n");
    }
    GC_EXIT();
}

#endif // MICROPY_ENABLE_GC
