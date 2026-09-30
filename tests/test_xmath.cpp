// Extended-precision arithmetic (src/game/xmath.hpp; docs/spec/02 "Arithmetic",
// docs/spec/03 Conventions, docs/spec/05 §0).

#include "game/xmath.hpp"

#include "core/rng.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::game::xmath;

namespace {

constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
constexpr int64_t kMin = std::numeric_limits<int64_t>::min();

// The spec examples, checked at compile time too.
static_assert(pctTrunc(100, 53) == 52);
static_assert(pctTrunc(90, 130) == 116);
static_assert(pctTrunc(300, 21) == 62);
static_assert(pctRound(5, 50) == 2);

// 2^k as an Ext, for 0 <= k < 63.
Ext pow2(int k) { return Ext(int64_t{1} << k); }

} // namespace

TEST_CASE("xmath: the spec's truncated percentages") {
    CHECK(pctTrunc(100, 53) == 52);
    CHECK(pctTrunc(90, 130) == 116);
    CHECK(pctTrunc(300, 21) == 62);
    // Rounding the same products gives the whole number.
    CHECK(pctRound(100, 53) == 53);
    CHECK(pctRound(90, 130) == 117);
    CHECK(pctRound(300, 21) == 63);
    // A product that is not whole truncates normally.
    CHECK(pctTrunc(784, 140) == 1097);
    // Negative amounts truncate toward zero, symmetrically.
    CHECK(pctTrunc(-300, 21) == -62);
    CHECK(pctRound(-300, 21) == -63);
}

TEST_CASE("xmath: exact factors") {
    // 50 %, 25 %, 100 % and 200 % are exact in binary, so nothing is lost.
    for (int64_t x = -1000; x <= 1000; ++x) {
        CHECK(pctTrunc(x, 50) == x / 2);
        CHECK(pctRound(x, 50) == divRoundHalfEven(x, 2));
        CHECK(pctTrunc(x, 25) == x / 4);
        CHECK(pctTrunc(x, 100) == x);
        CHECK(pctTrunc(x, 200) == 2 * x);
        CHECK(pctTrunc(x, 0) == 0);
    }
    CHECK(pctTrunc(kMax, 100) == kMax);
    CHECK(pctTrunc(kMin, 100) == kMin);
}

TEST_CASE("xmath: halves round to even") {
    CHECK(pctRound(5, 50) == 2);   // 2.5
    CHECK(pctRound(7, 50) == 4);   // 3.5
    CHECK(pctRound(-5, 50) == -2); // -2.5
    CHECK(pctRound(-7, 50) == -4); // -3.5
    CHECK(pctRound(1, 50) == 0);   // 0.5
    CHECK(pctRound(3, 50) == 2);   // 1.5
    CHECK((Ext(5) / Ext(2)).round() == 2);
    CHECK((Ext(-5) / Ext(2)).round() == -2);
    CHECK((Ext(-5) / Ext(2)).trunc() == -2);
    CHECK((Ext(7) / Ext(-2)).round() == -4);
    CHECK((Ext(1) / Ext(3)).round() == 0);
    CHECK((Ext(2) / Ext(3)).round() == 1);
    CHECK((Ext(-2) / Ext(3)).round() == -1);
    CHECK((Ext(-2) / Ext(3)).trunc() == 0);
}

TEST_CASE("xmath: exact integer division rounded half to even") {
    CHECK(divRoundHalfEven(5, 2) == 2);
    CHECK(divRoundHalfEven(7, 2) == 4);
    CHECK(divRoundHalfEven(-5, 2) == -2);
    CHECK(divRoundHalfEven(5, -2) == -2);
    CHECK(divRoundHalfEven(-7, -2) == 4);
    CHECK(divRoundHalfEven(1, 3) == 0);
    CHECK(divRoundHalfEven(2, 3) == 1);
    CHECK(divRoundHalfEven(-2, 3) == -1);
    CHECK(divRoundHalfEven(0, 5) == 0);
    CHECK(divRoundHalfEven(10, 5) == 2);
    CHECK(divRoundHalfEven(kMax, 2) == kMax / 2 + 1); // odd half: 2^62 − 0.5 goes to 2^62
    CHECK(divRoundHalfEven(kMin, 2) == kMin / 2);
    CHECK(divRoundHalfEven(kMin, -1) == kMax); // saturates
    CHECK(divRoundHalfEven(kMax, -1) == -kMax);
    CHECK(divRoundHalfEven(12, 0) == 0);

    // It matches the floating-point quotient rounded or truncated, for any int64.
    Rng rng(11);
    int mismatches = 0;
    for (int i = 0; i < 20000; ++i) {
        const int64_t a = static_cast<int64_t>(rng.next() >> (1 + rng.below(63))) * (rng.below(2) ? 1 : -1);
        int64_t b = static_cast<int64_t>(rng.next() >> (1 + rng.below(63))) * (rng.below(2) ? 1 : -1);
        if (i % 4 == 0) b = rng.range(-9, 9);
        if (b == 0 || (a == kMin && b == -1)) continue;
        const Ext q = Ext(a) / Ext(b);
        if (q.round() != divRoundHalfEven(a, b) || q.trunc() != a / b) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("xmath: integers, signs, zero and ordering") {
    for (const int64_t v : {int64_t{0}, int64_t{1}, int64_t{-1}, int64_t{12345}, int64_t{-98765}, kMax, kMin, kMax - 1}) {
        CHECK(Ext(v).trunc() == v);
        CHECK(Ext(v).round() == v);
        CHECK((Ext(v) - Ext(v)).isZero());
        CHECK((Ext(v) - Ext(v)) == Ext());
        CHECK((Ext(v) * Ext(1)) == Ext(v));
    }
    CHECK(Ext(0) == Ext());
    CHECK(-Ext(0) == Ext());
    CHECK_FALSE(Ext(0).isNegative());
    CHECK(Ext(-3).isNegative());
    CHECK(Ext(1).significand() == uint64_t{1} << 63);
    CHECK(Ext(1).exponent() == -63);
    CHECK(Ext(6).significand() == uint64_t{3} << 62);
    CHECK(Ext(6).exponent() == -61);

    CHECK(Ext(2) + Ext(3) == Ext(5));
    CHECK(Ext(2) - Ext(3) == Ext(-1));
    CHECK(Ext(-2) - Ext(-3) == Ext(1));
    CHECK(Ext(-4) * Ext(3) == Ext(-12));
    CHECK(Ext(-12) / Ext(-3) == Ext(4));
    CHECK(Ext(7) / Ext(0) == Ext()); // documented: zero
    Ext acc(10);
    acc += Ext(5);
    acc *= Ext(3);
    acc -= Ext(1);
    acc /= Ext(2);
    CHECK(acc == Ext(22));

    CHECK(Ext(-5) < Ext(-4));
    CHECK(Ext(-1) < Ext(0));
    CHECK(Ext(0) < Ext(1));
    CHECK(Ext(3) > Ext(2));
    CHECK(Ext(1) / Ext(3) < Ext(1) / Ext(2));
    CHECK(-(Ext(1) / Ext(3)) > -(Ext(1) / Ext(2)));
    CHECK(Ext(kMin) < Ext(kMax));
    CHECK(Ext(5) >= Ext(5));
    CHECK((Ext(5) <=> Ext(5)) == std::strong_ordering::equal);

    // "Compared in floating point": 300 × 21 % is just below 63.
    CHECK(Ext(300) * percent(21) < Ext(63));
    CHECK(Ext(300) * percent(21) > Ext(62));
}

TEST_CASE("xmath: adding values of very different sizes") {
    const Ext third = Ext(1) / Ext(3);
    const Ext big = pow2(62) * Ext(2); // 2^63: from here up the gap between values is 1, just below it 1/2
    CHECK(big + third == big);                                 // less than half a gap: rounds down
    CHECK(big + Ext(2) / Ext(3) == big + Ext(1));              // more than half: rounds up
    CHECK(big + Ext(1) / Ext(2) == big);                       // a tie: to the even neighbour
    CHECK(big + Ext(3) / Ext(2) == big + Ext(2));              // a tie: to the even neighbour
    CHECK(big - third == big - Ext(1) / Ext(2));               // below 2^63 the gap is 1/2
    CHECK((big - third).trunc() == kMax);
    CHECK(((big + third) - big).isZero());
    // A tie broken by bits far below: 2^63 + (1/2 + 2^-60) rounds up.
    const Ext justOverHalf = Ext(1) / Ext(2) + Ext(1) / pow2(30) / pow2(30);
    CHECK(big + justOverHalf == big + Ext(1));
    CHECK(big - justOverHalf == big - Ext(1) / Ext(2));
    // 2^63 − (1/4 + 2^-64): the bits shifted out make the result just below a
    // tie, so it rounds down to 2^63 − 1/2, not up to 2^63.
    const Ext quarterAndABit = Ext(1) / Ext(4) + Ext(1) / pow2(32) / pow2(32);
    CHECK(big - quarterAndABit == big - Ext(1) / Ext(2));
    CHECK(big + quarterAndABit == big);
    // 1 ± 2^-70 rounds back to 1: the gap is 2^-64 below 1 and 2^-63 above.
    Ext tiny = Ext(1);
    for (int i = 0; i < 7; ++i) tiny /= pow2(10);
    CHECK(Ext(1) - tiny == Ext(1));
    CHECK(Ext(1) + tiny == Ext(1));
    CHECK((Ext(1) - tiny) - Ext(1) == Ext());
    // Just over half a gap below 1 rounds down to the next value, 1 − 2^-64.
    const Ext below = Ext(1) - (pow2(1) / pow2(62) / pow2(4) + tiny); // 1 − (2^-65 + 2^-70)
    CHECK(below < Ext(1));
    CHECK(below == Ext(1) - Ext(1) / pow2(32) / pow2(32));
}

TEST_CASE("xmath: rounding the significand to a narrower width") {
    const int64_t two53 = int64_t{1} << 53;
    CHECK(Ext(two53 + 1).roundedTo(kDoubleBits) == Ext(two53));     // tie, to even
    CHECK(Ext(two53 + 3).roundedTo(kDoubleBits) == Ext(two53 + 4)); // tie, to even
    CHECK(Ext(two53 + 2).roundedTo(kDoubleBits) == Ext(two53 + 2)); // exact
    CHECK(Ext(-(two53 + 3)).roundedTo(kDoubleBits) == Ext(-(two53 + 4)));
    CHECK(Ext(kMax).roundedTo(kDoubleBits) == pow2(62) * Ext(2)); // carries into the next power of two
    CHECK(Ext(12345).roundedTo(64) == Ext(12345));
    CHECK(Ext(16777217).roundedTo(kSingleBits) == Ext(16777216));
    CHECK(Ext(7).roundedTo(1) == Ext(8));
    CHECK(Ext().roundedTo(kDoubleBits) == Ext());
    // 1/3 in double precision is below the extended 1/3 (its dropped bits are 0101...).
    const Ext third = Ext(1) / Ext(3);
    CHECK(third.roundedTo(kDoubleBits) < third);
    CHECK((third.roundedTo(kDoubleBits).significand() & 0x7ff) == 0);
}

TEST_CASE("xmath: out-of-range values are defined") {
    CHECK((Ext(kMax) * Ext(kMax)).trunc() == kMax);
    CHECK((Ext(kMax) * Ext(kMin)).round() == kMin);
    CHECK((Ext(kMin) / Ext(1)).trunc() == kMin);
    CHECK((Ext(kMax) + Ext(kMax)).trunc() == kMax);
    // Growing without bound saturates; shrinking flushes to zero.
    Ext big(kMax);
    for (int i = 0; i < 400; ++i) big *= Ext(kMax);
    CHECK(big > Ext(kMax));
    CHECK(big.trunc() == kMax);
    CHECK((big * Ext(-1)).round() == kMin);
    Ext small(1);
    for (int i = 0; i < 400; ++i) small /= Ext(kMax);
    CHECK(small.isZero());
    CHECK(small.trunc() == 0);
}

#if defined(__SIZEOF_INT128__)

namespace {

// An independent reference: exact rational arithmetic in 128-bit integers, with
// the documented rounding steps applied by comparing remainders.
__extension__ typedef unsigned __int128 u128;

int bitLength(u128 v) {
    const auto hi = static_cast<uint64_t>(v >> 64);
    return hi != 0 ? 64 + static_cast<int>(std::bit_width(hi)) : static_cast<int>(std::bit_width(static_cast<uint64_t>(v)));
}

struct RefFloat {
    uint64_t m = 0; // 0, or in [2^63, 2^64)
    int e = 0;      // value = m × 2^e
};

// n / d (n ≥ 0, d > 0) rounded to 64 significant bits, to nearest, ties to even.
RefFloat roundRational(u128 n, u128 d) {
    if (n == 0) return {};
    const u128 lowest = u128{1} << 63, limit = u128{1} << 64;
    int e = bitLength(n) - bitLength(d) - 64;
    for (;;) {
        // q = floor(n / (d × 2^e)), with remainder r over den.
        const u128 num = e < 0 ? n << -e : n;
        const u128 den = e < 0 ? d : d << e;
        u128 q = num / den;
        if (q >= limit) {
            ++e;
            continue;
        }
        if (q < lowest) {
            --e;
            continue;
        }
        const u128 r = num - q * den;
        if (2 * r > den || (2 * r == den && (q & 1) != 0)) ++q;
        if (q == limit) return {uint64_t{1} << 63, e + 1};
        return {static_cast<uint64_t>(q), e};
    }
}

// m × 2^e to an integer: truncated, and rounded to nearest-even.
void toIntegers(RefFloat v, int64_t& truncated, int64_t& rounded) {
    if (v.m == 0 || v.e <= -128) {
        truncated = rounded = 0;
        return;
    }
    if (v.e >= 0) {
        truncated = rounded = static_cast<int64_t>(u128{v.m} << v.e);
        return;
    }
    const u128 den = u128{1} << -v.e;
    const u128 q = v.m / den, r = v.m % den;
    truncated = static_cast<int64_t>(q);
    rounded = static_cast<int64_t>(q + ((2 * r > den || (2 * r == den && (q & 1) != 0)) ? 1 : 0));
}

} // namespace

TEST_CASE("xmath: percentages against an exact rational reference") {
    // Every p in 0..1000; every x in 0..2000, plus spread-out x up to 100000.
    std::vector<int64_t> xs;
    for (int64_t x = 0; x <= 2000; ++x) xs.push_back(x);
    Rng rng(4242);
    for (int i = 0; i < 400; ++i) xs.push_back(rng.range(2001, 100000));
    xs.push_back(100000);

    int64_t mismatches = 0, lowered = 0, checked = 0;
    std::string first;
    for (int64_t p = 0; p <= 1000; ++p) {
        const RefFloat factor = roundRational(static_cast<u128>(p), 100);
        for (const int64_t x : xs) {
            RefFloat product{};
            if (factor.m != 0 && x != 0) {
                product = roundRational(static_cast<u128>(x) * factor.m, 1);
                product.e += factor.e;
            }
            int64_t wantTrunc = 0, wantRound = 0;
            toIntegers(product, wantTrunc, wantRound);
            const int64_t gotTrunc = pctTrunc(x, p), gotRound = pctRound(x, p);
            ++checked;
            // Negative amounts mirror positive ones (checked on a subset, for speed).
            const bool mirrored = x > 500 || (pctTrunc(-x, p) == -wantTrunc && pctRound(-x, p) == -wantRound);
            if (gotTrunc != wantTrunc || gotRound != wantRound || !mirrored) {
                if (mismatches++ == 0)
                    first = std::to_string(x) + " x " + std::to_string(p) + "%: trunc " + std::to_string(gotTrunc) +
                            " want " + std::to_string(wantTrunc) + ", round " + std::to_string(gotRound) + " want " +
                            std::to_string(wantRound);
            }
            // The only deviation from exact arithmetic: a whole product one lower.
            const int64_t exact = x * p / 100;
            if (wantTrunc != exact) {
                CHECK((x * p % 100 == 0 && wantTrunc == exact - 1));
                ++lowered;
            }
        }
    }
    INFO(first);
    CHECK(mismatches == 0);
    CHECK(checked > 2000000);
    CHECK(lowered > 1000); // the case the rule is about does occur, often
}

TEST_CASE("xmath: chained operations against an exact rational reference") {
    // round(P × ((R / 100) / 10)): three roundings, then to the integer.
    Rng rng(99);
    int mismatches = 0;
    for (int i = 0; i < 20000; ++i) {
        const int64_t pop = rng.range(1, 5000000), rate = rng.range(0, 100);
        const RefFloat r100 = roundRational(static_cast<u128>(rate), 100);
        int64_t want = 0, ignored = 0;
        if (r100.m != 0) {
            // (m × 2^e) / 10 = (m / 10) × 2^e
            RefFloat r10 = roundRational(r100.m, 10);
            r10.e += r100.e;
            RefFloat prod = roundRational(static_cast<u128>(pop) * r10.m, 1);
            prod.e += r10.e;
            toIntegers(prod, ignored, want);
        }
        if ((Ext(pop) * (percent(rate) / Ext(10))).round() != want) ++mismatches;
    }
    CHECK(mismatches == 0);
}

#endif // __SIZEOF_INT128__

#if (defined(__x86_64__) || defined(__i386__)) && LDBL_MANT_DIG == 64

// On x86 with GCC or Clang, long double is the x87 extended format itself, in
// its default precision and rounding mode. This is a check of the emulation
// only; long double never appears in src/.
namespace {

struct Both {
    Ext x;
    long double y = 0;
};

bool sameBits(const Ext& x, long double y) {
    if (y == 0) return x.isZero();
    if (x.isZero() || x.isNegative() != (y < 0)) return false;
    int e = 0;
    const long double f = std::frexp(std::fabs(y), &e); // in [0.5, 1)
    const auto m = static_cast<uint64_t>(std::ldexp(f, 64));
    return x.significand() == m && x.exponent() == e - 64;
}

// Multiplies both by 2^k, exactly.
void scale(Both& v, int k) {
    while (k != 0) {
        const int step = k > 0 ? std::min(k, 60) : std::max(k, -60);
        const int64_t factor = int64_t{1} << (step > 0 ? step : -step);
        if (step > 0) {
            v.x *= Ext(factor);
            v.y *= static_cast<long double>(factor);
        } else {
            v.x /= Ext(factor);
            v.y /= static_cast<long double>(factor);
        }
        k -= step;
    }
}

// A value at a random binary scale, with a full 64-bit significand (a
// quotient), a short one (an integer), or a sparse one (2^i ± 2^j, which makes
// the exact ties and near-ties that rounding must get right).
Both randomValue(Rng& rng) {
    const auto mag = [&] {
        const int64_t v = static_cast<int64_t>(rng.next() >> (1 + rng.below(63)));
        return rng.below(2) ? v : -v;
    };
    Both v;
    switch (rng.below(3)) {
    case 0: {
        const int64_t a = mag();
        int64_t b = mag();
        if (b == 0) b = 1;
        v = {Ext(a) / Ext(b), static_cast<long double>(a) / static_cast<long double>(b)};
        break;
    }
    case 1: {
        const int64_t a = mag();
        v = {Ext(a), static_cast<long double>(a)};
        break;
    }
    default: {
        const int i = static_cast<int>(rng.below(62)), j = static_cast<int>(rng.below(62));
        const int64_t a = (int64_t{1} << i) + (rng.below(2) ? int64_t{1} << j : -(int64_t{1} << j));
        v = {Ext(a), static_cast<long double>(a)};
        break;
    }
    }
    scale(v, static_cast<int>(rng.range(-100, 100)));
    return v;
}

} // namespace

TEST_CASE("xmath: every operation matches the x87 extended format") {
    Rng rng(2026);
    int mismatches = 0;
    std::string first;
    const auto check = [&](bool ok, const char* what, int i) {
        if (ok) return;
        if (mismatches++ == 0) first = std::string(what) + " at sample " + std::to_string(i);
    };
    for (int i = 0; i < 200000; ++i) {
        const Both a = randomValue(rng), b = randomValue(rng);
        check(sameBits(a.x, a.y), "operand", i);
        check(sameBits(a.x + b.x, a.y + b.y), "+", i);
        check(sameBits(a.x - b.x, a.y - b.y), "-", i);
        check(sameBits(a.x * b.x, a.y * b.y), "*", i);
        if (!b.x.isZero()) check(sameBits(a.x / b.x, a.y / b.y), "/", i);
        check((a.x < b.x) == (a.y < b.y) && (a.x == b.x) == (a.y == b.y), "compare", i);
        check(sameBits(a.x.roundedTo(kDoubleBits), static_cast<long double>(static_cast<double>(a.y))), "double", i);
        if (std::fabs(a.y) > 1.0e-37L && std::fabs(a.y) < 1.0e38L) // float's normal range
            check(sameBits(a.x.roundedTo(kSingleBits), static_cast<long double>(static_cast<float>(a.y))), "float", i);
        if (std::fabs(a.y) < 9.0e18L) {
            check(a.x.trunc() == static_cast<int64_t>(a.y), "trunc", i);
            check(a.x.round() == std::llrint(a.y), "round", i);
        }
    }
    INFO(first);
    CHECK(mismatches == 0);
}

TEST_CASE("xmath: sums near a tie match the x87 extended format") {
    // a near 1, and b = 2^p + 2^q placed 60 to 66 binary places below it: the
    // region where the smaller operand's last bits are shifted out and decide
    // the rounding.
    std::vector<Both> as;
    for (const int64_t m : {int64_t{1} << 62, (int64_t{1} << 62) + 1, kMax, (int64_t{3} << 60) + 7}) {
        Both a{Ext(m), static_cast<long double>(m)};
        scale(a, -62);
        as.push_back(a);
    }
    int mismatches = 0;
    for (int p = 0; p <= 62; ++p)
        for (int q = 0; q <= p; ++q)
            for (int shift = 60; shift <= 66; ++shift) {
                const int64_t m = (int64_t{1} << p) + (q < p ? int64_t{1} << q : 0);
                Both b{Ext(m), static_cast<long double>(m)};
                scale(b, -p - shift);
                for (const Both& a : as)
                    if (!sameBits(a.x + b.x, a.y + b.y) || !sameBits(a.x - b.x, a.y - b.y) ||
                        !sameBits(b.x - a.x, b.y - a.y))
                        ++mismatches;
            }
    CHECK(mismatches == 0);
}

TEST_CASE("xmath: percentages match the x87 extended format") {
    Rng rng(7);
    int mismatches = 0;
    for (int i = 0; i < 300000; ++i) {
        const int64_t x = rng.range(-100000, 100000), p = rng.range(-200, 1000);
        const long double y = static_cast<long double>(x) * (static_cast<long double>(p) / 100.0L);
        if (pctTrunc(x, p) != static_cast<int64_t>(y) || pctRound(x, p) != std::llrint(y)) ++mismatches;
    }
    CHECK(mismatches == 0);
}

#endif // x87 long double
