"""Iterator building blocks, as in CPython's itertools (OpenSE4 script runtime).

count, cycle, repeat, accumulate, batched, chain (and chain.from_iterable),
compress, dropwhile, filterfalse, groupby, islice, pairwise, starmap, takewhile,
tee, zip_longest, product, permutations, combinations and
combinations_with_replacement. Written in Python, so they cost budget like any
other script code.
"""


def count(start=0, step=1):
    n = start
    while True:
        yield n
        n += step


def cycle(iterable):
    saved = []
    for element in iterable:
        yield element
        saved.append(element)
    if not saved:
        return
    while True:
        for element in saved:
            yield element


def repeat(obj, times=None):
    if times is None:
        while True:
            yield obj
    else:
        for _ in range(times):
            yield obj


def accumulate(iterable, func=None, *, initial=None):
    it = iter(iterable)
    total = initial
    if initial is None:
        try:
            total = next(it)
        except StopIteration:
            return
    yield total
    for element in it:
        total = func(total, element) if func is not None else total + element
        yield total


def batched(iterable, n):
    if n < 1:
        raise ValueError("n must be at least one")
    batch = []
    for element in iterable:
        batch.append(element)
        if len(batch) == n:
            yield tuple(batch)
            batch = []
    if batch:
        yield tuple(batch)


class chain:
    def __init__(self, *iterables):
        self._iterables = iterables

    def __iter__(self):
        for it in self._iterables:
            for element in it:
                yield element

    @staticmethod
    def from_iterable(iterables):
        for it in iterables:
            for element in it:
                yield element


def compress(data, selectors):
    for d, s in zip(data, selectors):
        if s:
            yield d


def dropwhile(predicate, iterable):
    it = iter(iterable)
    for x in it:
        if not predicate(x):
            yield x
            break
    for x in it:
        yield x


def filterfalse(predicate, iterable):
    if predicate is None:
        predicate = bool
    for x in iterable:
        if not predicate(x):
            yield x


class _Group:
    def __init__(self, owner, key):
        self._owner = owner
        self._key = key

    def __iter__(self):
        return self

    def __next__(self):
        return self._owner._next_in_group(self)


class groupby:
    """Consecutive items with the same key: (key, group) pairs."""

    def __init__(self, iterable, key=None):
        self._it = iter(iterable)
        self._keyfunc = key if key is not None else (lambda x: x)
        self._current = None   # the group being read
        self._pending = False  # an item read ahead, waiting
        self._item = None
        self._item_key = None
        self._done = False

    def _advance(self):
        try:
            self._item = next(self._it)
        except StopIteration:
            self._done = True
            self._pending = False
            return False
        self._item_key = self._keyfunc(self._item)
        self._pending = True
        return True

    def _next_in_group(self, group):
        if group is not self._current or self._done:
            raise StopIteration
        if not self._pending and not self._advance():
            raise StopIteration
        if self._item_key != group._key:
            raise StopIteration
        self._pending = False
        return self._item

    def __iter__(self):
        return self

    def __next__(self):
        # skip what is left of the current group
        if self._current is not None:
            while not self._done:
                if not self._pending and not self._advance():
                    break
                if self._item_key != self._current._key:
                    break
                self._pending = False
        if not self._pending and not self._advance():
            raise StopIteration
        self._current = _Group(self, self._item_key)
        return self._item_key, self._current


def islice(iterable, *args):
    if len(args) == 1:
        start, stop, step = None, args[0], None
    elif len(args) in (2, 3):
        start, stop = args[0], args[1]
        step = args[2] if len(args) == 3 else None
    else:
        raise TypeError("islice expected 2 to 4 arguments")
    start = 0 if start is None else start
    step = 1 if step is None else step
    if start < 0 or (stop is not None and stop < 0) or step <= 0:
        raise ValueError("islice() indices must be non-negative and the step positive")
    nexti = start
    for i, element in enumerate(iterable):
        if stop is not None and i >= stop:
            return
        if i == nexti:
            yield element
            nexti += step


def pairwise(iterable):
    it = iter(iterable)
    try:
        a = next(it)
    except StopIteration:
        return
    for b in it:
        yield a, b
        a = b


def starmap(function, iterable):
    for args in iterable:
        yield function(*args)


def takewhile(predicate, iterable):
    for x in iterable:
        if predicate(x):
            yield x
        else:
            break


def tee(iterable, n=2):
    it = iter(iterable)
    buffers = [[] for _ in range(n)]

    def gen(mine):
        while True:
            if not mine:
                try:
                    value = next(it)
                except StopIteration:
                    return
                for b in buffers:
                    b.append(value)
            yield mine.pop(0)

    return tuple(gen(b) for b in buffers)


def zip_longest(*iterables, fillvalue=None):
    iterators = [iter(it) for it in iterables]
    active = len(iterators)
    if not active:
        return
    while True:
        values = []
        for i, it in enumerate(iterators):
            try:
                value = next(it)
            except StopIteration:
                active -= 1
                if not active:
                    return
                iterators[i] = repeat(fillvalue)
                value = fillvalue
            values.append(value)
        yield tuple(values)


def product(*iterables, repeat=1):
    pools = [tuple(pool) for pool in iterables] * repeat
    result = [[]]
    for pool in pools:
        result = [x + [y] for x in result for y in pool]
    for prod in result:
        yield tuple(prod)


def permutations(iterable, r=None):
    pool = tuple(iterable)
    n = len(pool)
    r = n if r is None else r
    if r > n:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
        else:
            return


def combinations(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != i + n - r:
                break
        else:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)


def combinations_with_replacement(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != n - 1:
                break
        else:
            return
        indices[i:] = [indices[i] + 1] * (r - i)
        yield tuple(pool[i] for i in indices)
