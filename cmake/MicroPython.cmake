# MicroPython, the interpreter of the script runtime (docs/sdk/runtime.md), built
# as a static C library from third_party/micropython: the pinned release with our
# patches and the headers generated for src/script/port/mpconfigport.h by
# tools/update_micropython.sh (docs/BUILDING.md, "MicroPython"). Nothing is
# fetched or generated at build time, so every compiler builds the same sources.
#
# Defines opense4_micropython: MicroPython's core, its json, re and heapq modules,
# the regular expression engine and the bundled math functions. Our port
# (src/script/port/*.c, compiled into opense4_script with our warning flags)
# supplies the hooks it calls. Third-party code: our warning flags stay off it.

set(OPENSE4_MICROPYTHON_DIR "${CMAKE_SOURCE_DIR}/third_party/micropython")
set(OPENSE4_SCRIPT_PORT_DIR "${CMAKE_SOURCE_DIR}/src/script/port")

# The generated headers must match the configuration they were generated from.
file(SHA256 "${OPENSE4_SCRIPT_PORT_DIR}/mpconfigport.h" _opense4_mpconfig_sha)
file(STRINGS "${OPENSE4_MICROPYTHON_DIR}/UPSTREAM.txt" _opense4_mp_recorded REGEX "^mpconfigport.h sha256: ")
string(REGEX REPLACE "^mpconfigport.h sha256: " "" _opense4_mp_recorded "${_opense4_mp_recorded}")
if(NOT _opense4_mp_recorded STREQUAL _opense4_mpconfig_sha)
    message(FATAL_ERROR "src/script/port/mpconfigport.h changed since third_party/micropython was generated "
                        "(${_opense4_mp_recorded} there, ${_opense4_mpconfig_sha} now): run tools/update_micropython.sh")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${OPENSE4_SCRIPT_PORT_DIR}/mpconfigport.h" "${OPENSE4_MICROPYTHON_DIR}/UPSTREAM.txt")

file(GLOB _opense4_mp_core CONFIGURE_DEPENDS "${OPENSE4_MICROPYTHON_DIR}/py/*.c")
set(_opense4_mp_sources
    ${_opense4_mp_core}
    "${OPENSE4_MICROPYTHON_DIR}/extmod/modheapq.c"
    "${OPENSE4_MICROPYTHON_DIR}/extmod/modjson.c"
    "${OPENSE4_MICROPYTHON_DIR}/extmod/modre.c"
    "${OPENSE4_MICROPYTHON_DIR}/shared/runtime/gchelper_generic.c")

# The transcendental functions from musl (lib/libm_dbl), compiled against a stand-in
# <math.h> that gives them names of their own (mp_musl_*), so they never replace the
# C library's. The exact functions they use (floor, fabs, sqrt...) come from the
# platform: IEEE 754 defines their results.
set(_opense4_mp_libm "")
foreach(f IN ITEMS __cos __expo2 __fpclassify __rem_pio2 __rem_pio2_large __signbit __sin __tan
                   acos acosh asin asinh atan atan2 atanh cos cosh erf exp expm1 lgamma log log10
                   log1p pow scalbn sin sinh tan tanh tgamma)
    list(APPEND _opense4_mp_libm "${OPENSE4_MICROPYTHON_DIR}/lib/libm_dbl/${f}.c")
endforeach()

add_library(opense4_micropython_libm OBJECT ${_opense4_mp_libm})
target_include_directories(opense4_micropython_libm BEFORE PRIVATE "${OPENSE4_SCRIPT_PORT_DIR}/libm")
set_target_properties(opense4_micropython_libm PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_compile_options(opense4_micropython_libm PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)

add_library(opense4_micropython STATIC ${_opense4_mp_sources} $<TARGET_OBJECTS:opense4_micropython_libm>)
target_include_directories(opense4_micropython SYSTEM PUBLIC "${OPENSE4_MICROPYTHON_DIR}" "${OPENSE4_SCRIPT_PORT_DIR}")
if(MSVC)
    # unistd.h, for ssize_t
    target_include_directories(opense4_micropython SYSTEM PRIVATE "${OPENSE4_SCRIPT_PORT_DIR}/msvc")
endif()
set_target_properties(opense4_micropython PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_compile_options(opense4_micropython PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
if(NOT MSVC)
    # The interpreter loop is the hot path; keep it optimized in debug builds too,
    # as the budget tests run millions of bytecodes.
    target_compile_options(opense4_micropython PRIVATE $<$<CONFIG:Debug>:-O1>)
endif()

# The sanitizers (the asan preset): MicroPython's garbage collector scans the C stack
# for pointers, so locals must live on the real stack, never in the sanitizer's
# stack of returned frames; and it is third-party code, so no undefined-behaviour
# checks in it. The C stack scan itself is exempt from address checks in gc.c.
if(CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*address")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(OPENSE4_ASAN_STACK_OPTION "--param=asan-use-after-return=0")
    else()
        set(OPENSE4_ASAN_STACK_OPTION "-fsanitize-address-use-after-return=never")
    endif()
    target_compile_options(opense4_micropython PRIVATE ${OPENSE4_ASAN_STACK_OPTION} -fno-sanitize=undefined)
    target_compile_options(opense4_micropython_libm PRIVATE -fno-sanitize=undefined)
endif()
