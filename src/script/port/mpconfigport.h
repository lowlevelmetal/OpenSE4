// MicroPython configuration for OpenSE4's script runtime (docs/sdk/runtime.md).
//
// Every interpreter built from this configuration is a sandbox: only the modules
// enabled here exist, and none of them reaches files, the network, the clock,
// threads or other programs. The configuration must be the same on every platform,
// because the qstr tables in third_party/micropython/genhdr are generated from it
// once (tools/update_micropython.sh); CMake refuses to build when this file no
// longer matches them. Settings that only change speed may differ per compiler.
//
// The hooks at the end (budget, call depth, hashing, identity, sorting) are
// implemented in port.c; third_party/micropython/patches/ adds the places in the
// interpreter that call them.

#ifndef OPENSE4_MPCONFIGPORT_H
#define OPENSE4_MPCONFIGPORT_H

#include <stdint.h>
#include <stddef.h>

// --- Base level ---------------------------------------------------------------
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// --- Objects, numbers and text ----------------------------------------------------
#define MICROPY_OBJ_REPR (MICROPY_OBJ_REPR_A)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
// 16-bit digits on every platform (the default would be 32 on 64-bit machines), so
// big-number arithmetic takes the same steps, and costs the same budget, everywhere.
#define MPZ_DIG_SIZE (16)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_FLOAT_HIGH_QUALITY_HASH (1)
// Float formatting and parsing in double arithmetic only, on every platform. (The
// "exact" variant uses long double, which is 64, 80 or 128 bits depending on the
// platform, so its output would differ between them.)
#define MICROPY_FLOAT_FORMAT_IMPL (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
// Ints converted to machine words (sizes, indices, range() bounds) must fit in 32
// bits on every build, as on 32-bit ARM (our patch to py/obj.c).
#define MICROPY_MACHINE_INT_32 (1)
// Transcendental functions come from the bundled musl-derived library (lib/libm_dbl),
// not the platform's: glibc, msvcrt and Apple's libm round some results differently.
#define MICROPY_FLOAT_C_FUN(fun) mp_libm_##fun
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_QSTR_BYTES_IN_HASH (2)
#define MICROPY_PY_TSTRINGS (0)
// Dicts keep insertion order, as in CPython (our patch to py/map.c); their order
// never depends on hashing.
#define MICROPY_MAP_INSERTION_ORDER (1)
// Class bodies record their annotated names in __annotations__ (our patch to
// py/compile.c), for dataclasses.
#define MICROPY_COMP_CLASS_ANNOTATIONS (1)
// dict(x) and d.update(x) take a defaultdict or Counter as the dict it is (our patch).
#define MICROPY_PY_DICT_UPDATE_FROM_SUBCLASS (1)

// --- Compiler and bytecode ----------------------------------------------------------
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_PERSISTENT_CODE_LOAD (1)   // the per-process bytecode cache (.mpy in memory)
#define MICROPY_PERSISTENT_CODE_SAVE (1)
#define MICROPY_PERSISTENT_CODE_SAVE_FILE (0)
#define MICROPY_PERSISTENT_CODE_SAVE_FUN (0)
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_ENABLE_DOC_STRING (0)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_WARNINGS (0)
#define MICROPY_ROM_TEXT_COMPRESSION (0)
#define MICROPY_HELPER_REPL (0)
#define MICROPY_REPL_EMACS_KEYS (0)
#define MICROPY_REPL_AUTO_INDENT (0)

// --- Memory -------------------------------------------------------------------
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_FINALISER (0)        // no __del__: when garbage is collected must not matter
#define MICROPY_GC_CONSERVATIVE_CLEAR (1)
#define MICROPY_ENABLE_PYSTACK (0)
#define MICROPY_STACKLESS (0)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_STACK_CHECK_MARGIN (0)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE (256)
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE (0)
#define MICROPY_TRACKED_ALLOC (0)

// --- Exceptions and control flow -----------------------------------------------------
// setjmp everywhere: the same non-local return on every compiler (MSVC has no inline
// assembly), and one that the address sanitizer understands.
#define MICROPY_NLR_SETJMP (1)
// The garbage collector finds pointers held in registers through setjmp on Windows
// (its 64-bit calling convention keeps more registers than the assembly helper saves),
// through MicroPython's assembly helper elsewhere.
#if defined(_WIN32)
#define MICROPY_GCREGS_SETJMP (1)
#else
#define MICROPY_GCREGS_SETJMP (0)
#endif
#define MICROPY_KBD_EXCEPTION (0)
#define MICROPY_ASYNC_KBD_INTR (0)
#define MICROPY_ENABLE_SCHEDULER (0)
#define MICROPY_ENABLE_VM_ABORT (0)
#define MICROPY_PY_THREAD (0)

// --- Imports ---------------------------------------------------------------------
// Modules come only from the sources the engine gives each interpreter (port.c serves
// them through mp_import_stat and mp_reader_new_file). Nothing reads the file system.
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_READER_VFS (1)              // "a file reader exists": port.c provides it
#define MICROPY_READER_POSIX (0)
#define MICROPY_VFS (0)
#define MICROPY_MODULE_FROZEN_STR (0)
#define MICROPY_MODULE_FROZEN_MPY (0)
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (0)
#define MICROPY_MODULE___FILE__ (1)
#define MICROPY_MODULE_BUILTIN_SUBPACKAGES (0)
#define MICROPY_CAN_OVERRIDE_BUILTINS (1)  // per interpreter only; lets the runtime add names
#define MICROPY_ALLOC_PATH_MAX (256)

// --- Language features -----------------------------------------------------------------
#define MICROPY_CPYTHON_COMPAT (1)
#define MICROPY_FULL_CHECKS (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_FSTRINGS (1)
#define MICROPY_PY_ASSIGN_EXPR (1)
#define MICROPY_PY_DESCRIPTORS (1)
#define MICROPY_PY_DELATTR_SETATTR (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS (1)
#define MICROPY_PY_ALL_INPLACE_SPECIAL_METHODS (1)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS (1)
#define MICROPY_PY_FUNCTION_ATTRS (1)
#define MICROPY_PY_FUNCTION_ATTRS_CODE (0)  // no __code__: bytecode stays out of reach
#define MICROPY_PY_BUILTINS_CODE (MICROPY_PY_BUILTINS_CODE_MINIMUM)
#define MICROPY_PY_BUILTINS_COMPILE (1)
#define MICROPY_PY_BUILTINS_EVAL_EXEC (1)
#define MICROPY_PY_BUILTINS_EXECFILE (0)
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_PY_BUILTINS_HELP (0)
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_BUILTINS_NOTIMPLEMENTED (1)
#define MICROPY_PY_BUILTINS_POW3 (1)
#define MICROPY_PY_BUILTINS_ROUND_INT (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS (1)
#define MICROPY_PY_BUILTINS_SLICE_INDICES (1)
#define MICROPY_PY_BUILTINS_STR_CENTER (1)
#define MICROPY_PY_BUILTINS_STR_PARTITION (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_BUILTINS_BYTES_HEX (1)
#define MICROPY_PY_BUILTINS_RANGE_BINOP (1)
#define MICROPY_PY_BUILTINS_NEXT2 (1)
#define MICROPY_PY_ATTRTUPLE (1)
#define MICROPY_PY_GENERATOR_PEND_THROW (0)
#define MICROPY_PY_STR_BYTES_CMP_WARN (0)
#define MICROPY_MODULE___ALL__ (1)
#define MICROPY_MODULE_GETATTR (1)
#define MICROPY_MULTIPLE_INHERITANCE (1)

// --- Modules -------------------------------------------------------------------------
// Present: builtins, sys (reduced), collections, heapq, math, json, re, struct, array, io.
#define MICROPY_PY_COLLECTIONS (1)
#define MICROPY_PY_COLLECTIONS_DEQUE (1)
#define MICROPY_PY_COLLECTIONS_DEQUE_ITER (1)
#define MICROPY_PY_COLLECTIONS_DEQUE_SUBSCR (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (1)
#define MICROPY_PY_COLLECTIONS_NAMEDTUPLE__ASDICT (1)
#define MICROPY_PY_HEAPQ (1)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_MATH_CONSTANTS (1)
#define MICROPY_PY_MATH_SPECIAL_FUNCTIONS (1)
#define MICROPY_PY_MATH_FACTORIAL (1)
#define MICROPY_PY_MATH_ISCLOSE (1)
#define MICROPY_PY_MATH_EXTENSIBLE (1)            // python/lib/math.py adds gcd, isqrt, hypot...
#define MICROPY_PY_MATH_ATAN2_FIX_INFNAN (0)  // musl's atan2 is right already
#define MICROPY_PY_MATH_FMOD_FIX_INFNAN (0)   // done in port.c (mp_libm_fmod)
#define MICROPY_PY_MATH_MODF_FIX_NEGZERO (1)
#define MICROPY_PY_MATH_POW_FIX_NAN (1)
#define MICROPY_PY_MATH_GAMMA_FIX_NEGINF (1)
#define MICROPY_OPT_MATH_FACTORIAL (1)
#define MICROPY_PY_CMATH (0)
#define MICROPY_PY_JSON (1)
#define MICROPY_PY_JSON_SEPARATORS (1)
#define MICROPY_PY_RE (1)
#define MICROPY_PY_RE_MATCH_GROUPS (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END (1)
#define MICROPY_PY_RE_SUB (1)
#define MICROPY_PY_RE_DEBUG (0)
#define MICROPY_PY_STRUCT (1)
#define MICROPY_PY_STRUCT_UNSAFE_TYPECODES (0)  // no 'P', 'O' or 'S': no raw pointers
#define MICROPY_PY_ARRAY (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN (1)
#define MICROPY_NATIVE_LONG_SIZE (8)            // struct/array 'l' and 'L': 8 bytes everywhere

// io: in-memory streams only (StringIO, BytesIO; json.loads reads through one). No open().
#define MICROPY_PY_IO (1)
#define MICROPY_PY_IO_IOBASE (0)
#define MICROPY_PY_IO_BYTESIO (1)
#define MICROPY_PY_IO_BUFFEREDWRITER (0)
#define MICROPY_PY_BUILTINS_OPEN (0)

// sys: version information, sys.modules, sys.exit and sys.exc_info. No path, argv,
// standard streams, sizes of objects or recursion settings.
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_MODULES (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_SYS_EXC_INFO (1)
#define MICROPY_PY_SYS_MAXSIZE (1)
#define MP_SYS_MAXSIZE (INT64_C(9223372036854775807))  // the same on 32-bit builds
#define MICROPY_PY_SYS_PATH (0)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (0)
#define MICROPY_PY_SYS_ARGV (0)
#define MICROPY_PY_SYS_STDFILES (0)
#define MICROPY_PY_SYS_STDIO_BUFFER (0)
#define MICROPY_PY_SYS_GETSIZEOF (0)
#define MICROPY_PY_SYS_INTERN (0)
#define MICROPY_PY_SYS_SETTRACE (0)
#define MICROPY_PY_SYS_PS1_PS2 (0)
#define MICROPY_PY_SYS_TRACEBACKLIMIT (0)
#define MICROPY_PY_SYS_ATEXIT (0)
#define MICROPY_PY_SYS_EXECUTABLE (0)
#define MICROPY_PY_SYS_ATTR_DELEGATION (0)
#define MICROPY_PY_SYS_PLATFORM "opense4"
#define MICROPY_BANNER_MACHINE "OpenSE4 script runtime"
#define MP_ENDIANNESS_LITTLE (1)

// Absent: everything that reaches outside the interpreter, or whose answers depend on
// the computer or on when garbage is collected.
#define MICROPY_PY_OS (0)
#define MICROPY_PY_TIME (0)
#define MICROPY_PY_SELECT (0)
#define MICROPY_PY_SOCKET (0)
#define MICROPY_PY_NETWORK (0)
#define MICROPY_PY_RANDOM (0)
#define MICROPY_PY_MACHINE (0)
#define MICROPY_PY_GC (0)
#define MICROPY_PY_MICROPYTHON (0)
#define MICROPY_PY_ERRNO (0)
#define MICROPY_PY_UCTYPES (0)
#define MICROPY_PY_WEAKREF (0)
#define MICROPY_PY_MARSHAL (0)
#define MICROPY_PY_ASYNCIO (0)
#define MICROPY_PY_PLATFORM (0)
#define MICROPY_PY_HASHLIB (0)
#define MICROPY_PY_CRYPTOLIB (0)
#define MICROPY_PY_BINASCII (0)
#define MICROPY_PY_DEFLATE (0)
#define MICROPY_PY_FRAMEBUF (0)
#define MICROPY_PY_BTREE (0)
#define MICROPY_PY_SSL (0)
#define MICROPY_PY_WEBSOCKET (0)
#define MICROPY_PY_ONEWIRE (0)
#define MICROPY_PY_LWIP (0)
#define MICROPY_PY_VFS (0)
#define MICROPY_PY_BLUETOOTH (0)

// --- Speed (may differ per compiler; never changes results) -----------------------------
#if defined(__GNUC__) || defined(__clang__)
#define MICROPY_OPT_COMPUTED_GOTO (1)
#else
#define MICROPY_OPT_COMPUTED_GOTO (0)
#endif
#define MICROPY_OPT_LOAD_ATTR_FAST_PATH (1)
#define MICROPY_OPT_MAP_LOOKUP_CACHE (0)  // keyed by address: would change what collisions cost
#define MICROPY_OPT_MPZ_BITWISE (1)

// --- C library ----------------------------------------------------------------------
// Never MicroPython's own printf: it would replace the C library's for the whole program.
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_USE_INTERNAL_ERRNO (0)
#define MICROPY_MPHALPORT_H "mphalport.h"
#define mp_hal_pin_obj_t

typedef long mp_off_t;

#if defined(_WIN32)
#include <malloc.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <stdlib.h>
#else
#include <alloca.h>
#endif

#define MP_SSIZE_MAX (INTPTR_MAX)

#if defined(_MSC_VER)
// Visual C++ (CI only): no GCC attributes.
#define MP_NORETURN __declspec(noreturn)
#define MP_WEAK
#define MP_NOINLINE __declspec(noinline)
#define MP_ALWAYSINLINE __forceinline
#define MP_LIKELY(x) (x)
#define MP_UNLIKELY(x) (x)
#define MP_UNREACHABLE __assume(0);
#endif

// --- OpenSE4 hooks (port.c; the patches call them) ----------------------------------
// The budget, counted in bytecodes executed plus native work in the same units.
extern int64_t mp_opense4_budget_left;
void mp_opense4_budget_exhausted(void);
#define MICROPY_BUDGET_CHARGE(units) do { \
        if ((mp_opense4_budget_left -= (int64_t)(units)) < 0) { \
            mp_opense4_budget_exhausted(); \
        } \
} while (0)
#define MICROPY_BUDGET_DEBIT(units) ((void)(mp_opense4_budget_left -= (int64_t)(units)))
// Inside the bytecode loop (py/vm.c): one unit per bytecode; the exception is raised
// as if the bytecode about to run had raised it.
void *mp_opense4_budget_exception(void);
#define MICROPY_VM_HOOK_OPCODE \
    if (--mp_opense4_budget_left < 0) { \
        RAISE(mp_opense4_budget_exception()); \
    }

// Python call depth, counted the same way on every computer (py/objfun.c,
// py/objgenerator.c); the C stack check stays as a last line of defence.
extern size_t mp_opense4_depth;
extern size_t mp_opense4_depth_limit;
void mp_opense4_depth_exceeded(void);
#define MICROPY_CALL_DEPTH_CHECK() do { \
        if (mp_opense4_depth >= mp_opense4_depth_limit) { \
            mp_opense4_depth_exceeded(); \
        } \
} while (0)
#define MICROPY_CALL_DEPTH_ENTER() (++mp_opense4_depth)
#define MICROPY_CALL_DEPTH_EXIT() (--mp_opense4_depth)

// Hashes: whole numbers in [0, 2^30), computed the same way on 32-bit and 64-bit
// builds. Integers hash as their value modulo a prime below 2^30. Objects without a
// value hash use a number given in the order the interpreter first needs one (or,
// for class instances, when they are created), never their address.
#define MICROPY_HASH_MASK (0x3fffffff)
#define MICROPY_HASH_INT_MODULUS (1073741789)
uintptr_t mp_opense4_identity(const void *obj);
uintptr_t mp_opense4_new_identity(void);
#define MICROPY_OBJ_IDENTITY(o) mp_opense4_identity((const void *)(o))
#define MICROPY_OBJ_ID_VALUE(o) (mp_opense4_identity((const void *)(o)) << 2)
#define MICROPY_OBJ_NEW_IDENTITY() mp_opense4_new_identity()
#define MICROPY_PRINT_POINTER_VALUE(p) mp_opense4_identity((const void *)(p))

// list.sort and sorted(): a stable merge sort that calls the key once per item, as
// CPython does (py/objlist.c).
struct _mp_obj_list_t;
void mp_opense4_list_sort(struct _mp_obj_list_t *list, void *key_fn, int reverse);
#define MICROPY_LIST_SORT(list, key_fn, reverse) mp_opense4_list_sort((list), (key_fn), (reverse))

// Imports: modules run from bytecode that the runtime caches per process.
struct _mp_compiled_module_t;
int mp_opense4_import_load(const char *path, struct _mp_compiled_module_t *cm);
#define MICROPY_PORT_IMPORT_LOAD(path, cm) mp_opense4_import_load((path), (cm))

// The math functions MicroPython calls through MICROPY_FLOAT_C_FUN.
double mp_libm_sin(double x);
double mp_libm_cos(double x);
double mp_libm_tan(double x);
double mp_libm_asin(double x);
double mp_libm_acos(double x);
double mp_libm_atan(double x);
double mp_libm_atan2(double y, double x);
double mp_libm_sinh(double x);
double mp_libm_cosh(double x);
double mp_libm_tanh(double x);
double mp_libm_asinh(double x);
double mp_libm_acosh(double x);
double mp_libm_atanh(double x);
double mp_libm_exp(double x);
double mp_libm_expm1(double x);
double mp_libm_log(double x);
double mp_libm_log1p(double x);
double mp_libm_log2(double x);
double mp_libm_log10(double x);
double mp_libm_pow(double x, double y);
double mp_libm_erf(double x);
double mp_libm_erfc(double x);
double mp_libm_lgamma(double x);
double mp_libm_tgamma(double x);
double mp_libm_sqrt(double x);
double mp_libm_fabs(double x);
double mp_libm_floor(double x);
double mp_libm_ceil(double x);
double mp_libm_trunc(double x);
double mp_libm_nearbyint(double x);
double mp_libm_fmod(double x, double y);
double mp_libm_copysign(double x, double y);
double mp_libm_frexp(double x, int *exp);
double mp_libm_ldexp(double x, int exp);
double mp_libm_modf(double x, double *iptr);
double mp_libm_nan(const char *tag);
int mp_libm_isfinite(double x);
int mp_libm_isinf(double x);
int mp_libm_isnan(double x);

#endif // OPENSE4_MPCONFIGPORT_H
