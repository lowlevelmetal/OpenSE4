// Extended-precision arithmetic (src/game/xmath.hpp; docs/spec/02 "Arithmetic",
// docs/spec/03 Conventions, docs/spec/05 §0).

#include "game/xmath.hpp"

#include "core/hash.hpp"
#include "core/rng.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
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

TEST_CASE("xmath: doubles kept as bit patterns") {
    // The rules never use host floating point; the test may, to name doubles.
    for (const double d : {0.0, 0.1, 0.3, 0.5, 1.0, 1.3, 1.5, 123456.789, -2.5, 1e-300, 1e300}) {
        const auto bits = std::bit_cast<uint64_t>(d);
        CHECK(toDoubleBits(fromDoubleBits(bits)) == bits);
    }
    CHECK(fromDoubleBits(std::bit_cast<uint64_t>(0.5)) == Ext(1) / Ext(2));
    CHECK(fromDoubleBits(std::bit_cast<uint64_t>(-3.0)) == Ext(-3));
    // Storing an extended value keeps the nearest double.
    CHECK(toDoubleBits(Ext(1) / Ext(10)) == std::bit_cast<uint64_t>(0.1));
    CHECK(toDoubleBits(Ext(13) / Ext(10)) == std::bit_cast<uint64_t>(1.3));
    CHECK(toDoubleBits(Ext(1) / Ext(10) + Ext(1) / Ext(2)) == std::bit_cast<uint64_t>(0.6));
    CHECK(toDoubleBits(Ext(1) / Ext(3)) == std::bit_cast<uint64_t>(1.0 / 3.0));
    // The double nearest 0.3 lies below the extended 0.3.
    CHECK(fromDoubleBits(std::bit_cast<uint64_t>(0.3)) < Ext(3) / Ext(10));
    CHECK(fromDoubleBits(std::bit_cast<uint64_t>(1.3)) > Ext(13) / Ext(10));
    // fromParts: ±significand × 2^exponent.
    CHECK(Ext::fromParts(false, 3, -1) == Ext(3) / Ext(2));
    CHECK(Ext::fromParts(true, 5, 2) == Ext(-20));
    CHECK(Ext::fromParts(false, 0, 7).isZero());
}

// ---- Exact references ---------------------------------------------------------------------------
//
// The tests below compare Ext with exact arithmetic. It is done in unsigned
// integers wider than 64 bits, written out here, with no __int128 and no x87
// long double, so that the tests run with every compiler: Windows players
// must get the same games as Linux players. On x86 with GCC or Clang, where
// long double is the x87 extended format itself, they also compare with the
// hardware. Golden checksums of the results tie every compiler to the runs
// that were checked against the x87.

namespace {

// The 128-bit product of two words, as hi × 2^64 + lo.
void multiplyWords(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
    constexpr uint64_t kLow = 0xffffffffu;
    const uint64_t p00 = (a & kLow) * (b & kLow), p01 = (a & kLow) * (b >> 32);
    const uint64_t p10 = (a >> 32) * (b & kLow), p11 = (a >> 32) * (b >> 32);
    const uint64_t mid = (p00 >> 32) + (p01 & kLow) + (p10 & kLow);
    lo = (mid << 32) | (p00 & kLow);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// (hi × 2^64 + lo) / d and its remainder, for hi < d: long division in 32-bit
// digits (Knuth's algorithm D for two words, as Hacker's Delight writes it).
uint64_t divideWords(uint64_t hi, uint64_t lo, uint64_t d, uint64_t& rem) {
    constexpr uint64_t kBase = uint64_t{1} << 32, kLow = kBase - 1;
    const int s = std::countl_zero(d);
    d <<= s;
    const uint64_t d1 = d >> 32, d0 = d & kLow;
    const uint64_t top = s == 0 ? hi : (hi << s) | (lo >> (64 - s));
    const uint64_t low = lo << s;
    const uint64_t n1 = low >> 32, n0 = low & kLow;
    uint64_t q1 = top / d1, r = top - q1 * d1;
    while (q1 >= kBase || q1 * d0 > ((r << 32) | n1)) {
        --q1;
        r += d1;
        if (r >= kBase) break;
    }
    const uint64_t mid = (top << 32) + n1 - q1 * d; // modulo 2^64: the true value is below d
    uint64_t q0 = mid / d1;
    r = mid - q0 * d1;
    while (q0 >= kBase || q0 * d0 > ((r << 32) | n0)) {
        --q0;
        r += d1;
        if (r >= kBase) break;
    }
    rem = ((mid << 32) + n0 - q0 * d) >> s;
    return (q1 << 32) + q0;
}

// An unsigned integer of N 64-bit words, the lowest first, with the operations
// the references need. Like the built-in unsigned types, it wraps modulo
// 2^(64 N). It converts from any non-negative integer.
template <size_t N>
struct Wide {
    std::array<uint64_t, N> w{};

    Wide() = default;
    template <std::integral T>
    Wide(T v) { w[0] = static_cast<uint64_t>(v); }
    template <std::integral T>
    explicit operator T() const { return static_cast<T>(w[0]); } // the lowest word

    int bitLength() const {
        for (size_t i = N; i-- > 0;)
            if (w[i] != 0) return static_cast<int>(64 * i) + static_cast<int>(std::bit_width(w[i]));
        return 0;
    }
    bool bit(int i) const { return ((w[static_cast<size_t>(i / 64)] >> (i % 64)) & 1) != 0; }
    bool odd() const { return (w[0] & 1) != 0; }

    friend bool operator==(const Wide&, const Wide&) = default;
    friend std::strong_ordering operator<=>(const Wide& a, const Wide& b) {
        for (size_t i = N; i-- > 0;)
            if (a.w[i] != b.w[i]) return a.w[i] <=> b.w[i];
        return std::strong_ordering::equal;
    }

    friend Wide operator+(const Wide& a, const Wide& b) {
        Wide r;
        uint64_t carry = 0;
        for (size_t i = 0; i < N; ++i) {
            const uint64_t s = a.w[i] + b.w[i], t = s + carry;
            carry = (s < a.w[i] ? 1u : 0u) + (t < s ? 1u : 0u);
            r.w[i] = t;
        }
        return r;
    }
    friend Wide operator-(const Wide& a, const Wide& b) {
        Wide r;
        uint64_t borrow = 0;
        for (size_t i = 0; i < N; ++i) {
            const uint64_t d = a.w[i] - b.w[i], t = d - borrow;
            borrow = (a.w[i] < b.w[i] ? 1u : 0u) + (d < borrow ? 1u : 0u);
            r.w[i] = t;
        }
        return r;
    }
    friend Wide operator*(const Wide& a, const Wide& b) {
        Wide r;
        for (size_t i = 0; i < N; ++i) {
            uint64_t carry = 0;
            for (size_t j = 0; i + j < N; ++j) {
                uint64_t hi = 0, lo = 0;
                multiplyWords(a.w[i], b.w[j], hi, lo);
                lo += carry;
                hi += lo < carry ? 1u : 0u;
                r.w[i + j] += lo;
                hi += r.w[i + j] < lo ? 1u : 0u;
                carry = hi;
            }
        }
        return r;
    }
    friend Wide operator<<(const Wide& a, int n) {
        Wide r;
        if (n >= static_cast<int>(64 * N)) return r;
        const auto words = static_cast<size_t>(n / 64);
        const int bits = n % 64;
        for (size_t i = words; i < N; ++i) {
            r.w[i] = a.w[i - words] << bits;
            if (bits != 0 && i > words) r.w[i] |= a.w[i - words - 1] >> (64 - bits);
        }
        return r;
    }
    friend Wide operator>>(const Wide& a, int n) {
        Wide r;
        if (n >= static_cast<int>(64 * N)) return r;
        const auto words = static_cast<size_t>(n / 64);
        const int bits = n % 64;
        for (size_t i = 0; i + words < N; ++i) {
            r.w[i] = a.w[i + words] >> bits;
            if (bits != 0 && i + words + 1 < N) r.w[i] |= a.w[i + words + 1] << (64 - bits);
        }
        return r;
    }

    // q = a / b and r = a % b, for b ≠ 0.
    static void divide(const Wide& a, const Wide& b, Wide& q, Wide& r) {
        q = {};
        r = {};
        if (b.bitLength() <= 64) { // a word at a time
            uint64_t rem = 0;
            for (size_t i = N; i-- > 0;) q.w[i] = divideWords(rem, a.w[i], b.w[0], rem);
            r.w[0] = rem;
            return;
        }
        for (int i = a.bitLength() - 1; i >= 0; --i) { // a bit at a time
            const bool carry = r.bit(static_cast<int>(64 * N) - 1);
            r = r << 1;
            if (a.bit(i)) r.w[0] |= 1;
            if (carry || r >= b) {
                r = r - b;
                q.w[static_cast<size_t>(i / 64)] |= uint64_t{1} << (i % 64);
            }
        }
    }
    friend Wide operator/(const Wide& a, const Wide& b) {
        Wide q, r;
        divide(a, b, q, r);
        return q;
    }
    friend Wide operator%(const Wide& a, const Wide& b) {
        Wide q, r;
        divide(a, b, q, r);
        return r;
    }
};

using u128 = Wide<2>;

// A number as the reference computes it: ±m × 2^e, with m zero or in
// [2^63, 2^64), as in the x87 extended format.
struct RefFloat {
    uint64_t m = 0;
    int e = 0;
    bool neg = false;
};

// n / d (n ≥ 0, d > 0) rounded to 64 significant bits, to nearest, ties to
// even: the remainders are compared.
RefFloat roundRational(u128 n, u128 d) {
    if (n == 0) return {};
    const u128 lowest = u128{1} << 63, limit = u128{1} << 64;
    int e = n.bitLength() - d.bitLength() - 64;
    for (;;) {
        // q = floor(n / (d × 2^e)), with remainder r over den.
        const u128 num = e < 0 ? n << -e : n;
        const u128 den = e < 0 ? d : d << e;
        u128 q, r;
        u128::divide(num, den, q, r);
        if (q >= limit) {
            ++e;
            continue;
        }
        if (q < lowest) {
            --e;
            continue;
        }
        if (r + r > den || (r + r == den && q.odd())) q = q + 1;
        if (q == limit) return {uint64_t{1} << 63, e + 1};
        return {static_cast<uint64_t>(q), e};
    }
}

// ±n × 2^e rounded to `bits` significant bits (1 to 64), to nearest, ties to
// even: an exact result rounded once. The dropped bits are compared with half.
template <size_t N>
RefFloat roundExact(bool neg, Wide<N> n, int e, int bits = 64) {
    int length = n.bitLength();
    if (length == 0) return {};
    if (length > bits) {
        const int drop = length - bits;
        Wide<N> kept = n >> drop;
        const Wide<N> rest = n - (kept << drop), half = Wide<N>{1} << (drop - 1);
        if (rest > half || (rest == half && kept.odd())) kept = kept + 1;
        e += drop;
        if (kept.bitLength() > bits) { // carried into a new place: exactly 2^bits
            kept = kept >> 1;
            ++e;
        }
        n = kept;
        length = bits;
    }
    return {static_cast<uint64_t>(n << (64 - length)), e - (64 - length), neg};
}

RefFloat refExt(const Ext& x) { return {x.significand(), x.exponent(), x.isNegative()}; }

RefFloat refInt(int64_t v) {
    const uint64_t magnitude = v < 0 ? uint64_t{0} - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    return roundExact(v < 0, u128{magnitude}, 0);
}

RefFloat refNeg(RefFloat a) {
    if (a.m != 0) a.neg = !a.neg;
    return a;
}

// The exact sum, in 512 bits: enough for the operands below, which lie
// within 2^±240 of 1.
RefFloat refAdd(const RefFloat& a, const RefFloat& b) {
    if (a.m == 0) return b;
    if (b.m == 0) return a;
    using Exact = Wide<8>;
    const int e = std::min(a.e, b.e);
    if (std::max(a.e, b.e) - e > 64 * 7 - 2) throw std::out_of_range("refAdd: the operands are too far apart");
    const Exact x = Exact{a.m} << (a.e - e), y = Exact{b.m} << (b.e - e);
    if (a.neg == b.neg) return roundExact(a.neg, x + y, e);
    if (x == y) return {};
    return x > y ? roundExact(a.neg, x - y, e) : roundExact(b.neg, y - x, e);
}

RefFloat refMul(const RefFloat& a, const RefFloat& b) {
    if (a.m == 0 || b.m == 0) return {};
    return roundExact(a.neg != b.neg, u128{a.m} * u128{b.m}, a.e + b.e);
}

// b ≠ 0.
RefFloat refDiv(const RefFloat& a, const RefFloat& b) {
    if (a.m == 0) return {};
    RefFloat q = roundRational(a.m, b.m);
    q.e += a.e - b.e;
    q.neg = a.neg != b.neg;
    return q;
}

RefFloat refNarrow(const RefFloat& a, int bits) { return roundExact(a.neg, u128{a.m}, a.e, bits); }

// The sign of a − b, exactly: −1, 0 or 1.
int refCompare(const RefFloat& a, const RefFloat& b) {
    const RefFloat d = refAdd(a, refNeg(b));
    return d.m == 0 ? 0 : d.neg ? -1 : 1;
}

bool same(const Ext& x, const RefFloat& r) {
    if (r.m == 0) return x.isZero();
    return x.significand() == r.m && x.exponent() == r.e && x.isNegative() == r.neg;
}

// m × 2^e (|value| < 2^63) to an integer: truncated, and rounded to nearest-even.
void toIntegers(RefFloat v, int64_t& truncated, int64_t& rounded) {
    truncated = rounded = 0;
    if (v.m == 0 || v.e <= -128) return;
    if (v.e >= 0) {
        truncated = rounded = static_cast<int64_t>(u128{v.m} << v.e);
    } else {
        const u128 den = u128{1} << -v.e;
        u128 q, r;
        u128::divide(v.m, den, q, r);
        truncated = static_cast<int64_t>(q);
        rounded = static_cast<int64_t>(q + ((r + r > den || (r + r == den && q.odd())) ? 1 : 0));
    }
    if (v.neg) {
        truncated = -truncated;
        rounded = -rounded;
    }
}

// For the golden checksums of the results.
void hashExt(Hasher& h, const Ext& x) { h.add(x.significand()).add(static_cast<int32_t>(x.exponent())).add(x.isNegative()); }

// Multiplies by 2^k, exactly.
Ext scaled(Ext x, int k) {
    while (k != 0) {
        const int step = k > 0 ? std::min(k, 60) : std::max(k, -60);
        const Ext factor(int64_t{1} << (step > 0 ? step : -step));
        x = step > 0 ? x * factor : x / factor;
        k -= step;
    }
    return x;
}

// A value at a random binary scale, with a full 64-bit significand (a
// quotient), a short one (an integer), or a sparse one (2^i ± 2^j, which makes
// the exact ties and near-ties that rounding must get right). One draw per
// statement, so that every compiler makes the same values.
Ext randomValue(Rng& rng) {
    const auto magnitude = [&] {
        const uint64_t bits = rng.next();
        const auto shift = static_cast<int>(1 + rng.below(63));
        const auto v = static_cast<int64_t>(bits >> shift);
        return rng.below(2) ? v : -v;
    };
    Ext v;
    switch (rng.below(3)) {
    case 0: {
        const int64_t a = magnitude();
        int64_t b = magnitude();
        if (b == 0) b = 1;
        v = Ext(a) / Ext(b);
        break;
    }
    case 1:
        v = Ext(magnitude());
        break;
    default: {
        const auto i = static_cast<int>(rng.below(62));
        const auto j = static_cast<int>(rng.below(62));
        const int64_t low = rng.below(2) ? int64_t{1} << j : -(int64_t{1} << j);
        v = Ext((int64_t{1} << i) + low);
        break;
    }
    }
    return scaled(v, static_cast<int>(rng.range(-100, 100)));
}

} // namespace

#if (defined(__x86_64__) || defined(__i386__)) && LDBL_MANT_DIG == 64

// On x86 with GCC or Clang, long double is the x87 extended format itself, in
// its default precision and rounding mode. It is a check of the emulation
// only; long double never appears in src/.
#define OPENSE4_TEST_X87 1

namespace {

// Exact: the significand has 64 bits.
long double toX87(const Ext& x) {
    const long double magnitude = std::ldexp(static_cast<long double>(x.significand()), x.exponent());
    return x.isNegative() ? -magnitude : magnitude;
}

bool sameBits(const Ext& x, long double y) {
    if (y == 0) return x.isZero();
    if (x.isZero() || x.isNegative() != (y < 0)) return false;
    int e = 0;
    const long double f = std::frexp(std::fabs(y), &e); // in [0.5, 1)
    const auto m = static_cast<uint64_t>(std::ldexp(f, 64));
    return x.significand() == m && x.exponent() == e - 64;
}

} // namespace

#else
#define OPENSE4_TEST_X87 0
#endif

TEST_CASE("xmath: the tests' wide integers") {
    Rng rng(5);
    // A word with a random number of significant bits, zero included.
    const auto word = [&] {
        const uint64_t bits = rng.next();
        const auto shift = static_cast<int>(rng.below(65));
        return shift == 64 ? uint64_t{0} : bits >> shift;
    };
    // Division, by its definition: a = q × b + r with r < b.
    int wrong = 0;
    for (int i = 0; i < 20000; ++i) {
        u128 a, b;
        a.w = {word(), word()};
        b.w = {word(), word()};
        if (b == 0) continue;
        const u128 q = a / b, r = a % b;
        if (!(r < b) || q * b + r != a) ++wrong;
    }
    CHECK(wrong == 0);
    // Carries through all words of the widest one.
    const Wide<8> top = Wide<8>{1} << 511;
    CHECK(top.bitLength() == 512);
    CHECK((top - 1) + 1 == top);
    CHECK((top - 1).bitLength() == 511);
    CHECK(((top - 1) >> 447) == Wide<8>{0xffffffffffffffffu});
    CHECK((Wide<8>{3} << 300) - (Wide<8>{1} << 300) == Wide<8>{1} << 301);
    CHECK((Wide<8>{0xffffffffffffffffu} << 400) * Wide<8>{2} == (Wide<8>{0xffffffffffffffffu} << 401));

#if defined(__SIZEOF_INT128__)
    // Every operation, against the compiler's own 128-bit integers.
    __extension__ typedef unsigned __int128 native;
    const auto toNative = [](const u128& v) { return (native{v.w[1]} << 64) | v.w[0]; };
    const auto lengthOf = [](native v) {
        const auto hi = static_cast<uint64_t>(v >> 64);
        return hi != 0 ? 64 + static_cast<int>(std::bit_width(hi)) : static_cast<int>(std::bit_width(static_cast<uint64_t>(v)));
    };
    int differ = 0;
    for (int i = 0; i < 20000; ++i) {
        u128 a, b;
        a.w = {word(), word()};
        b.w = {word(), word()};
        const auto k = static_cast<int>(rng.below(128));
        const native x = toNative(a), y = toNative(b);
        if (toNative(a + b) != x + y || toNative(a - b) != x - y || toNative(a * b) != x * y || toNative(a << k) != x << k ||
            toNative(a >> k) != x >> k || (a < b) != (x < y) || (a == b) != (x == y) || a.bitLength() != lengthOf(x))
            ++differ;
        if (y != 0 && (toNative(a / b) != x / y || toNative(a % b) != x % y)) ++differ;
    }
    CHECK(differ == 0);
#endif
}

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

TEST_CASE("xmath: every operation matches the x87 extended format") {
    Rng rng(2026);
    Hasher results;
    int mismatches = 0;
    std::string first;
    const auto check = [&](bool ok, const char* what, int i) {
        if (ok) return;
        if (mismatches++ == 0) first = std::string(what) + " at sample " + std::to_string(i);
    };
    for (int i = 0; i < 200000; ++i) {
        const Ext a = randomValue(rng);
        const Ext b = randomValue(rng);
        const RefFloat ra = refExt(a), rb = refExt(b);
        const Ext sum = a + b, diff = a - b, prod = a * b, quot = a / b;
        const Ext dbl = a.roundedTo(kDoubleBits), flt = a.roundedTo(kSingleBits);
        const int64_t truncated = a.trunc(), rounded = a.round();
        check(same(sum, refAdd(ra, rb)), "+", i);
        check(same(diff, refAdd(ra, refNeg(rb))), "-", i);
        check(same(prod, refMul(ra, rb)), "*", i);
        if (!b.isZero()) check(same(quot, refDiv(ra, rb)), "/", i);
        const int order = refCompare(ra, rb);
        check((a < b) == (order < 0) && (a == b) == (order == 0), "compare", i);
        check(same(dbl, refNarrow(ra, kDoubleBits)), "double", i);
        check(same(flt, refNarrow(ra, kSingleBits)), "float", i);
        if (a.isZero() || a.exponent() <= -2) { // |a| < 2^62
            int64_t wantTrunc = 0, wantRound = 0;
            toIntegers(ra, wantTrunc, wantRound);
            check(truncated == wantTrunc && rounded == wantRound, "to an integer", i);
        }
#if OPENSE4_TEST_X87
        const long double x = toX87(a), y = toX87(b);
        check(sameBits(sum, x + y), "x87 +", i);
        check(sameBits(diff, x - y), "x87 -", i);
        check(sameBits(prod, x * y), "x87 *", i);
        if (!b.isZero()) check(sameBits(quot, x / y), "x87 /", i);
        check((a < b) == (x < y) && (a == b) == (x == y), "x87 compare", i);
        check(sameBits(dbl, static_cast<long double>(static_cast<double>(x))), "x87 double", i);
        if (std::fabs(x) > 1.0e-37L && std::fabs(x) < 1.0e38L) // float's normal range
            check(sameBits(flt, static_cast<long double>(static_cast<float>(x))), "x87 float", i);
        if (std::fabs(x) < 9.0e18L) {
            check(truncated == static_cast<int64_t>(x), "x87 trunc", i);
            check(rounded == std::llrint(x), "x87 round", i);
        }
#endif
        for (const Ext& r : {sum, diff, prod, quot, dbl, flt}) hashExt(results, r);
        results.add(a < b).add(a == b).add(truncated).add(rounded);
    }
    INFO(first);
    CHECK(mismatches == 0);
    CHECK_MESSAGE(results.value() == 0xa86e80dd2e1b1eb1ull, "the checksum of the results is " << std::format("{:#x}", results.value()));
}

TEST_CASE("xmath: sums near a tie match the x87 extended format") {
    // a near 1, and b = 2^p + 2^q placed 60 to 66 binary places below it: the
    // region where the smaller operand's last bits are shifted out and decide
    // the rounding.
    std::vector<Ext> as;
    for (const int64_t m : {int64_t{1} << 62, (int64_t{1} << 62) + 1, kMax, (int64_t{3} << 60) + 7}) as.push_back(scaled(Ext(m), -62));
    Hasher results;
    int mismatches = 0, hardware = 0;
    for (int p = 0; p <= 62; ++p)
        for (int q = 0; q <= p; ++q)
            for (int shift = 60; shift <= 66; ++shift) {
                const int64_t m = (int64_t{1} << p) + (q < p ? int64_t{1} << q : 0);
                const Ext b = scaled(Ext(m), -p - shift);
                for (const Ext& a : as) {
                    const Ext sum = a + b, diff = a - b, back = b - a;
                    const RefFloat ra = refExt(a), rb = refExt(b);
                    if (!same(sum, refAdd(ra, rb)) || !same(diff, refAdd(ra, refNeg(rb))) || !same(back, refAdd(rb, refNeg(ra))))
                        ++mismatches;
#if OPENSE4_TEST_X87
                    const long double x = toX87(a), y = toX87(b);
                    if (!sameBits(sum, x + y) || !sameBits(diff, x - y) || !sameBits(back, y - x)) ++hardware;
#endif
                    for (const Ext& r : {sum, diff, back}) hashExt(results, r);
                }
            }
    CHECK(mismatches == 0);
    CHECK(hardware == 0);
    CHECK_MESSAGE(results.value() == 0x1a1b9f806f0ff197ull, "the checksum of the results is " << std::format("{:#x}", results.value()));
}

TEST_CASE("xmath: percentages match the x87 extended format") {
    Rng rng(7);
    Hasher results;
    int mismatches = 0, hardware = 0;
    for (int i = 0; i < 300000; ++i) {
        const int64_t x = rng.range(-100000, 100000), p = rng.range(-200, 1000);
        const int64_t gotTrunc = pctTrunc(x, p), gotRound = pctRound(x, p);
        int64_t wantTrunc = 0, wantRound = 0;
        toIntegers(refMul(refInt(x), refDiv(refInt(p), refInt(100))), wantTrunc, wantRound);
        if (gotTrunc != wantTrunc || gotRound != wantRound) ++mismatches;
#if OPENSE4_TEST_X87
        const long double y = static_cast<long double>(x) * (static_cast<long double>(p) / 100.0L);
        if (gotTrunc != static_cast<int64_t>(y) || gotRound != std::llrint(y)) ++hardware;
#endif
        results.add(gotTrunc).add(gotRound);
    }
    CHECK(mismatches == 0);
    CHECK(hardware == 0);
    CHECK_MESSAGE(results.value() == 0x94ed4d801273065aull, "the checksum of the results is " << std::format("{:#x}", results.value()));
}
