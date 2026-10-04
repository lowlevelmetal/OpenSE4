#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace opense4 {

// Deterministic PRNG (xoshiro256**). All game-state randomness must go through
// this type so that a seed fully determines a game (replays, multiplayer sync).
// Integer helpers never touch floating point, so they are bit-identical across
// compilers and platforms.
class Rng {
public:
    Rng() : Rng(0x5eed5eed5eed5eedull) {}
    explicit Rng(uint64_t seed) { reseed(seed); }

    void reseed(uint64_t seed) {
        uint64_t sm = seed;
        for (auto& s : state_) s = splitmix64(sm);
    }

    uint64_t next() {
        const uint64_t result = rotl(state_[1] * 5, 7) * 9;
        const uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return result;
    }

    // Uniform integer in [0, bound). Unbiased: rejects the short final bucket.
    uint64_t below(uint64_t bound) {
        if (bound <= 1) return 0;
        const uint64_t threshold = (0 - bound) % bound;
        for (;;) {
            const uint64_t r = next();
            if (r >= threshold) return r % bound;
        }
    }

    // A uniform index into `size` elements: below(size) as a size_t (32 bits on
    // armhf), the same number on every machine.
    size_t index(size_t size) { return static_cast<size_t>(below(size)); }

    // Uniform integer in [lo, hi] (inclusive).
    int64_t range(int64_t lo, int64_t hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int64_t>(below(static_cast<uint64_t>(hi - lo) + 1));
    }
    int rangeInt(int lo, int hi) { return static_cast<int>(range(lo, hi)); }

    // True with probability percent/100.
    bool percent(int pct) { return static_cast<int>(below(100)) < pct; }

    template <class T>
    T& pick(std::span<T> items) { return items[index(items.size())]; }

    template <class Container>
    void shuffle(Container& c) {
        for (size_t i = c.size(); i > 1; --i) {
            const size_t j = index(i);
            using std::swap;
            swap(c[i - 1], c[j]);
        }
    }

    // Derive an independent stream (e.g. one per subsystem) from this one.
    Rng fork() { return Rng(next()); }

    const uint64_t* rawState() const { return state_; }
    void setRawState(const uint64_t (&state)[4]) {
        for (int i = 0; i < 4; ++i) state_[i] = state[i];
    }
    bool operator==(const Rng& o) const {
        for (int i = 0; i < 4; ++i)
            if (state_[i] != o.state_[i]) return false;
        return true;
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static uint64_t splitmix64(uint64_t& x) {
        uint64_t z = (x += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    uint64_t state_[4]{};
};

} // namespace opense4
