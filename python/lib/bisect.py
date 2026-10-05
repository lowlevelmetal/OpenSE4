"""Bisection algorithms, as in CPython's bisect module (OpenSE4 script runtime).

Keeps a list sorted while inserting: bisect_left, bisect_right (bisect), insort_left
and insort_right (insort), each with the optional lo, hi and key arguments.
"""

__all__ = ["bisect", "bisect_left", "bisect_right", "insort", "insort_left", "insort_right"]


def bisect_right(a, x, lo=0, hi=None, *, key=None):
    """The index where x would go to the right of any equal items in sorted a."""
    if lo < 0:
        raise ValueError("lo must be non-negative")
    if hi is None:
        hi = len(a)
    if key is None:
        while lo < hi:
            mid = (lo + hi) // 2
            if x < a[mid]:
                hi = mid
            else:
                lo = mid + 1
    else:
        while lo < hi:
            mid = (lo + hi) // 2
            if x < key(a[mid]):
                hi = mid
            else:
                lo = mid + 1
    return lo


def bisect_left(a, x, lo=0, hi=None, *, key=None):
    """The index where x would go to the left of any equal items in sorted a."""
    if lo < 0:
        raise ValueError("lo must be non-negative")
    if hi is None:
        hi = len(a)
    if key is None:
        while lo < hi:
            mid = (lo + hi) // 2
            if a[mid] < x:
                lo = mid + 1
            else:
                hi = mid
    else:
        while lo < hi:
            mid = (lo + hi) // 2
            if key(a[mid]) < x:
                lo = mid + 1
            else:
                hi = mid
    return lo


def insort_right(a, x, lo=0, hi=None, *, key=None):
    """Inserts x into sorted a, after any equal items."""
    if key is None:
        lo = bisect_right(a, x, lo, hi)
    else:
        lo = bisect_right(a, key(x), lo, hi, key=key)
    a.insert(lo, x)


def insort_left(a, x, lo=0, hi=None, *, key=None):
    """Inserts x into sorted a, before any equal items."""
    if key is None:
        lo = bisect_left(a, x, lo, hi)
    else:
        lo = bisect_left(a, key(x), lo, hi, key=key)
    a.insert(lo, x)


bisect = bisect_right
insort = insort_right
