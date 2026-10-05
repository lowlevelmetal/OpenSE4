"""Container datatypes, as in CPython's collections (OpenSE4 script runtime).

OrderedDict is MicroPython's own. namedtuple builds on MicroPython's, adding
keyword arguments, defaults, _make, _replace and _asdict. deque, defaultdict and
Counter are written here in Python, so that they behave as in CPython: deque()
needs no maximum length, defaultdict calls its factory for missing keys, Counter
counts.
"""

from ucollections import OrderedDict
from ucollections import namedtuple as _namedtuple

__all__ = ["OrderedDict", "namedtuple", "deque", "defaultdict", "Counter"]

# The class statement makes the type carry its own name.
_NAMEDTUPLE_CLASS = """
class {name}(_base):
    def __init__(self, *args, **kwargs):
        super().__init__(*_values(args, kwargs))
"""


def _is_name(s):
    if not s or s[0].isdigit():
        return False
    for c in s:
        if not (c.isalpha() or c.isdigit() or c == "_"):
            return False
    return True


def namedtuple(typename, field_names, *, rename=False, defaults=None, module=None):
    if isinstance(field_names, str):
        field_names = field_names.replace(",", " ").split()
    names = tuple(field_names)
    for name in (typename,) + names:
        if not _is_name(name):
            raise ValueError("names must be identifiers: {!r}".format(name))
    defaults = tuple(defaults) if defaults else ()
    field_defaults = dict(zip(names[len(names) - len(defaults):], defaults))

    def values(args, kwargs):
        if len(args) > len(names):
            raise TypeError("{}() takes {} arguments but {} were given".format(typename, len(names), len(args)))
        out = list(args)
        for name in names[len(args):]:
            if name in kwargs:
                out.append(kwargs.pop(name))
            elif name in field_defaults:
                out.append(field_defaults[name])
            else:
                raise TypeError("{}() missing argument '{}'".format(typename, name))
        if kwargs:
            raise TypeError("{}() got an unexpected argument '{}'".format(typename, next(iter(kwargs))))
        return out

    namespace = {"_base": _namedtuple(typename, names), "_values": values}
    exec(_NAMEDTUPLE_CLASS.format(name=typename), namespace)
    cls = namespace[typename]
    cls._fields = names
    cls._field_defaults = field_defaults
    cls._make = classmethod(lambda c, iterable: c(*iterable))
    cls._replace = lambda self, **changes: type(self)(*[changes.pop(n) if n in changes else getattr(self, n) for n in names])
    cls._asdict = lambda self: {n: getattr(self, n) for n in names}
    cls.__repr__ = lambda self: "{}({})".format(typename, ", ".join("{}={!r}".format(n, getattr(self, n)) for n in names))
    return cls


class deque:
    """A double-ended queue: append and pop at both ends in amortised O(1)."""

    def __init__(self, iterable=(), maxlen=None):
        if maxlen is not None and maxlen < 0:
            raise ValueError("maxlen must be non-negative")
        self._items = []
        self._head = 0   # items before _head are gone
        self.maxlen = maxlen
        self.extend(iterable)

    def _compact(self):
        if self._head > 32 and self._head * 2 > len(self._items):
            del self._items[:self._head]
            self._head = 0

    def __len__(self):
        return len(self._items) - self._head

    def __bool__(self):
        return len(self._items) > self._head

    def __iter__(self):
        items = self._items
        for i in range(self._head, len(items)):
            yield items[i]

    def __reversed__(self):
        items = self._items
        for i in range(len(items) - 1, self._head - 1, -1):
            yield items[i]

    def _index(self, i):
        n = len(self)
        if i < 0:
            i += n
        if i < 0 or i >= n:
            raise IndexError("deque index out of range")
        return self._head + i

    def __getitem__(self, i):
        return self._items[self._index(i)]

    def __setitem__(self, i, value):
        self._items[self._index(i)] = value

    def __delitem__(self, i):
        del self._items[self._index(i)]

    def __contains__(self, value):
        for x in self:
            if x == value:
                return True
        return False

    def __eq__(self, other):
        return isinstance(other, deque) and list(self) == list(other)

    def __repr__(self):
        if self.maxlen is None:
            return "deque({!r})".format(list(self))
        return "deque({!r}, maxlen={})".format(list(self), self.maxlen)

    def append(self, x):
        self._items.append(x)
        if self.maxlen is not None and len(self) > self.maxlen:
            self.popleft()

    def appendleft(self, x):
        if self._head > 0:
            self._head -= 1
            self._items[self._head] = x
        else:
            self._items.insert(0, x)
        if self.maxlen is not None and len(self) > self.maxlen:
            self.pop()

    def pop(self):
        if not self:
            raise IndexError("pop from an empty deque")
        return self._items.pop()

    def popleft(self):
        if not self:
            raise IndexError("pop from an empty deque")
        x = self._items[self._head]
        self._items[self._head] = None
        self._head += 1
        if self._head == len(self._items):
            self._items = []
            self._head = 0
        else:
            self._compact()
        return x

    def extend(self, iterable):
        for x in iterable:
            self.append(x)

    def extendleft(self, iterable):
        for x in iterable:
            self.appendleft(x)

    def clear(self):
        self._items = []
        self._head = 0

    def copy(self):
        return deque(self, self.maxlen)

    def count(self, value):
        return sum(1 for x in self if x == value)

    def index(self, value, start=0, stop=None):
        items = list(self)
        if stop is None:
            stop = len(items)
        for i in range(start, stop):
            if items[i] == value:
                return i
        raise ValueError("value not in deque")

    def insert(self, i, x):
        if self.maxlen is not None and len(self) >= self.maxlen:
            raise IndexError("deque already at its maximum size")
        n = len(self)
        if i < 0:
            i = max(0, i + n)
        self._items.insert(self._head + min(i, n), x)

    def remove(self, value):
        del self[self.index(value)]

    def reverse(self):
        items = list(self)
        items.reverse()
        self._items = items
        self._head = 0

    def rotate(self, n=1):
        length = len(self)
        if not length:
            return
        n %= length
        if n:
            items = list(self)
            self._items = items[-n:] + items[:-n]
            self._head = 0


class defaultdict(dict):
    """A dict that makes missing values with default_factory()."""

    def __init__(self, default_factory=None, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.default_factory = default_factory

    def __getitem__(self, key):
        try:
            return super().__getitem__(key)
        except KeyError:
            return self.__missing__(key)

    def __missing__(self, key):
        if self.default_factory is None:
            raise KeyError(key)
        value = self.default_factory()
        self[key] = value
        return value

    def copy(self):
        return defaultdict(self.default_factory, self)

    def __repr__(self):
        return "defaultdict({!r}, {!r})".format(self.default_factory, dict(self))


class Counter(dict):
    """A dict that counts things: missing keys count zero."""

    def __init__(self, iterable=None, **kwargs):
        super().__init__()
        self.update(iterable, **kwargs)

    def __getitem__(self, key):
        try:
            return super().__getitem__(key)
        except KeyError:
            return 0

    def __missing__(self, key):
        return 0

    def update(self, iterable=None, **kwargs):
        if iterable is not None:
            if isinstance(iterable, dict):
                for elem, count in iterable.items():
                    self[elem] = self[elem] + count
            else:
                for elem in iterable:
                    self[elem] = self[elem] + 1
        for elem, count in kwargs.items():
            self[elem] = self[elem] + count

    def subtract(self, iterable=None, **kwargs):
        if iterable is not None:
            if isinstance(iterable, dict):
                for elem, count in iterable.items():
                    self[elem] = self[elem] - count
            else:
                for elem in iterable:
                    self[elem] = self[elem] - 1
        for elem, count in kwargs.items():
            self[elem] = self[elem] - count

    def most_common(self, n=None):
        # sorted by count, highest first; equal counts keep the order they were met (stable)
        items = sorted(self.items(), key=lambda item: item[1], reverse=True)
        return items if n is None else items[:n]

    def elements(self):
        for elem, count in self.items():
            for _ in range(count):
                yield elem

    def total(self):
        return sum(self.values())

    def copy(self):
        return Counter(self)

    def __add__(self, other):
        result = Counter()
        for elem, count in self.items():
            if count + other[elem] > 0:
                result[elem] = count + other[elem]
        for elem, count in other.items():
            if elem not in self and count > 0:
                result[elem] = count
        return result

    def __sub__(self, other):
        result = Counter()
        for elem, count in self.items():
            if count - other[elem] > 0:
                result[elem] = count - other[elem]
        for elem, count in other.items():
            if elem not in self and count < 0:
                result[elem] = -count
        return result

    def __repr__(self):
        return "Counter({!r})".format(dict(self))
