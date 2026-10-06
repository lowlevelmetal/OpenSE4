"""opense4.rng: the same numbers on every runtime. The golden values were checked
against PCG32's reference implementation in C; the C++ side also compares what both
runtimes publish here."""

import support
from opense4.rng import Random


def test_pcg32_reference():
    # The reference implementation's demo: seed 42, stream 54.
    r = Random(42, 54)
    assert [r.next32() for _ in range(6)] == [0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e]
    r = Random(-1)   # seeds are taken modulo 2**64
    assert [r.next32() for _ in range(3)] == [0, 3837872008, 932996374]


def _sequence(seed):
    r = Random(seed)
    out = {}
    out["next32"] = [r.next32() for _ in range(6)]
    out["randint"] = [r.randint(1, 6) for _ in range(12)]
    out["big"] = [r.randrange(0, 10 ** 12) for _ in range(3)]
    out["bits"] = [r.getrandbits(k) for k in (1, 7, 33, 64, 100)]
    x = list(range(10))
    r.shuffle(x)
    out["shuffle"] = x
    out["sample"] = r.sample(list(range(20)), 5)
    out["choices"] = r.choices(["a", "b", "c"], weights=[1, 2, 3], k=8)
    out["chance"] = [r.chance(30) for _ in range(10)]
    out["random53"] = [int(r.random() * 2 ** 53) for _ in range(3)]
    out["ranges"] = [r.randrange(-5, 5) for _ in range(5)] + [r.randrange(10, 0, -3) for _ in range(5)]
    return out


def test_golden_sequence():
    out = _sequence(20261005)
    assert out["next32"] == [2198128954, 2599468490, 3279935368, 3379789619, 664935781, 819301179]
    assert out["randint"] == [1, 3, 5, 4, 2, 4, 2, 1, 3, 6, 3, 6]
    assert out["big"] == [460409461894, 459777437767, 541420171810]
    assert out["bits"] == [1, 15, 3205410893, 1144834829216642996, 271548244643987270213364875589]
    assert out["shuffle"] == [4, 2, 8, 3, 5, 0, 9, 7, 6, 1]
    assert out["sample"] == [9, 2, 12, 18, 4]
    assert out["choices"] == ["c", "c", "b", "a", "b", "b", "a", "c"]
    assert out["chance"] == [False, True, False, False, True, True, False, False, False, False]
    assert out["random53"] == [5210157044862682, 7963223366541913, 1303957757359185]
    assert out["ranges"] == [3, -4, -3, -4, -4, 10, 7, 1, 1, 7]
    # The big numbers cross to the engine as text: they need not fit in 64 bits.
    support.publish("random", {k: [str(v) for v in vs] for k, vs in out.items()})


def test_seeds_and_state():
    a, b = Random(7), Random(7)
    assert [a.next32() for _ in range(5)] == [b.next32() for _ in range(5)]
    assert Random(7).next32() != Random(8).next32()
    assert Random(7, 1).next32() != Random(7, 2).next32()
    r = Random(99)
    r.next32()
    state = r.getstate()
    later = [r.next32() for _ in range(4)]
    s = Random()
    s.setstate(state)
    assert [s.next32() for _ in range(4)] == later
    assert all(isinstance(v, int) for v in state)


def test_ranges_and_errors():
    r = Random(3)
    assert r.randint(5, 5) == 5
    for _ in range(200):
        v = r.randrange(3, 30, 3)
        assert 3 <= v < 30 and v % 3 == 0
        f = r.random()
        assert 0.0 <= f < 1.0
        assert 1 <= r.uniform(1, 2) <= 2
    counts = [0] * 4
    for _ in range(4000):
        counts[r.randbelow(4)] += 1
    assert min(counts) > 850
    assert r.chance(0) is False and r.chance(100) is True
    support.raises(ValueError, r.randrange, 0)
    support.raises(ValueError, r.randint, 3, 2)
    support.raises(IndexError, r.choice, [])
    support.raises(ValueError, r.sample, [1, 2], 3)
    support.raises(TypeError, Random, "seed")
    assert r.choices(["x"], k=3) == ["x", "x", "x"]
    assert sorted(r.sample(range(5), 5)) == [0, 1, 2, 3, 4]
