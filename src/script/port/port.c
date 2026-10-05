// MicroPython's port for OpenSE4's script runtime (docs/sdk/runtime.md): what the
// interpreter asks of its host (output, module files, garbage collection roots),
// the hooks our patches add (budget, call depth, identity, sorting, imports from
// the bytecode cache), the math functions, and the C interface of port.h.
//
// One interpreter runs at a time per process, so the port keeps its state in
// globals, as MicroPython itself does.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/mpz.h"
#include "py/objint.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/objtype.h"
#include "py/persistentcode.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "shared/runtime/gchelper.h"

#include "port.h"

// Constant folding while a module is compiled for the cache gets this budget of its
// own: a module compiles to the same bytecode whether or not the cache had it, and
// a huge constant expression is left for run time, where it is paid for.
#define OSE_COMPILE_ALLOWANCE INT64_C(10000000)
// Running a script's __str__ or __repr__ while an error is described.
#define OSE_FORMAT_ALLOWANCE INT64_C(1000000)

int64_t mp_opense4_budget_left = OSE_BUDGET_UNLIMITED;
size_t mp_opense4_depth = 0;
size_t mp_opense4_depth_limit = 200;

static struct {
    const ose_host *host;
    bool active;
    int protect_depth;
    mp_obj_t budget_exception;
    mp_obj_t recursion_error;
    mp_obj_t last_exception;     // kept reachable while the engine describes it
    // Objects the port holds on to (garbage collection roots).
    mp_obj_t *pins;
    size_t pins_len, pins_cap;
    // Identities of objects without a value hash: open addressing on the object's
    // address (or, for interned strings, its word); the values are given in order.
    uintptr_t *id_keys;
    uintptr_t *id_values;
    size_t id_cap, id_len;
    uintptr_t next_identity;
} ose;

static void ose_pin(mp_obj_t obj) {
    if (ose.pins_len == ose.pins_cap) {
        size_t cap = ose.pins_cap ? ose.pins_cap * 2 : 16;
        mp_obj_t *pins = realloc(ose.pins, cap * sizeof(mp_obj_t));
        if (pins == NULL) {
            m_malloc_fail(cap * sizeof(mp_obj_t));
        }
        ose.pins = pins;
        ose.pins_cap = cap;
    }
    ose.pins[ose.pins_len++] = obj;
}

// --- What MicroPython needs from a port -------------------------------------------------

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    if (ose.pins_len != 0) {
        gc_collect_root((void **)ose.pins, ose.pins_len);
    }
    if (ose.id_cap != 0) {
        gc_collect_root((void **)ose.id_keys, ose.id_cap);
    }
    gc_collect_root((void **)&ose.last_exception, 1);
    gc_collect_end();
}

MP_NORETURN void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "OpenSE4 script runtime: a Python exception was raised outside a protected call\n");
    abort();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    if (ose.host != NULL && ose.host->output != NULL) {
        ose.host->output(ose.host->ctx, str, len);
    }
    return len;
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

static bool ends_with(const char *s, size_t len, const char *suffix) {
    size_t n = strlen(suffix);
    return len >= n && memcmp(s + len - n, suffix, n) == 0;
}

// Modules exist only as the files the host gives the interpreter.
mp_import_stat_t mp_import_stat(const char *path) {
    size_t len = strlen(path);
    if (ends_with(path, len, ".py")) {
        size_t n;
        return ose.host->source(ose.host->ctx, path, &n) != NULL ? MP_IMPORT_STAT_FILE : MP_IMPORT_STAT_NO_EXIST;
    }
    if (ends_with(path, len, ".mpy")) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    return ose.host->is_package(ose.host->ctx, path) ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_NO_EXIST;
}

void mp_reader_new_file(mp_reader_t *reader, qstr filename) {
    size_t len;
    const char *text = ose.host->source(ose.host->ctx, qstr_str(filename), &len);
    if (text == NULL) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_reader_new_mem(reader, (const byte *)text, len, MP_READER_IS_ROM);
}

// Imports run cached bytecode: compiled once per process (see runtime.cpp), then
// loaded in place by every interpreter. Compiling and loading are set aside from the
// budget, so a module costs the same whether or not the cache already had it.
int mp_opense4_import_load(const char *path, mp_compiled_module_t *cm) {
    const ose_host *h = ose.host;
    size_t src_len;
    const char *src = h->source(h->ctx, path, &src_len);
    if (src == NULL) {
        return 0;
    }
    const int64_t saved = mp_opense4_budget_left;
    size_t mpy_len = 0;
    const uint8_t *mpy = h->compiled(h->ctx, path, &mpy_len);
    if (mpy == NULL) {
        mp_opense4_budget_left = OSE_COMPILE_ALLOWANCE;
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            qstr name = qstr_from_str(path);
            mp_lexer_t *lex = mp_lexer_new_from_str_len(name, src, src_len, 0);
            mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
            mp_compiled_module_t compiled;
            memset(&compiled, 0, sizeof(compiled));
            compiled.context = m_new_obj(mp_module_context_t);
            mp_compile_to_raw_code(&tree, name, false, &compiled);
            vstr_t vstr;
            vstr_init(&vstr, 1024);
            mp_print_t print = {&vstr, (mp_print_strn_t)vstr_add_strn};
            mp_raw_code_save(&compiled, &print);
            mpy = h->store_compiled(h->ctx, path, (const uint8_t *)vstr.buf, vstr.len, &mpy_len);
            vstr_clear(&vstr);
            nlr_pop();
        } else {
            // A syntax error (or no memory): MicroPython compiles the file again,
            // which raises the error with its place in the file.
            mp_opense4_budget_left = saved;
            return 0;
        }
        if (mpy == NULL) {
            mp_opense4_budget_left = saved;
            return 0;
        }
    }
    mp_opense4_budget_left = OSE_BUDGET_UNLIMITED;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_reader_t reader;
        mp_reader_new_mem(&reader, mpy, mpy_len, MP_READER_IS_ROM);
        mp_raw_code_load(&reader, cm);
        nlr_pop();
    } else {
        mp_opense4_budget_left = saved;
        nlr_jump(nlr.ret_val);
    }
    mp_opense4_budget_left = saved;
    return 1;
}

// --- Budget and call depth --------------------------------------------------------------

void *mp_opense4_budget_exception(void) {
    return MP_OBJ_TO_PTR(ose.budget_exception);
}

void mp_opense4_budget_exhausted(void) {
    nlr_raise(ose.budget_exception);
}

void mp_opense4_depth_exceeded(void) {
    if (ose.recursion_error != MP_OBJ_NULL) {
        nlr_raise(mp_call_function_1(ose.recursion_error,
            MP_OBJ_NEW_QSTR(MP_QSTR_maximum_space_recursion_space_depth_space_exceeded)));
    }
    mp_raise_recursion_depth();
}

void ose_budget_set(int64_t left) {
    mp_opense4_budget_left = left;
}

int64_t ose_budget_get(void) {
    return mp_opense4_budget_left;
}

// --- Identity -------------------------------------------------------------------------

uintptr_t mp_opense4_new_identity(void) {
    return ose.next_identity++;
}

static size_t id_slot(uintptr_t key, size_t cap) {
    uint64_t h = (uint64_t)key * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(h >> 32) & (cap - 1);
}

static void id_grow(void) {
    size_t cap = ose.id_cap ? ose.id_cap * 2 : 64;
    uintptr_t *keys = calloc(cap, sizeof(uintptr_t));
    uintptr_t *values = calloc(cap, sizeof(uintptr_t));
    if (keys == NULL || values == NULL) {
        free(keys);
        free(values);
        m_malloc_fail(cap * sizeof(uintptr_t));
    }
    for (size_t i = 0; i < ose.id_cap; i++) {
        if (ose.id_keys[i] != 0) {
            size_t s = id_slot(ose.id_keys[i], cap);
            while (keys[s] != 0) {
                s = (s + 1) & (cap - 1);
            }
            keys[s] = ose.id_keys[i];
            values[s] = ose.id_values[i];
        }
    }
    free(ose.id_keys);
    free(ose.id_values);
    ose.id_keys = keys;
    ose.id_values = values;
    ose.id_cap = cap;
}

uintptr_t mp_opense4_identity(const void *p) {
    mp_obj_t o = (mp_obj_t)p;
    if (mp_obj_is_obj(o)) {
        const mp_obj_type_t *type = ((const mp_obj_base_t *)p)->type;
        if (type != NULL && (type->flags & MP_TYPE_FLAG_INSTANCE_TYPE)) {
            const mp_obj_instance_t *instance = p;
            if (instance->identity != 0) {
                return instance->identity;
            }
        }
    }
    uintptr_t key = (uintptr_t)p;
    if (ose.id_cap != 0) {
        for (size_t s = id_slot(key, ose.id_cap);; s = (s + 1) & (ose.id_cap - 1)) {
            if (ose.id_keys[s] == key) {
                return ose.id_values[s];
            }
            if (ose.id_keys[s] == 0) {
                break;
            }
        }
    }
    if (2 * (ose.id_len + 1) > ose.id_cap) {
        id_grow();
    }
    size_t s = id_slot(key, ose.id_cap);
    while (ose.id_keys[s] != 0) {
        s = (s + 1) & (ose.id_cap - 1);
    }
    ose.id_keys[s] = key;
    ose.id_values[s] = ose.next_identity++;
    ose.id_len++;
    return ose.id_values[s];
}

// --- list.sort --------------------------------------------------------------------------
// A bottom-up merge sort: stable, O(n log n) comparisons whatever the input, the key
// called once per item, and reverse=True keeping equal items in their order, as in
// CPython. The list is empty while it is sorted (changing it is an error), and on an
// exception it holds its items in some order, none lost.

#define OSE_SORT_RUN 8

typedef struct {
    mp_obj_t *items, *keys;         // the list's array, and the keys (or NULL)
    mp_obj_t *tmp_items, *tmp_keys;
    mp_obj_t *volatile cur_items;   // the array holding every item right now
    mp_obj_t *volatile cur_keys;
    bool reverse;
} ose_sort_t;

static bool sort_less(const ose_sort_t *s, mp_obj_t a, mp_obj_t b) {
    MICROPY_BUDGET_CHARGE(1);
    mp_obj_t r = s->reverse ? mp_binary_op(MP_BINARY_OP_LESS, b, a) : mp_binary_op(MP_BINARY_OP_LESS, a, b);
    return mp_obj_is_true(r);
}

static void sort_run(ose_sort_t *s, size_t lo, size_t hi) {
    mp_obj_t *items = s->items;
    mp_obj_t *keys = s->keys ? s->keys : s->items;
    for (size_t i = lo + 1; i < hi; i++) {
        // swaps rather than shifts: the array is a permutation at every comparison
        for (size_t j = i; j > lo && sort_less(s, keys[j], keys[j - 1]); j--) {
            mp_obj_t t = items[j];
            items[j] = items[j - 1];
            items[j - 1] = t;
            if (s->keys) {
                t = s->keys[j];
                s->keys[j] = s->keys[j - 1];
                s->keys[j - 1] = t;
            }
        }
    }
}

static void sort_merge(const ose_sort_t *s, mp_obj_t *src_items, mp_obj_t *src_keys, mp_obj_t *dst_items,
    mp_obj_t *dst_keys, size_t lo, size_t mid, size_t hi) {
    mp_obj_t *src_k = src_keys ? src_keys : src_items;
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        // take from the right only when it is strictly less: stable
        size_t from = sort_less(s, src_k[j], src_k[i]) ? j++ : i++;
        dst_items[k] = src_items[from];
        if (dst_keys) {
            dst_keys[k] = src_keys[from];
        }
        k++;
    }
    for (; i < mid; i++, k++) {
        dst_items[k] = src_items[i];
        if (dst_keys) {
            dst_keys[k] = src_keys[i];
        }
    }
    for (; j < hi; j++, k++) {
        dst_items[k] = src_items[j];
        if (dst_keys) {
            dst_keys[k] = src_keys[j];
        }
    }
}

static void sort_all(ose_sort_t *s, size_t n) {
    for (size_t lo = 0; lo < n; lo += OSE_SORT_RUN) {
        sort_run(s, lo, lo + OSE_SORT_RUN < n ? lo + OSE_SORT_RUN : n);
    }
    mp_obj_t *src_items = s->items, *src_keys = s->keys;
    mp_obj_t *dst_items = s->tmp_items, *dst_keys = s->tmp_keys;
    for (size_t width = OSE_SORT_RUN; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            sort_merge(s, src_items, src_keys, dst_items, dst_keys, lo, mid, hi);
        }
        mp_obj_t *t = src_items;
        src_items = dst_items;
        dst_items = t;
        t = src_keys;
        src_keys = dst_keys;
        dst_keys = t;
        s->cur_items = src_items;
        s->cur_keys = src_keys;
    }
}

void mp_opense4_list_sort(struct _mp_obj_list_t *list, void *key_fn_in, int reverse) {
    mp_obj_t key_fn = (mp_obj_t)key_fn_in;
    size_t n = list->len;
    if (n < 2) {
        return;
    }
    mp_obj_t *items = list->items;
    size_t alloc = list->alloc;
    mp_obj_t *placeholder = m_new(mp_obj_t, 4);
    list->items = placeholder;
    list->alloc = 4;
    list->len = 0;
    mp_seq_clear(placeholder, 0, 4, sizeof(mp_obj_t));

    ose_sort_t s;
    memset(&s, 0, sizeof(s));
    s.items = items;
    s.cur_items = items;
    s.reverse = reverse != 0;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        if (key_fn != MP_OBJ_NULL) {
            s.keys = m_new(mp_obj_t, n);
            mp_seq_clear(s.keys, 0, n, sizeof(mp_obj_t));
            for (size_t i = 0; i < n; i++) {
                s.keys[i] = mp_call_function_1(key_fn, items[i]);
            }
            s.tmp_keys = m_new(mp_obj_t, n);
        }
        s.cur_keys = s.keys;
        s.tmp_items = m_new(mp_obj_t, n);
        sort_all(&s, n);
        nlr_pop();
    } else {
        if (s.cur_items != items) {
            memcpy(items, s.cur_items, n * sizeof(mp_obj_t));
        }
        list->items = items;
        list->alloc = alloc;
        list->len = n;
        nlr_jump(nlr.ret_val);
    }
    if (s.cur_items != items) {
        memcpy(items, s.cur_items, n * sizeof(mp_obj_t));
    }
    bool modified = list->len != 0;
    list->items = items;
    list->alloc = alloc;
    list->len = n;
    if (modified) {
        mp_raise_ValueError(MP_ERROR_TEXT("list modified during sort"));
    }
}

// --- Math ---------------------------------------------------------------------------------
// Transcendental functions: the bundled musl code (lib/libm_dbl, renamed mp_musl_*),
// identical on every platform. Exact operations: the platform's own.

double mp_musl_acos(double);
double mp_musl_acosh(double);
double mp_musl_asin(double);
double mp_musl_asinh(double);
double mp_musl_atan(double);
double mp_musl_atan2(double, double);
double mp_musl_atanh(double);
double mp_musl_cos(double);
double mp_musl_cosh(double);
double mp_musl_erf(double);
double mp_musl_erfc(double);
double mp_musl_exp(double);
double mp_musl_expm1(double);
double mp_musl_lgamma(double);
double mp_musl_log(double);
double mp_musl_log10(double);
double mp_musl_log1p(double);
double mp_musl_pow(double, double);
double mp_musl_sin(double);
double mp_musl_sinh(double);
double mp_musl_tan(double);
double mp_musl_tanh(double);
double mp_musl_tgamma(double);

double mp_libm_sin(double x) { return mp_musl_sin(x); }
double mp_libm_cos(double x) { return mp_musl_cos(x); }
double mp_libm_tan(double x) { return mp_musl_tan(x); }
double mp_libm_asin(double x) { return mp_musl_asin(x); }
double mp_libm_acos(double x) { return mp_musl_acos(x); }
double mp_libm_atan(double x) { return mp_musl_atan(x); }
double mp_libm_atan2(double y, double x) { return mp_musl_atan2(y, x); }
double mp_libm_sinh(double x) { return mp_musl_sinh(x); }
double mp_libm_cosh(double x) { return mp_musl_cosh(x); }
double mp_libm_tanh(double x) { return mp_musl_tanh(x); }
double mp_libm_asinh(double x) { return mp_musl_asinh(x); }
double mp_libm_acosh(double x) { return mp_musl_acosh(x); }
double mp_libm_atanh(double x) { return mp_musl_atanh(x); }
double mp_libm_exp(double x) { return mp_musl_exp(x); }
double mp_libm_expm1(double x) { return mp_musl_expm1(x); }
double mp_libm_log(double x) { return mp_musl_log(x); }
double mp_libm_log1p(double x) { return mp_musl_log1p(x); }
double mp_libm_pow(double x, double y) { return mp_musl_pow(x, y); }
double mp_libm_erf(double x) { return mp_musl_erf(x); }
double mp_libm_erfc(double x) { return mp_musl_erfc(x); }
double mp_libm_lgamma(double x) { return mp_musl_lgamma(x); }
double mp_libm_tgamma(double x) { return mp_musl_tgamma(x); }

// Exact for powers of two; otherwise log(x) / log(2).
double mp_libm_log2(double x) {
    if (x > 0 && isfinite(x)) {
        int e;
        if (frexp(x, &e) == 0.5) {
            return (double)(e - 1);
        }
    }
    return mp_musl_log(x) / 0.69314718055994530942;
}

// Exact for the powers of ten a double holds exactly; otherwise musl's log10.
double mp_libm_log10(double x) {
    double p = 1;
    for (int k = 0; k <= 22; k++, p *= 10) {
        if (x == p) {
            return (double)k;
        }
    }
    return mp_musl_log10(x);
}

double mp_libm_sqrt(double x) { return sqrt(x); }
double mp_libm_fabs(double x) { return fabs(x); }
double mp_libm_floor(double x) { return floor(x); }
double mp_libm_ceil(double x) { return ceil(x); }
double mp_libm_trunc(double x) { return trunc(x); }
double mp_libm_nearbyint(double x) { return nearbyint(x); }
double mp_libm_fmod(double x, double y) {
    // fmod(x, inf) is x; old C libraries (msvcrt) get it wrong
    return (!isinf(x) && isinf(y)) ? x : fmod(x, y);
}
double mp_libm_copysign(double x, double y) { return copysign(x, y); }
double mp_libm_frexp(double x, int *e) { return frexp(x, e); }
double mp_libm_ldexp(double x, int e) { return ldexp(x, e); }
double mp_libm_modf(double x, double *iptr) { return modf(x, iptr); }
double mp_libm_nan(const char *tag) { return nan(tag); }
int mp_libm_isfinite(double x) { return isfinite(x) != 0; }
int mp_libm_isinf(double x) { return isinf(x) != 0; }
int mp_libm_isnan(double x) { return isnan(x) != 0; }

// --- port.h: lifecycle --------------------------------------------------------------------

int ose_init(const ose_host *host, void *heap, size_t heap_size, size_t depth_limit) {
    if (ose.active) {
        return 1;
    }
    memset(&ose, 0, sizeof(ose));
    ose.host = host;
    ose.active = true;
    ose.next_identity = 1;
    ose.budget_exception = MP_OBJ_NULL;
    ose.recursion_error = MP_OBJ_NULL;
    mp_opense4_budget_left = OSE_BUDGET_UNLIMITED;
    mp_opense4_depth = 0;
    mp_opense4_depth_limit = depth_limit;
    gc_init(heap, (char *)heap + heap_size);
    mp_init();
    return 0;
}

void ose_deinit(void) {
    if (!ose.active) {
        return;
    }
    mp_deinit();
    free(ose.pins);
    free(ose.id_keys);
    free(ose.id_values);
    memset(&ose, 0, sizeof(ose));
    mp_opense4_budget_left = OSE_BUDGET_UNLIMITED;
    mp_opense4_depth = 0;
}

void ose_enter(void *stack_top, size_t stack_limit) {
    mp_cstack_init_with_top(stack_top, stack_limit);
}

size_t ose_heap_used(void) {
    gc_info_t info;
    gc_info(&info);
    return info.used;
}

int ose_protect(void (*fn)(void *ctx), void *ctx, ose_obj *exception) {
    if (ose.protect_depth++ == 0) {
        mp_opense4_depth = 0;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        fn(ctx);
        nlr_pop();
        ose.protect_depth--;
        return 0;
    }
    ose.protect_depth--;
    if (ose.protect_depth == 0) {
        ose.last_exception = MP_OBJ_FROM_PTR(nlr.ret_val);
    }
    *exception = MP_OBJ_FROM_PTR(nlr.ret_val);
    return 1;
}

static mp_obj_t make_class(const char *name, const mp_obj_type_t *base) {
    mp_obj_t base_obj = MP_OBJ_FROM_PTR(base);
    mp_obj_t args[3] = {
        MP_OBJ_NEW_QSTR(qstr_from_str(name)),
        mp_obj_new_tuple(1, &base_obj),
        mp_obj_new_dict(0),
    };
    return mp_call_function_n_kw(MP_OBJ_FROM_PTR(&mp_type_type), 3, 0, args);
}

void ose_setup(void) {
    mp_obj_t budget_type = make_class("BudgetExceeded", &mp_type_BaseException);
    ose_pin(budget_type);
    ose.budget_exception = mp_call_function_1(budget_type,
        mp_obj_new_str_from_cstr("the script used up its budget of bytecodes"));
    ose_pin(ose.budget_exception);
    ose.recursion_error = make_class("RecursionError", &mp_type_RuntimeError);
    ose_pin(ose.recursion_error);
    // RecursionError as in CPython (MicroPython has none)
    if (MP_STATE_VM(mp_module_builtins_override_dict) == NULL) {
        MP_STATE_VM(mp_module_builtins_override_dict) = MP_OBJ_TO_PTR(mp_obj_new_dict(1));
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(MP_STATE_VM(mp_module_builtins_override_dict)),
        MP_OBJ_NEW_QSTR(qstr_from_str("RecursionError")), ose.recursion_error);
}

ose_obj ose_import(const char *module) {
    return mp_import_name(qstr_from_str(module), mp_const_true, MP_OBJ_NEW_SMALL_INT(0));
}

ose_obj ose_getattr_maybe(ose_obj obj, const char *name) {
    mp_obj_t dest[2];
    mp_load_method_maybe((mp_obj_t)obj, qstr_from_str(name), dest);
    if (dest[0] == MP_OBJ_NULL) {
        return NULL;
    }
    if (dest[1] != MP_OBJ_NULL) {
        return mp_obj_new_bound_meth(dest[0], dest[1]);
    }
    return dest[0];
}

ose_obj ose_call(ose_obj fn, size_t n_args, const ose_obj *args) {
    return mp_call_function_n_kw((mp_obj_t)fn, n_args, 0, (const mp_obj_t *)args);
}

ose_obj ose_exec(const char *source, size_t len, const char *name, int mode) {
    qstr qname = qstr_from_str(name);
    mp_lexer_t *lex = mp_lexer_new_from_str_len(qname, source, len, 0);
    mp_obj_dict_t *globals = mp_globals_get();
    return mp_parse_compile_execute(lex, mode ? MP_PARSE_EVAL_INPUT : MP_PARSE_FILE_INPUT, globals, globals);
}

// --- port.h: exceptions -----------------------------------------------------------------

static bool is_recursion_error(mp_obj_t exc) {
    const mp_obj_type_t *type = mp_obj_get_type(exc);
    if (ose.recursion_error != MP_OBJ_NULL && mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(type), ose.recursion_error)) {
        return true;
    }
    if (type == &mp_type_RuntimeError) {
        // the C stack check's error
        mp_obj_t arg = mp_obj_exception_get_value(exc);
        return mp_obj_is_qstr(arg) && MP_OBJ_QSTR_VALUE(arg) == MP_QSTR_maximum_space_recursion_space_depth_space_exceeded;
    }
    return false;
}

int ose_exception_kind(ose_obj exc_in) {
    mp_obj_t exc = (mp_obj_t)exc_in;
    if (exc == ose.budget_exception) {
        return OSE_EXC_BUDGET;
    }
    mp_obj_t type = MP_OBJ_FROM_PTR(mp_obj_get_type(exc));
    if (mp_obj_is_subclass_fast(type, MP_OBJ_FROM_PTR(&mp_type_SyntaxError))) {
        return OSE_EXC_SYNTAX;
    }
    if (mp_obj_is_subclass_fast(type, MP_OBJ_FROM_PTR(&mp_type_MemoryError))) {
        return OSE_EXC_MEMORY;
    }
    if (is_recursion_error(exc)) {
        return OSE_EXC_RECURSION;
    }
    return OSE_EXC_OTHER;
}

const char *ose_exception_type(ose_obj exc) {
    return mp_obj_get_type_str((mp_obj_t)exc);
}

typedef struct {
    void (*out)(void *ctx, const char *text, size_t len);
    void *ctx;
    mp_obj_t exc;
    bool traceback;
} ose_format_t;

static void format_strn(void *data, const char *str, size_t len) {
    ose_format_t *f = data;
    f->out(f->ctx, str, len);
}

static void format_run(void *data) {
    ose_format_t *f = data;
    mp_print_t print = {f, format_strn};
    if (f->traceback) {
        mp_obj_print_exception(&print, f->exc);
    } else {
        mp_obj_print_helper(&print, f->exc, PRINT_STR);
    }
}

// A script's own __str__ may fail or run long: it gets a budget of its own, and the
// type name stands in when it fails.
static void format_exception(ose_obj exc, bool traceback, void (*out)(void *ctx, const char *text, size_t len), void *ctx) {
    ose_format_t f = {out, ctx, (mp_obj_t)exc, traceback};
    int64_t saved_budget = mp_opense4_budget_left;
    size_t saved_depth = mp_opense4_depth;
    mp_obj_t saved_exception = ose.last_exception;
    ose.last_exception = (mp_obj_t)exc;
    mp_opense4_budget_left = OSE_FORMAT_ALLOWANCE;
    mp_opense4_depth = 0;
    ose_obj failure;
    if (ose_protect(format_run, &f, &failure) != 0) {
        const char *note = traceback ? "(the traceback could not be written)\n" : "(the message could not be written)";
        out(ctx, note, strlen(note));
    }
    mp_opense4_budget_left = saved_budget;
    mp_opense4_depth = saved_depth;
    ose.last_exception = saved_exception;
}

void ose_exception_message(ose_obj exc, void (*out)(void *ctx, const char *text, size_t len), void *ctx) {
    format_exception(exc, false, out, ctx);
}

void ose_exception_traceback(ose_obj exc, void (*out)(void *ctx, const char *text, size_t len), void *ctx) {
    format_exception(exc, true, out, ctx);
}

ose_obj ose_new_exception(const char *type, const char *message, size_t len) {
    static const struct {
        const char *name;
        const mp_obj_type_t *type;
    } types[] = {
        {"ArithmeticError", &mp_type_ArithmeticError},
        {"AssertionError", &mp_type_AssertionError},
        {"AttributeError", &mp_type_AttributeError},
        {"Exception", &mp_type_Exception},
        {"IndexError", &mp_type_IndexError},
        {"KeyError", &mp_type_KeyError},
        {"LookupError", &mp_type_LookupError},
        {"NameError", &mp_type_NameError},
        {"NotImplementedError", &mp_type_NotImplementedError},
        {"OverflowError", &mp_type_OverflowError},
        {"RuntimeError", &mp_type_RuntimeError},
        {"TypeError", &mp_type_TypeError},
        {"ValueError", &mp_type_ValueError},
        {"ZeroDivisionError", &mp_type_ZeroDivisionError},
    };
    const mp_obj_type_t *t = &mp_type_RuntimeError;
    for (size_t i = 0; i < MP_ARRAY_SIZE(types); i++) {
        if (strcmp(types[i].name, type) == 0) {
            t = types[i].type;
            break;
        }
    }
    return mp_obj_new_exception_arg1(t, mp_obj_new_str(message, len));
}

// --- port.h: values -----------------------------------------------------------------------

// An instance of a class derived from dict, list or tuple (a Counter, a namedtuple
// subclass...) converts as its built-in part.
static mp_obj_t native_part(mp_obj_t o) {
    if (!mp_obj_is_obj(o) || mp_obj_is_tuple_compatible(o)) {
        return o;
    }
    const mp_obj_type_t *type = mp_obj_get_type(o);
    if (!(type->flags & MP_TYPE_FLAG_INSTANCE_TYPE)) {
        return o;
    }
    mp_obj_t t = MP_OBJ_FROM_PTR(type);
    if (mp_obj_is_subclass_fast(t, MP_OBJ_FROM_PTR(&mp_type_dict))
        || mp_obj_is_subclass_fast(t, MP_OBJ_FROM_PTR(&mp_type_list))
        || mp_obj_is_subclass_fast(t, MP_OBJ_FROM_PTR(&mp_type_tuple))) {
        mp_obj_t sub = ((const mp_obj_instance_t *)MP_OBJ_TO_PTR(o))->subobj[0];
        if (mp_obj_is_type(sub, &mp_type_dict) || mp_obj_is_type(sub, &mp_type_ordereddict)
            || mp_obj_is_type(sub, &mp_type_list) || mp_obj_is_tuple_compatible(sub)) {
            return sub;
        }
    }
    return o;
}

int ose_kind(ose_obj obj) {
    mp_obj_t o = native_part((mp_obj_t)obj);
    if (o == mp_const_none) {
        return OSE_NONE;
    }
    if (mp_obj_is_bool(o)) {
        return OSE_BOOL;
    }
    if (mp_obj_is_int(o)) {
        return OSE_INT;
    }
    if (mp_obj_is_float(o)) {
        return OSE_FLOAT;
    }
    if (mp_obj_is_str(o)) {
        return OSE_STR;
    }
    if (mp_obj_is_type(o, &mp_type_list) || mp_obj_is_tuple_compatible(o)) {
        return OSE_LIST;
    }
    if (mp_obj_is_type(o, &mp_type_ordereddict)) {
        return OSE_ORDERED_DICT;
    }
    if (mp_obj_is_type(o, &mp_type_dict)) {
        return OSE_DICT;
    }
    return OSE_OTHER;
}

const char *ose_type_name(ose_obj obj) {
    return mp_obj_get_type_str((mp_obj_t)obj);
}

int ose_bool_value(ose_obj obj) {
    return (mp_obj_t)obj == mp_const_true;
}

int ose_int_value(ose_obj obj, int64_t *value) {
    mp_obj_t o = (mp_obj_t)obj;
    if (mp_obj_is_small_int(o)) {
        *value = (int64_t)MP_OBJ_SMALL_INT_VALUE(o);
        return 0;
    }
    const mp_obj_int_t *big = MP_OBJ_TO_PTR(o);
    const mpz_t *z = &big->mpz;
    if ((size_t)z->len * MPZ_DIG_SIZE > 64 + MPZ_DIG_SIZE) {
        return 1;
    }
    uint64_t magnitude = 0;
    for (size_t i = z->len; i > 0; i--) {
        if (magnitude >> (64 - MPZ_DIG_SIZE)) {
            return 1;
        }
        magnitude = (magnitude << MPZ_DIG_SIZE) | z->dig[i - 1];
    }
    if (z->neg) {
        if (magnitude > (UINT64_C(1) << 63)) {
            return 1;
        }
        *value = magnitude == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)magnitude;
    } else {
        if (magnitude > (uint64_t)INT64_MAX) {
            return 1;
        }
        *value = (int64_t)magnitude;
    }
    return 0;
}

const char *ose_str_value(ose_obj obj, size_t *len) {
    GET_STR_DATA_LEN((mp_obj_t)obj, data, n);
    *len = n;
    return (const char *)data;
}

size_t ose_list_items(ose_obj obj, ose_obj **items) {
    mp_obj_t o = native_part((mp_obj_t)obj);
    size_t len;
    mp_obj_t *array;
    if (mp_obj_is_type(o, &mp_type_list)) {
        mp_obj_list_get(o, &len, &array);
    } else {
        mp_obj_tuple_get(o, &len, &array);   // a tuple, or a namedtuple
    }
    *items = (ose_obj *)array;
    return len;
}

size_t ose_dict_size(ose_obj obj) {
    const mp_obj_dict_t *d = MP_OBJ_TO_PTR(native_part((mp_obj_t)obj));
    return d->map.used;
}

int ose_dict_next(ose_obj obj, size_t *pos, ose_obj *key, ose_obj *value) {
    const mp_obj_dict_t *d = MP_OBJ_TO_PTR(native_part((mp_obj_t)obj));
    const mp_map_t *map = &d->map;
    for (; *pos < map->alloc; ++*pos) {
        if (mp_map_slot_is_filled(map, *pos)) {
            *key = map->table[*pos].key;
            *value = map->table[*pos].value;
            ++*pos;
            return 1;
        }
    }
    return 0;
}

ose_obj ose_none(void) {
    return mp_const_none;
}

ose_obj ose_bool(int value) {
    return mp_obj_new_bool(value);
}

ose_obj ose_int(int64_t value) {
    return mp_obj_new_int_from_ll((long long)value);
}

ose_obj ose_str(const char *data, size_t len) {
    return mp_obj_new_str(data, len);
}

ose_obj ose_list(size_t len) {
    mp_obj_t list = mp_obj_new_list(len, NULL);
    mp_obj_list_t *l = MP_OBJ_TO_PTR(list);
    for (size_t i = 0; i < len; i++) {
        l->items[i] = mp_const_none;
    }
    return list;
}

void ose_list_set(ose_obj list, size_t i, ose_obj item) {
    mp_obj_list_t *l = MP_OBJ_TO_PTR((mp_obj_t)list);
    l->items[i] = (mp_obj_t)item;
}

ose_obj ose_dict(void) {
    return mp_obj_new_dict(0);
}

void ose_dict_set(ose_obj dict, ose_obj key, ose_obj value) {
    mp_obj_dict_store((mp_obj_t)dict, (mp_obj_t)key, (mp_obj_t)value);
}

// --- port.h: native modules ----------------------------------------------------------

typedef struct {
    mp_obj_base_t base;
    size_t index;
    qstr name;
} ose_native_fun_t;

static mp_obj_t native_fun_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    ose_native_fun_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_kw != 0) {
        mp_raise_TypeError(MP_ERROR_TEXT("engine functions take positional arguments only"));
    }
    ose_obj result = NULL;
    if (ose.host->call_native(ose.host->ctx, self->index, n_args, (const ose_obj *)args, &result) != 0) {
        nlr_raise((mp_obj_t)result);
    }
    return (mp_obj_t)result;
}

static void native_fun_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    ose_native_fun_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<engine function %q>", self->name);
}

// MicroPython's type definitions initialise a flexible array member (a GNU C
// extension that MicroPython relies on everywhere).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
static MP_DEFINE_CONST_OBJ_TYPE(
    ose_type_native_fun, MP_QSTR_function, MP_TYPE_FLAG_NONE,
    call, native_fun_call,
    print, native_fun_print
    );
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

ose_obj ose_native_module(const char *name) {
    return mp_obj_new_module(qstr_from_str(name));
}

void ose_native_function(ose_obj module, const char *name, size_t index) {
    ose_native_fun_t *f = mp_obj_malloc(ose_native_fun_t, &ose_type_native_fun);
    f->index = index;
    f->name = qstr_from_str(name);
    mp_store_attr((mp_obj_t)module, f->name, MP_OBJ_FROM_PTR(f));
}
