"""Random numbers a computer player can replay: `self.random` in opense4.ai.

`Random(seed)` is PCG32 (O'Neill's permuted congruential generator, the XSH RR
variant) in plain whole-number arithmetic, so the same seed gives the same numbers
on the game's MicroPython and on CPython, on every computer. The engine gives each
request its own seed, from the game's seed, the turn, the empire and the call: a
request replayed gets the same numbers again.

The methods follow Python's `random` where they can (`randrange`, `randint`,
`choice`, `shuffle`, `sample`, `choices`, `random`, `uniform`, `getrandbits`), with
their own algorithms: the numbers are not those CPython's `random` would give.
"""

from __future__ import annotations

from typing import Any, List, MutableSequence, Optional, Sequence

_MASK64 = 0xFFFFFFFFFFFFFFFF
_MULTIPLIER = 6364136223846793005
_TWO53 = 9007199254740992.0


def _bit_length(n: int) -> int:
    # int.bit_length is not in MicroPython.
    bits = 0
    while n >= 0x10000:
        n >>= 16
        bits += 16
    while n:
        n >>= 1
        bits += 1
    return bits


class Random:
    """A PCG32 generator: `Random(seed, stream=0)`."""

    def __init__(self, seed: int = 0, stream: int = 0) -> None:
        self.seed(seed, stream)

    def seed(self, seed: int = 0, stream: int = 0) -> None:
        """Starts the sequence of `seed` (any whole number; taken modulo 2**64) in `stream`."""
        if not isinstance(seed, int) or isinstance(seed, bool):
            raise TypeError("the seed is a whole number")
        self._inc = ((stream & _MASK64) << 1 | 1) & _MASK64
        self._state = 0
        self._next32()
        self._state = (self._state + (seed & _MASK64)) & _MASK64
        self._next32()

    def getstate(self) -> List[int]:
        """The generator's state, as plain numbers (for a player's memory)."""
        return [self._state, self._inc]

    def setstate(self, state: Sequence[int]) -> None:
        """Continues from a state getstate() gave."""
        self._state = state[0] & _MASK64
        self._inc = (state[1] | 1) & _MASK64

    def _next32(self) -> int:
        old = self._state
        self._state = (old * _MULTIPLIER + self._inc) & _MASK64
        xorshifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rot = old >> 59
        return ((xorshifted >> rot) | (xorshifted << ((32 - rot) & 31))) & 0xFFFFFFFF

    def next32(self) -> int:
        """The next 32 random bits, 0 to 2**32 - 1."""
        return self._next32()

    def getrandbits(self, k: int) -> int:
        """A whole number of `k` random bits."""
        if k < 0:
            raise ValueError("the number of bits cannot be negative")
        out = 0
        got = 0
        while got < k:
            out = (out << 32) | self._next32()
            got += 32
        return out >> (got - k)

    def _below(self, n: int) -> int:
        # Uniform in [0, n), without bias: draws that would favour low numbers are drawn again.
        if n <= 0:
            raise ValueError("empty range")
        if n <= 0x100000000:
            threshold = (0x100000000 - n) % n
            while True:
                r = self._next32()
                if r >= threshold:
                    return r % n
        bits = _bit_length(n - 1)
        while True:
            r = self.getrandbits(bits)
            if r < n:
                return r

    def randbelow(self, n: int) -> int:
        """A whole number from 0 to n - 1."""
        return self._below(n)

    def randrange(self, start: int, stop: Optional[int] = None, step: int = 1) -> int:
        """A whole number from range(start, stop, step)."""
        if stop is None:
            return self._below(start)
        if step == 1:
            return start + self._below(stop - start)
        if step == 0:
            raise ValueError("a step of 0")
        n = (stop - start + step - 1) // step if step > 0 else (stop - start + step + 1) // step
        if n <= 0:
            raise ValueError("empty range")
        return start + step * self._below(n)

    def randint(self, a: int, b: int) -> int:
        """A whole number from a to b, both included."""
        return a + self._below(b - a + 1)

    def chance(self, percent: int) -> bool:
        """True with this chance in percent (0 never, 100 always)."""
        return self._below(100) < percent

    def choice(self, seq: Sequence[Any]) -> Any:
        """One element of a non-empty sequence."""
        if not seq:
            raise IndexError("cannot choose from an empty sequence")
        return seq[self._below(len(seq))]

    def shuffle(self, x: MutableSequence[Any]) -> None:
        """Shuffles a list in place."""
        for i in range(len(x) - 1, 0, -1):
            j = self._below(i + 1)
            x[i], x[j] = x[j], x[i]

    def sample(self, population: Sequence[Any], k: int) -> List[Any]:
        """`k` different elements of the population, in a random order."""
        pool = list(population)
        if k < 0 or k > len(pool):
            raise ValueError("the sample is larger than the population or negative")
        for i in range(k):
            j = i + self._below(len(pool) - i)
            pool[i], pool[j] = pool[j], pool[i]
        return pool[:k]

    def choices(self, population: Sequence[Any], weights: Optional[Sequence[Any]] = None, k: int = 1) -> List[Any]:
        """`k` elements chosen with replacement, by whole-number (or float) weights."""
        n = len(population)
        if n == 0:
            raise IndexError("cannot choose from an empty population")
        if weights is None:
            return [population[self._below(n)] for _ in range(k)]
        if len(weights) != n:
            raise ValueError("the number of weights does not match the population")
        totals = []
        total = 0
        whole = True
        for w in weights:
            if not isinstance(w, int) or isinstance(w, bool):
                whole = False
            total += w
            totals.append(total)
        if total <= 0:
            raise ValueError("the weights add up to nothing")
        out = []
        for _ in range(k):
            r = self._below(total) if whole else self.random() * total
            lo, hi = 0, n - 1
            while lo < hi:
                mid = (lo + hi) // 2
                if r < totals[mid]:
                    hi = mid
                else:
                    lo = mid + 1
            out.append(population[lo])
        return out

    def random(self) -> float:
        """A float in [0, 1), from 53 random bits."""
        a = self._next32() >> 5
        b = self._next32() >> 6
        return (a * 67108864 + b) / _TWO53

    def uniform(self, a: float, b: float) -> float:
        """A float between a and b."""
        return a + (b - a) * self.random()
