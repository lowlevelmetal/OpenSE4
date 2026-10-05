"""A workload whose output must be identical on every platform (docs/sdk/runtime.md,
"The same on every computer"): tests/sdk/test_script_determinism.cpp checks a
checksum of what run() returns, and of the budget it used, against a golden value
on Linux x86_64, Windows and 32-bit ARM.

It touches what could differ between computers: the order of dicts and sets of
strings, ints, tuples and objects, hashes and identities, sorting, big integers,
floats and the math functions (rounded to ints, and printed), string formatting.
"""

import math
import json
import re
import struct
import heapq
from collections import OrderedDict, deque, Counter


class Ship:
    def __init__(self, name, hp):
        self.name = name
        self.hp = hp

    def __repr__(self):
        return "Ship({}, {})".format(self.name, self.hp)


class Plain:
    pass


def lcg(seed):
    # a small generator of our own, so the workload needs no random module
    state = seed
    while True:
        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)
        yield state >> 33


def run():
    out = []
    put = out.append
    rand = lcg(12345)

    # sets and dicts of strings, ints (small, beyond 32 and 64 bits, negative), tuples
    words = ["w{}".format(next(rand) % 1000) for _ in range(200)]
    ints = [next(rand) % 100000 - 50000 for _ in range(200)] + [2 ** 31 + i for i in range(20)] + \
           [-(2 ** 40) - i for i in range(20)] + [2 ** 70 + i for i in range(10)]
    tuples = [(next(rand) % 50, "t{}".format(next(rand) % 7)) for _ in range(100)]
    put(repr(set(words)))
    put(repr(set(ints)))
    put(repr(set(tuples)))
    put(repr(frozenset(words[:20])))
    put(repr({w: i for i, w in enumerate(words)}))
    put(repr(dict.fromkeys(ints)))
    put(repr({t: len(t[1]) for t in tuples}))
    mixed = {None: 0, True: 1, 2.5: 2, "x": 3, (1, (2, 3)): 4, 10 ** 30: 5, -1: 6}
    put(repr(mixed))
    put(repr(set(mixed)))

    # objects: identity, hashes, sets and dicts of them
    ships = [Ship("s{}".format(i), next(rand) % 100) for i in range(60)]
    plains = [Plain() for _ in range(30)]
    put(repr([hash(s) for s in ships[:10]]))
    put(repr([id(p) for p in plains[:10]]))
    put(repr(plains[3]))
    put(repr(list(set(ships))[:20]))
    put(repr([hash(x) for x in (None, True, Ship, len, "abc", b"abc", 2 ** 100, -7, 1.5, (1, 2), frozenset([1, 2]))]))
    by_ship = {s: s.hp for s in ships}
    put(repr(list(by_ship.values())[:20]))
    seen = set()
    for p in plains + plains[::2]:
        seen.add(p)
    put(repr(sorted(id(p) for p in seen)[:10]))

    # sorting: stable, with keys, reversed
    put(repr(sorted(ships, key=lambda s: s.hp)[:15]))
    put(repr(sorted(ships, key=lambda s: s.hp, reverse=True)[:15]))
    put(repr(sorted(words)[:20]))
    put(repr(sorted(tuples)))
    put(repr(sorted(ints, key=abs)[:30]))

    # big integers
    f = 1
    for i in range(1, 120):
        f *= i
    put(str(f))
    put(str(3 ** 300 // 7 ** 50))
    put(str(pow(3, 10 ** 6, 10 ** 30 + 7)))
    put(str((2 ** 200 + 12345) % 1000000007))
    put(hex(2 ** 130 ^ 2 ** 64 - 1))
    put(str(int("9" * 80) - int("1" * 79)))
    put(repr(divmod(-(10 ** 40), 7)))

    # floats: arithmetic, math functions, rounded to ints and printed
    acc = []
    for i in range(1, 400):
        x = i / 7.0
        acc.append(int(math.sin(x) * 1e9))
        acc.append(int(math.cos(x * 3) * 1e9))
        acc.append(int(math.tan(x / 10) * 1e9))
        acc.append(int(math.exp(x / 50) * 1e6))
        acc.append(int(math.log(x) * 1e9))
        acc.append(int(math.log10(x) * 1e9))
        acc.append(int(math.sqrt(x) * 1e9))
        acc.append(int(math.atan2(x, i % 13 - 6) * 1e9))
        acc.append(int(math.pow(x, 1.37) * 1e3))
        acc.append(int(math.asin((i % 200) / 200.0) * 1e9))
        acc.append(int(math.sinh(x / 30) * 1e9))
        acc.append(int(math.hypot(x, i) * 1e6))
        acc.append(round(x * 1000) + math.floor(x * 3.3) + math.ceil(-x / 3))
    put(repr(sum(acc)))
    put(repr(acc[:40]))
    put(repr([math.gamma(i / 3) for i in range(1, 12)]))
    put(repr([math.lgamma(i / 3) for i in range(1, 12)]))
    put(repr([math.erf(i / 5) for i in range(-5, 6)]))
    put(repr([1 / 3, 2 / 3, 0.1 + 0.2, 1e22, 1e-7, 123456.789, -0.0, 2.5e300 * 10, float("nan")]))
    put("{:.6f} {:.3e} {:g} {:10.4f} {:%}".format(math.pi, math.e * 1e10, 1 / 7, -math.sqrt(2), 0.125))
    put("%5.2f|%-8s|%x|%r|%c" % (math.tau, "left", 48879, "q", 65))
    put(f"{12345678:,} {0.5:.0f} {1.5:.0f} {2.675:.2f} {-7:+d} {255:#b}")
    put(str(float(2 ** 80)) + " " + str(int(1e20)) + " " + str(7.0 // 2) + " " + str(-7.5 % 2))

    # strings, json, re, struct, heapq, deque, Counter
    text = " ".join(words[:50])
    put(json.dumps({"words": words[:10], "n": ints[:5], "nested": {"a": [1, 2, {"b": None}]}}))
    put(repr(json.loads('{"z": [1, 2, 3], "a": {"x": true, "y": null}}')))
    put(repr(re.findall(r"w(\d+)", text)[:20]))
    put(re.sub(r"(\d)(\d)", r"\2\1", text[:80]))
    put(repr(struct.pack("<iHqd", -5, 65535, 2 ** 40, 1.25)))
    put(repr(struct.pack("ll", 1, -1)))
    heap = []
    for t in tuples:
        heapq.heappush(heap, t)
    put(repr([heapq.heappop(heap) for _ in range(20)]))
    dq = deque(maxlen=10)
    for i in range(25):
        dq.append(i)
    put(repr(list(dq)))
    put(repr(Counter(words).most_common(10)))
    od = OrderedDict()
    for w in words[:30]:
        od[w] = len(w)
    put(repr(list(od.items())[:10]))
    put(repr("Ünïcödé ✓".upper()) + repr("ΑΒΓ".lower()) + repr(len("🚀x")))
    return "\n".join(out)
