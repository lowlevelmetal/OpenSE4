// The C side of the script runtime: MicroPython's port for OpenSE4 (port.c), and
// the small C interface through which runtime.cpp drives it. C++ never includes
// MicroPython's headers; it sees objects only as `ose_obj` handles.
//
// Calls that can raise a Python exception (allocation, import, calling Python)
// must run inside ose_protect(). A raise unwinds with longjmp to the innermost
// ose_protect, skipping the frames in between: the functions passed to it must
// not hold C++ objects with destructors across such calls.

#ifndef OPENSE4_SCRIPT_PORT_H
#define OPENSE4_SCRIPT_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *ose_obj;

// What the engine side provides to the interpreter.
typedef struct ose_host {
    void *ctx;
    // print() output.
    void (*output)(void *ctx, const char *text, size_t len);
    // Module files. `path` is relative, '/'-separated, e.g. "ai/strategy.py".
    // source: the file's text, or NULL when there is no such file.
    const char *(*source)(void *ctx, const char *path, size_t *len);
    // is_package: whether `path` (e.g. "ai") is a folder that holds module files.
    int (*is_package)(void *ctx, const char *path);
    // compiled: the bytecode cached for the file's current text, or NULL.
    const uint8_t *(*compiled)(void *ctx, const char *path, size_t *len);
    // store_compiled: keeps freshly compiled bytecode; returns the kept copy, which
    // stays valid for the interpreter's life.
    const uint8_t *(*store_compiled)(void *ctx, const char *path, const uint8_t *data, size_t len, size_t *kept_len);
    // call_native: runs native function `index`. Returns 0 with the result in
    // *result, or 1 with the exception to raise in *result.
    int (*call_native)(void *ctx, size_t index, size_t n_args, const ose_obj *args, ose_obj *result);
} ose_host;

// --- Lifecycle (one interpreter at a time per process) -------------------------
// Starts an interpreter on `heap`. Returns 0, or 1 if one is already running.
int ose_init(const ose_host *host, void *heap, size_t heap_size, size_t depth_limit);
void ose_deinit(void);
// Where the C stack of the current call starts, and how much of it scripts may use.
// Set at every entry: the interpreter may be used from another thread next time.
void ose_enter(void *stack_top, size_t stack_limit);
size_t ose_heap_used(void);

// --- Running code ---------------------------------------------------------------------
// Runs fn(ctx); returns 0, or 1 with the Python exception in *exception.
int ose_protect(void (*fn)(void *ctx), void *ctx, ose_obj *exception);
// Before each call of the engine's into scripts: resets what one call counts.
void ose_begin_call(void);

// Inside ose_protect: imports a module ("ai.strategy") and returns it.
ose_obj ose_import(const char *module);
// Attribute, or NULL when it doesn't exist (never raises AttributeError).
ose_obj ose_getattr_maybe(ose_obj obj, const char *name);
ose_obj ose_call(ose_obj fn, size_t n_args, const ose_obj *args);
// Runs source text in __main__: mode 0 executes statements, 1 evaluates an expression.
ose_obj ose_exec(const char *source, size_t len, const char *name, int mode);
// Sets up the runtime's own names: the BudgetExceeded and RecursionError classes.
void ose_setup(void);
// Keeps one object reachable for the garbage collector outside protected code (NULL
// lets it go), and returns it.
void ose_keep(ose_obj obj);
ose_obj ose_kept(void);

// --- Budget, in bytecodes (plus native work in the same units) --------------------------
void ose_budget_set(int64_t left);
int64_t ose_budget_get(void);
// While compiling or loading cached code, the budget is set aside (see port.c).
#define OSE_BUDGET_UNLIMITED (INT64_C(1) << 62)

// --- Exceptions ---------------------------------------------------------------------
enum ose_exception_kind {
    OSE_EXC_OTHER = 0,
    OSE_EXC_SYNTAX,
    OSE_EXC_BUDGET,
    OSE_EXC_MEMORY,
    OSE_EXC_RECURSION,
};
int ose_exception_kind(ose_obj exc);
// The exception's type name, e.g. "ValueError" (a static or interned string).
const char *ose_exception_type(ose_obj exc);
// Writes str(exc), or the traceback with the exception line, through `out`.
void ose_exception_message(ose_obj exc, void (*out)(void *ctx, const char *text, size_t len), void *ctx);
void ose_exception_traceback(ose_obj exc, void (*out)(void *ctx, const char *text, size_t len), void *ctx);
// Inside ose_protect: a new exception of a built-in type named `type` (ValueError,
// TypeError, KeyError, IndexError, RuntimeError, ...; anything else gives RuntimeError).
ose_obj ose_new_exception(const char *type, const char *message, size_t len);

// --- Values ---------------------------------------------------------------------------
enum ose_kind {
    OSE_NONE = 0,
    OSE_BOOL,
    OSE_INT,
    OSE_FLOAT,
    OSE_STR,
    OSE_LIST,         // list, or a tuple (namedtuples included)
    OSE_DICT,         // dict: keys in its own order
    OSE_ORDERED_DICT, // OrderedDict: keys in insertion order
    OSE_OTHER,
};
int ose_kind(ose_obj obj);
const char *ose_type_name(ose_obj obj);
int ose_bool_value(ose_obj obj);
// 0 and the value, or 1 when the integer doesn't fit in 64 bits.
int ose_int_value(ose_obj obj, int64_t *value);
const char *ose_str_value(ose_obj obj, size_t *len);
size_t ose_list_items(ose_obj obj, ose_obj **items);
size_t ose_dict_size(ose_obj obj);
// Walks a dict's entries in its order: start with *pos = 0; returns 0 at the end.
int ose_dict_next(ose_obj obj, size_t *pos, ose_obj *key, ose_obj *value);

// Inside ose_protect (they allocate):
ose_obj ose_none(void);
ose_obj ose_bool(int value);
ose_obj ose_int(int64_t value);
ose_obj ose_str(const char *data, size_t len); // valid UTF-8
ose_obj ose_list(size_t len);                  // items set with ose_list_set
void ose_list_set(ose_obj list, size_t i, ose_obj item);
ose_obj ose_dict(void);
void ose_dict_set(ose_obj dict, ose_obj key, ose_obj value);

// --- Native modules ----------------------------------------------------------------
// Inside ose_protect: a module available to `import name`, and a function in it that
// calls host->call_native(index).
ose_obj ose_native_module(const char *name);
void ose_native_function(ose_obj module, const char *name, size_t index);

#ifdef __cplusplus
}
#endif

#endif // OPENSE4_SCRIPT_PORT_H
