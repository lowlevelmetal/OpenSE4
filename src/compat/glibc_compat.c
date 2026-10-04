/* Lets a Linux release built on a new distribution run on glibc 2.34 and later.
 *
 * Only for OPENSE4_STATIC builds on Linux (cmake/GlibcCompat.cmake). The linker's
 * --wrap option sends each call below to the __wrap_ function here:
 *   - maths and C23 conversion functions go to the old versions of the same
 *     functions, which every glibc still exports;
 *   - a few functions that are newer than 2.34 get small implementations.
 * _dl_find_object (the C++ unwinder's lookup) is in glibc_compat_dlfo.c.
 *
 * Built as strict C11 without _GNU_SOURCE, so the headers do not themselves
 * rename strtol and friends to their C23 versions. */

#define _POSIX_C_SOURCE 200809L  /* strnlen, wcsnlen */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <wchar.h>

#define BIND(ret, name, version, params) \
    __asm__(".symver " #name "_old, " #name "@" version); \
    ret name##_old params;

/* The old versions: each architecture's first glibc version (BASE), and the
 * one that added the C99 scanf functions (ISOC99). */
#if defined(__x86_64__)
#define BASE "GLIBC_2.2.5"
#define ISOC99 "GLIBC_2.7"
#elif defined(__aarch64__)
#define BASE "GLIBC_2.17"
#define ISOC99 "GLIBC_2.17"
#elif defined(__arm__) && defined(__ARM_PCS_VFP)
#define BASE "GLIBC_2.4"
#define ISOC99 "GLIBC_2.7"
#else
#error "glibc_compat.c: add this architecture's old glibc symbol versions"
#endif

/* Maths: glibc 2.35 to 2.43 added new versions; the old ones remain. */
BIND(float, acosf, BASE, (float))
BIND(float, asinf, BASE, (float))
BIND(float, atan2f, BASE, (float, float))
BIND(float, log10f, BASE, (float))
BIND(float, sqrtf, BASE, (float))
BIND(float, hypotf, BASE, (float, float))
BIND(double, fmod, BASE, (double, double))
BIND(float, fmodf, BASE, (float, float))

float __wrap_acosf(float x) { return acosf_old(x); }
float __wrap_asinf(float x) { return asinf_old(x); }
float __wrap_atan2f(float y, float x) { return atan2f_old(y, x); }
float __wrap_log10f(float x) { return log10f_old(x); }
float __wrap_sqrtf(float x) { return sqrtf_old(x); }
float __wrap_hypotf(float x, float y) { return hypotf_old(x, y); }
double __wrap_fmod(double x, double y) { return fmod_old(x, y); }
float __wrap_fmodf(float x, float y) { return fmodf_old(x, y); }

/* C23 conversions (glibc 2.38): the pre-C23 functions differ only in not
 * accepting the 0b prefix, which nothing here relies on. */
BIND(long, strtol, BASE, (const char*, char**, int))
BIND(long long, strtoll, BASE, (const char*, char**, int))
BIND(unsigned long, strtoul, BASE, (const char*, char**, int))
BIND(unsigned long long, strtoull, BASE, (const char*, char**, int))
BIND(long, wcstol, BASE, (const wchar_t*, wchar_t**, int))
__asm__(".symver vsscanf_old, __isoc99_vsscanf@" ISOC99);
int vsscanf_old(const char*, const char*, va_list);
__asm__(".symver vfscanf_old, __isoc99_vfscanf@" ISOC99);
int vfscanf_old(FILE*, const char*, va_list);

long __wrap___isoc23_strtol(const char* s, char** end, int base) { return strtol_old(s, end, base); }
long long __wrap___isoc23_strtoll(const char* s, char** end, int base) { return strtoll_old(s, end, base); }
unsigned long __wrap___isoc23_strtoul(const char* s, char** end, int base) { return strtoul_old(s, end, base); }
unsigned long long __wrap___isoc23_strtoull(const char* s, char** end, int base) { return strtoull_old(s, end, base); }
long __wrap___isoc23_wcstol(const wchar_t* s, wchar_t** end, int base) { return wcstol_old(s, end, base); }
int __wrap___isoc23_vsscanf(const char* s, const char* format, va_list args) { return vsscanf_old(s, format, args); }
int __wrap___isoc23_sscanf(const char* s, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = vsscanf_old(s, format, args);
    va_end(args);
    return n;
}
int __wrap___isoc23_fscanf(FILE* f, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = vfscanf_old(f, format, args);
    va_end(args);
    return n;
}

/* BSD string copies (glibc 2.38): return the length they tried to create. */
size_t __wrap_strlcpy(char* dst, const char* src, size_t size) {
    const size_t len = strlen(src);
    if (size) {
        const size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
size_t __wrap_strlcat(char* dst, const char* src, size_t size) {
    const size_t have = strnlen(dst, size);
    if (have == size) return size + strlen(src);
    return have + __wrap_strlcpy(dst + have, src, size - have);
}
size_t __wrap_wcslcpy(wchar_t* dst, const wchar_t* src, size_t size) {
    const size_t len = wcslen(src);
    if (size) {
        const size_t n = len < size - 1 ? len : size - 1;
        wmemcpy(dst, src, n);
        dst[n] = L'\0';
    }
    return len;
}
size_t __wrap_wcslcat(wchar_t* dst, const wchar_t* src, size_t size) {
    const size_t have = wcsnlen(dst, size);
    if (have == size) return size + wcslen(src);
    return have + __wrap_wcslcpy(dst + have, src, size - have);
}

/* arc4random (glibc 2.36), used by std::random_device: the kernel's random source. */
uint32_t __wrap_arc4random(void) {
    uint32_t value = 0;
    size_t got = 0;
    while (got < sizeof value) {
        const ssize_t n = getrandom((char*)&value + got, sizeof value - got, 0);
        if (n > 0) got += (size_t)n;
    }
    return value;
}
