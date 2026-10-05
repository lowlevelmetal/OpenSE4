"""Mathematical functions (OpenSE4 script runtime): MicroPython's math module, plus
the functions of CPython's that it lacks: gcd, lcm, isqrt, comb, perm, prod, dist,
hypot with any number of coordinates, fsum, cbrt and exp2.

The transcendental functions are the same code on every platform, so they give the
same results everywhere (they may differ from CPython's in the last digit).
"""

from umath import *
import umath as _m


def gcd(*integers):
    result = 0
    for n in integers:
        a, b = abs(result), abs(n)
        while b:
            a, b = b, a % b
        result = a
    return result


def lcm(*integers):
    result = 1
    for n in integers:
        if n == 0:
            return 0
        result = abs(result * n) // gcd(result, n)
    return result


def isqrt(n):
    """The integer square root: the largest x with x * x <= n."""
    n = int(n)
    if n < 0:
        raise ValueError("isqrt() argument must be nonnegative")
    if n == 0:
        return 0
    x = 1   # a first guess above the root: a power of two
    while x * x <= n:
        x <<= 1
    while True:
        y = (x + n // x) >> 1
        if y >= x:
            return x
        x = y


def comb(n, k):
    if n < 0 or k < 0:
        raise ValueError("n and k must be non-negative")
    if k > n:
        return 0
    k = min(k, n - k)
    result = 1
    for i in range(1, k + 1):
        result = result * (n - k + i) // i
    return result


def perm(n, k=None):
    if k is None:
        k = n
    if n < 0 or k < 0:
        raise ValueError("n and k must be non-negative")
    if k > n:
        return 0
    result = 1
    for i in range(n - k + 1, n + 1):
        result *= i
    return result


def prod(iterable, *, start=1):
    result = start
    for x in iterable:
        result *= x
    return result


def hypot(*coordinates):
    total = 0.0
    for c in coordinates:
        total += float(c) * float(c)
    return _m.sqrt(total)


def dist(p, q):
    if len(p) != len(q):
        raise ValueError("both points must have the same number of dimensions")
    return hypot(*[a - b for a, b in zip(p, q)])


def fsum(iterable):
    """An exact floating-point sum (Shewchuk's algorithm), rounded once at the end."""
    partials = []
    for x in iterable:
        x = float(x)
        i = 0
        for y in partials:
            if abs(x) < abs(y):
                x, y = y, x
            hi = x + y
            lo = y - (hi - x)
            if lo:
                partials[i] = lo
                i += 1
            x = hi
        partials[i:] = [x]
    total = 0.0
    for p in reversed(partials):
        total += p
    return total


def cbrt(x):
    if x < 0:
        return -_m.pow(-x, 1.0 / 3.0)
    return _m.pow(x, 1.0 / 3.0)


def exp2(x):
    return _m.pow(2.0, x)
