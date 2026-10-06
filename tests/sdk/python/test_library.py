"""Tests of the script runtime's Python library (python/lib), written so that they run
the same under CPython 3.10 or newer: run() returns the failures, an empty list
when everything passes. tests/sdk/test_script_library.cpp runs it in the runtime;
`python3 tests/sdk/python/test_library.py` runs it under CPython.
"""

from __future__ import annotations

import bisect
import collections
import copy
import functools
import heapq
import itertools
import math
import operator
import string
import traceback
from abc import ABC, abstractmethod
from dataclasses import FrozenInstanceError, asdict, astuple, dataclass, field, fields, is_dataclass, replace
from typing import Any, Callable, ClassVar, Dict, Generic, List, NamedTuple, Optional, Tuple, TypeVar, cast

T = TypeVar("T")
Vector = Tuple[int, int]


def test_typing():
    class Box(Generic[T]):
        def __init__(self, value: T) -> None:
            self.value = value

        def get(self) -> T:
            return self.value

    def apply(f: Callable[[int], int], xs: List[int]) -> Dict[str, Optional[int]]:
        return {str(x): f(x) for x in xs}

    assert Box(3).get() == 3
    assert apply(lambda x: x * 2, [1, 2]) == {"1": 2, "2": 4}
    assert cast(int, "x") == "x"
    Point = NamedTuple("Point", [("x", int), ("y", int)])
    p = Point(1, 2)
    assert p.x == 1 and p.y == 2 and p == (1, 2)


@dataclass
class Order:
    target: int
    priority: int = 0
    tags: list = field(default_factory=list)
    created: ClassVar[int] = 0

    def score(self) -> int:
        return self.target * 10 + self.priority


@dataclass(frozen=True, order=True)
class Position:
    x: int
    y: int = 0


@dataclass
class Fleet:
    name: str
    orders: List[Order] = field(default_factory=list)
    flagship: Optional[Position] = None

    def __post_init__(self):
        self.name = self.name.upper()


def test_dataclasses():
    o = Order(5, tags=["a"])
    assert o.target == 5 and o.priority == 0 and o.tags == ["a"]
    assert o.score() == 50
    assert repr(o) == "Order(target=5, priority=0, tags=['a'])"
    assert o == Order(5, 0, ["a"]) and o != Order(5, 1, ["a"])
    assert Order(1).tags is not Order(1).tags
    assert [f.name for f in fields(Order)] == ["target", "priority", "tags"]
    assert asdict(o) == {"target": 5, "priority": 0, "tags": ["a"]}
    assert astuple(o) == (5, 0, ["a"])
    assert replace(o, priority=3) == Order(5, 3, ["a"])
    assert is_dataclass(o) and is_dataclass(Order) and not is_dataclass(1)
    try:
        Order()
        assert False, "a missing argument should fail"
    except TypeError:
        pass
    p = Position(2, 3)
    try:
        p.x = 1
        assert False, "frozen"
    except FrozenInstanceError:
        pass
    assert sorted([Position(2), Position(1, 5), Position(1, 2)]) == [Position(1, 2), Position(1, 5), Position(2, 0)]
    assert {p: "here"}[Position(2, 3)] == "here"
    f = Fleet("home", [Order(1)], Position(0, 0))
    assert f.name == "HOME"
    assert asdict(f) == {"name": "HOME", "orders": [{"target": 1, "priority": 0, "tags": []}],
                         "flagship": {"x": 0, "y": 0}}


def test_itertools():
    assert list(itertools.islice(itertools.count(5, 2), 3)) == [5, 7, 9]
    assert list(itertools.islice("abcdefg", 1, 6, 2)) == ["b", "d", "f"]
    assert list(itertools.islice(itertools.cycle("ab"), 5)) == ["a", "b", "a", "b", "a"]
    assert list(itertools.repeat(7, 3)) == [7, 7, 7]
    assert list(itertools.accumulate([1, 2, 3, 4])) == [1, 3, 6, 10]
    assert list(itertools.accumulate([1, 2, 3], operator.mul, initial=10)) == [10, 10, 20, 60]
    assert list(itertools.chain([1, 2], (3,), "ab")) == [1, 2, 3, "a", "b"]
    assert list(itertools.chain.from_iterable([[1], [2, 3]])) == [1, 2, 3]
    assert list(itertools.compress("abcd", [1, 0, 1, 0])) == ["a", "c"]
    assert list(itertools.dropwhile(lambda x: x < 3, [1, 2, 3, 1])) == [3, 1]
    assert list(itertools.takewhile(lambda x: x < 3, [1, 2, 3, 1])) == [1, 2]
    assert list(itertools.filterfalse(lambda x: x % 2, range(6))) == [0, 2, 4]
    assert [(k, list(g)) for k, g in itertools.groupby("aabbbca")] == [
        ("a", ["a", "a"]), ("b", ["b", "b", "b"]), ("c", ["c"]), ("a", ["a"])]
    assert [k for k, _ in itertools.groupby([1, 1, 2, 3, 3])] == [1, 2, 3]
    assert list(itertools.pairwise("abc")) == [("a", "b"), ("b", "c")]
    assert list(itertools.starmap(pow, [(2, 3), (3, 2)])) == [8, 9]
    a, b = itertools.tee([1, 2, 3])
    assert list(a) == [1, 2, 3] and list(b) == [1, 2, 3]
    assert list(itertools.zip_longest("ab", "xyz", fillvalue="-")) == [("a", "x"), ("b", "y"), ("-", "z")]
    assert list(itertools.product("ab", [1, 2])) == [("a", 1), ("a", 2), ("b", 1), ("b", 2)]
    assert len(list(itertools.product(range(3), repeat=3))) == 27
    assert list(itertools.permutations("abc", 2)) == [
        ("a", "b"), ("a", "c"), ("b", "a"), ("b", "c"), ("c", "a"), ("c", "b")]
    assert len(list(itertools.permutations(range(4)))) == 24
    assert list(itertools.combinations("abcd", 2)) == [
        ("a", "b"), ("a", "c"), ("a", "d"), ("b", "c"), ("b", "d"), ("c", "d")]
    assert list(itertools.combinations_with_replacement("ab", 2)) == [("a", "a"), ("a", "b"), ("b", "b")]


def test_functools():
    assert functools.reduce(operator.add, [1, 2, 3, 4]) == 10
    assert functools.reduce(lambda a, b: a * b, [], 5) == 5
    double = functools.partial(operator.mul, 2)
    assert double(21) == 42

    def scaled(value, factor=1, offset=0):
        return value * factor + offset

    assert functools.partial(scaled, factor=3)(2, offset=1) == 7

    calls = []

    @functools.lru_cache(maxsize=2)
    def square(n):
        calls.append(n)
        return n * n

    assert [square(2), square(2), square(3), square(4), square(2)] == [4, 4, 9, 16, 4]
    assert calls == [2, 3, 4, 2]
    assert square.cache_info().hits == 1

    @functools.cache
    def fib(n):
        return n if n < 2 else fib(n - 1) + fib(n - 2)

    assert fib(90) == 2880067194370816120

    assert sorted([3, 1, 2], key=functools.cmp_to_key(lambda a, b: b - a)) == [3, 2, 1]

    @functools.total_ordering
    class Version:
        def __init__(self, n):
            self.n = n

        def __eq__(self, other):
            return self.n == other.n

        def __lt__(self, other):
            return self.n < other.n

    assert Version(1) <= Version(2) and Version(3) > Version(2) and Version(2) >= Version(2)

    class Lazy:
        count = 0

        @functools.cached_property
        def value(self):
            Lazy.count += 1
            return 42

    lazy = Lazy()
    assert lazy.value == 42 and lazy.value == 42 and Lazy.count == 1

    def decorator(f):
        @functools.wraps(f)
        def wrapper(*args):
            return f(*args) + 1
        return wrapper

    @decorator
    def base(x):
        return x

    assert base(1) == 2

    @functools.singledispatch
    def kind(x):
        return "thing"

    @kind.register(int)
    def _(x):
        return "number"

    assert kind(3) == "number" and kind("s") == "thing"


def test_bisect():
    a = [1, 3, 3, 5]
    assert bisect.bisect_left(a, 3) == 1 and bisect.bisect_right(a, 3) == 3 and bisect.bisect(a, 4) == 3
    bisect.insort(a, 4)
    bisect.insort_left(a, 0)
    assert a == [0, 1, 3, 3, 4, 5]
    records = [("a", 1), ("b", 5), ("c", 9)]
    assert bisect.bisect_left(records, 5, key=lambda r: r[1]) == 1
    bisect.insort(records, ("x", 7), key=lambda r: r[1])
    assert [r[0] for r in records] == ["a", "b", "x", "c"]


def test_collections():
    d = collections.deque()
    d.append(1)
    d.appendleft(0)
    d.extend([2, 3])
    assert list(d) == [0, 1, 2, 3] and len(d) == 4 and d[0] == 0 and d[-1] == 3
    assert d.popleft() == 0 and d.pop() == 3 and list(d) == [1, 2]
    d.rotate(1)
    assert list(d) == [2, 1]
    bounded = collections.deque(range(5), maxlen=3)
    assert list(bounded) == [2, 3, 4]
    bounded.appendleft(9)
    assert list(bounded) == [9, 2, 3]
    queue = collections.deque()
    for i in range(1000):
        queue.append(i)
    total = 0
    while queue:
        total += queue.popleft()
    assert total == 499500
    dd = collections.defaultdict(list)
    dd["a"].append(1)
    dd["a"].append(2)
    assert dd["a"] == [1, 2] and dict(dd) == {"a": [1, 2]} and "b" not in dd
    counts = collections.defaultdict(int)
    for w in "the cat the hat".split():
        counts[w] += 1
    assert dict(counts) == {"the": 2, "cat": 1, "hat": 1}
    c = collections.Counter("abracadabra")
    assert c["a"] == 5 and c["z"] == 0
    assert c.most_common(2) == [("a", 5), ("b", 2)]
    c.update("aaz")
    assert c["a"] == 7 and c["z"] == 1
    assert sum(collections.Counter([1, 1, 2]).values()) == 3
    od = collections.OrderedDict()
    od["z"] = 1
    od["a"] = 2
    assert list(od) == ["z", "a"]
    P = collections.namedtuple("P", ["x", "y"])
    p = P(1, y=2)
    assert p.x == 1 and p[1] == 2 and p._replace(x=5) == (5, 2) and P._fields == ("x", "y")
    assert p == (1, 2) and isinstance(p, tuple) and repr(p) == "P(x=1, y=2)" and p._asdict() == {"x": 1, "y": 2}
    x, y = p
    assert (x, y) == (1, 2) and P._make([3, 4]) == (3, 4)
    Q = collections.namedtuple("Q", "a b c", defaults=[0, 9])
    assert Q(1) == (1, 0, 9) and Q(1, c=2) == (1, 0, 2) and type(Q(1)).__name__ == "Q"


def test_heapq():
    h = []
    for x in [5, 1, 4, 2, 3]:
        heapq.heappush(h, x)
    assert [heapq.heappop(h) for _ in range(5)] == [1, 2, 3, 4, 5]
    h = [5, 1, 4]
    heapq.heapify(h)
    assert heapq.heappushpop(h, 0) == 0
    assert heapq.heapreplace(h, 9) == 1
    assert sorted(h) == [4, 5, 9]
    assert heapq.nlargest(2, [1, 5, 3]) == [5, 3]
    assert heapq.nsmallest(2, [4, 1, 3]) == [1, 3]
    assert heapq.nsmallest(1, ["bb", "a", "ccc"], key=len) == ["a"]
    assert list(heapq.merge([1, 4, 7], [2, 3, 8])) == [1, 2, 3, 4, 7, 8]
    tasks = []
    heapq.heappush(tasks, (2, "b"))
    heapq.heappush(tasks, (1, "a"))
    assert heapq.heappop(tasks) == (1, "a")


def test_math():
    assert math.gcd(12, 18) == 6 and math.gcd(0, 5) == 5 and math.gcd(12, 18, 8) == 2
    assert math.lcm(4, 6) == 12
    assert math.isqrt(10 ** 20 + 5) == 10 ** 10 and math.isqrt(15) == 3 and math.isqrt(16) == 4
    assert math.comb(10, 3) == 120 and math.perm(5, 2) == 20 and math.perm(4) == 24
    assert math.prod([1, 2, 3, 4]) == 24 and math.prod([]) == 1
    assert math.hypot(3, 4) == 5.0 and math.dist((0, 0), (3, 4)) == 5.0
    assert math.fsum([0.1] * 10) == 1.0
    assert math.sqrt(16) == 4.0 and math.floor(2.5) == 2 and math.ceil(2.1) == 3
    assert math.isclose(math.sin(math.pi / 2), 1.0)
    assert math.log2(1024) == 10.0 and math.log10(1000) == 3.0
    assert math.inf > 10 ** 300 and math.isnan(math.nan)


def test_operator_copy_string_abc():
    assert operator.itemgetter(1)([5, 6]) == 6 and operator.itemgetter(0, 2)("abc") == ("a", "c")

    class Ship:
        def __init__(self, hp):
            self.hp = hp
            self.kids = []

    ships = [Ship(3), Ship(1), Ship(2)]
    assert [s.hp for s in sorted(ships, key=operator.attrgetter("hp"))] == [1, 2, 3]
    assert operator.methodcaller("upper")("a") == "A"
    a = Ship(1)
    a.kids.append(Ship(2))
    b = copy.deepcopy(a)
    b.kids[0].hp = 9
    assert a.kids[0].hp == 2 and b.kids[0].hp == 9
    shallow = copy.copy(a)
    assert shallow.kids is a.kids
    nested = {"x": [1, [2]]}
    deep = copy.deepcopy(nested)
    deep["x"][1].append(3)
    assert nested == {"x": [1, [2]]}
    assert string.ascii_lowercase[:3] == "abc" and string.digits == "0123456789"

    class Strategy(ABC):
        @abstractmethod
        def plan(self):
            ...

    class Attack(Strategy):
        def plan(self):
            return "attack"

    assert Attack().plan() == "attack"


def test_language():
    # dicts keep insertion order
    d = {"b": 1, "a": 2}
    d["c"] = 3
    del d["b"]
    d["b"] = 4
    assert list(d) == ["a", "c", "b"]
    assert dict({"x": 1}, y=2) == {"x": 1, "y": 2}
    # sorting is stable
    pairs = [(1, "b"), (0, "z"), (1, "a")]
    assert sorted(pairs, key=lambda p: p[0]) == [(0, "z"), (1, "b"), (1, "a")]
    assert sorted(pairs, key=lambda p: p[0], reverse=True) == [(1, "b"), (1, "a"), (0, "z")]
    # f-strings, comprehensions, generators, closures, big integers
    width = 6
    assert f"{3.14159:.2f}|{42:>{width}}|{'x'!r}" == "3.14|    42|'x'"
    assert {k: v for k, v in zip("ab", range(2))} == {"a": 0, "b": 1}
    assert sum(x * x for x in range(10)) == 285

    def counter():
        n = 0

        def bump():
            nonlocal n
            n += 1
            return n
        return bump

    c = counter()
    c()
    assert c() == 2
    assert 2 ** 100 == 1267650600228229401496703205376
    assert (-7) // 2 == -4 and (-7) % 2 == 1 and divmod(7, -2) == (-4, -1)
    assert round(2.5) == 2 and round(3.5) == 4 and round(1.2345, 2) == 1.23

    class Walrus:
        pass

    if (n := len([1, 2, 3])) > 2:
        assert n == 3


def test_traceback():
    def inner():
        raise ValueError("bad target")

    def outer():
        inner()

    try:
        outer()
    except ValueError as e:
        lines = traceback.format_exception(e)
        text = traceback.format_exc()
        only = traceback.format_exception_only(e)
    assert lines[0] == "Traceback (most recent call last):\n"
    assert lines[-1] == "ValueError: bad target\n"
    assert all(line.endswith("\n") for line in lines)
    joined = "".join(lines)
    assert "in inner" in joined and "in outer" in joined and joined.index("in outer") < joined.index("in inner")
    assert text == joined
    assert only == ["ValueError: bad target\n"]
    assert traceback.format_exception_only(KeyError()) == ["KeyError\n"]


TESTS = [
    test_typing, test_dataclasses, test_itertools, test_functools, test_bisect, test_collections,
    test_heapq, test_math, test_operator_copy_string_abc, test_language, test_traceback,
]


def run():
    failures = []
    for test in TESTS:
        try:
            test()
        except Exception as e:
            failures.append("{}: {}: {}".format(test.__name__, type(e).__name__, e))
    return failures


if __name__ == "__main__":
    problems = run()
    for p in problems:
        print(p)
    print("ok" if not problems else "{} failed".format(len(problems)))
