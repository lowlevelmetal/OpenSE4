"""Heap queue, as in CPython's heapq (OpenSE4 script runtime).

heappush, heappop and heapify are MicroPython's own; heappushpop, heapreplace,
nlargest, nsmallest and merge are added here.
"""

from uheapq import heapify, heappop, heappush

__all__ = ["heappush", "heappop", "heapify", "heapreplace", "heappushpop", "nlargest", "nsmallest", "merge"]


def _sift_towards_root(heap, start, pos):
    item = heap[pos]
    while pos > start:
        parent = (pos - 1) >> 1
        if item < heap[parent]:
            heap[pos] = heap[parent]
            pos = parent
        else:
            break
    heap[pos] = item


def _sift_from_root(heap, pos):
    end = len(heap)
    start = pos
    item = heap[pos]
    child = 2 * pos + 1
    while child < end:
        right = child + 1
        if right < end and not heap[child] < heap[right]:
            child = right
        heap[pos] = heap[child]
        pos = child
        child = 2 * pos + 1
    heap[pos] = item
    _sift_towards_root(heap, start, pos)


def heapreplace(heap, item):
    """Pops the smallest item, then pushes item."""
    if not heap:
        raise IndexError("index out of range")
    smallest = heap[0]
    heap[0] = item
    _sift_from_root(heap, 0)
    return smallest


def heappushpop(heap, item):
    """Pushes item, then pops the smallest item, faster than the two calls."""
    if heap and heap[0] < item:
        item, heap[0] = heap[0], item
        _sift_from_root(heap, 0)
    return item


def nsmallest(n, iterable, key=None):
    """The n smallest items, smallest first; equal items in the order they came."""
    return sorted(iterable, key=key)[:max(n, 0)]


def nlargest(n, iterable, key=None):
    """The n largest items, largest first; equal items in the order they came."""
    return sorted(iterable, key=key, reverse=True)[:max(n, 0)]


def merge(*iterables, key=None, reverse=False):
    """Merges sorted iterables into one sorted stream (earlier iterables first on ties)."""
    items = []
    for it in iterables:
        items.extend(it)
    items.sort(key=key, reverse=reverse)
    for item in items:
        yield item
