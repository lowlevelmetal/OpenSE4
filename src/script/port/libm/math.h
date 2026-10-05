// A stand-in for <math.h>, used only while compiling the bundled double-precision
// math functions (third_party/micropython/lib/libm_dbl, from musl; see
// cmake/MicroPython.cmake). It gives every function they define a name of its own
// (mp_musl_*), so that they never replace the C library's functions for the rest of
// the program, and sends the exact operations they use (floor, fabs, sqrt...) to
// the platform's, through port.c. Results are then the same on every platform:
// the transcendental functions are this code, and IEEE 754 fixes the exact ones.

#ifndef OPENSE4_LIBM_SHIM_MATH_H
#define OPENSE4_LIBM_SHIM_MATH_H

#include <float.h>
#include <stdint.h>

#ifndef FLT_EVAL_METHOD
#define FLT_EVAL_METHOD 0
#endif

typedef float float_t;
typedef double double_t;

// Functions defined by the bundled library.
#define __cos mp_musl___cos
#define __expo2 mp_musl___expo2
#define __fpclassifyd mp_musl___fpclassifyd
#define __rem_pio2 mp_musl___rem_pio2
#define __rem_pio2_large mp_musl___rem_pio2_large
#define __signbitd mp_musl___signbitd
#define __sin mp_musl___sin
#define __tan mp_musl___tan
#define __lgamma_r mp_musl___lgamma_r
#define acos mp_musl_acos
#define acosh mp_musl_acosh
#define asin mp_musl_asin
#define asinh mp_musl_asinh
#define atan mp_musl_atan
#define atan2 mp_musl_atan2
#define atanh mp_musl_atanh
#define cos mp_musl_cos
#define cosh mp_musl_cosh
#define erf mp_musl_erf
#define erfc mp_musl_erfc
#define exp mp_musl_exp
#define expm1 mp_musl_expm1
#define lgamma mp_musl_lgamma
#define log mp_musl_log
#define log10 mp_musl_log10
#define log1p mp_musl_log1p
#define pow mp_musl_pow
#define scalbn mp_musl_scalbn
#define sin mp_musl_sin
#define sinh mp_musl_sinh
#define tan mp_musl_tan
#define tanh mp_musl_tanh
#define tgamma mp_musl_tgamma

double acos(double);
double acosh(double);
double asin(double);
double asinh(double);
double atan(double);
double atan2(double, double);
double atanh(double);
double cos(double);
double cosh(double);
double erf(double);
double erfc(double);
double exp(double);
double expm1(double);
double lgamma(double);
double log(double);
double log10(double);
double log1p(double);
double pow(double, double);
double scalbn(double, int);
double sin(double);
double sinh(double);
double tan(double);
double tanh(double);
double tgamma(double);

// Exact operations: the platform's (port.c).
#define ceil mp_libm_ceil
#define copysign mp_libm_copysign
#define fabs mp_libm_fabs
#define floor mp_libm_floor
#define fmod mp_libm_fmod
#define frexp mp_libm_frexp
#define ldexp mp_libm_ldexp
#define modf mp_libm_modf
#define rint mp_libm_nearbyint
#define sqrt mp_libm_sqrt
#define trunc mp_libm_trunc

double ceil(double);
double copysign(double, double);
double fabs(double);
double floor(double);
double fmod(double, double);
double frexp(double, int *);
double ldexp(double, int);
double modf(double, double *);
double rint(double);
double sqrt(double);
double trunc(double);

// Classification, as musl defines it.
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
int __fpclassifyd(double);
int __signbitd(double);
#define fpclassify(x) __fpclassifyd(x)
#define signbit(x) __signbitd(x)
#define isnan(x) (__fpclassifyd(x) == FP_NAN)
#define isinf(x) (__fpclassifyd(x) == FP_INFINITE)
#define isfinite(x) (__fpclassifyd(x) > FP_INFINITE)
#define isnormal(x) (__fpclassifyd(x) == FP_NORMAL)

#define INFINITY (1e300 * 1e300)
#define HUGE_VAL INFINITY

#endif // OPENSE4_LIBM_SHIM_MATH_H
