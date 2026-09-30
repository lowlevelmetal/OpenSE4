#pragma once

// Extended-precision arithmetic for the classic rules (confirmed: binary).
// Specs: docs/spec/02 "Arithmetic", docs/spec/03 "Conventions", docs/spec/05 §0 "Math".
//
// The rule
// --------
// Amounts are integers, but the original applies most percentages in floating
// point. It forms the factor p / 100, multiplies the amount by it and turns the
// product back into an integer. Each rule says how it does that: by rounding
// (to the nearest integer, ties to the even one) or by truncating (dropping the
// fraction, toward zero). All of this happens in the x87's extended format.
// Its significand is 64 bits wide, and each operation's exact result is rounded
// to 64 bits, to the nearest value with ties to even. That is the processor's
// default precision and rounding mode. The game never seems to change them
// (inferred).
//
// The factor p / 100 is not exact for most p. So a product that is a whole
// number in exact arithmetic can come out a hair below it and truncate one
// lower. Three examples from the specs: trunc(100 × 53 %) = 52,
// trunc(90 × 130 %) = 116 and trunc(300 × 21 %) = 62. Some rules chain several
// operations: a percentage of a percentage, a pool divided by a count, a sum,
// or a comparison "in floating point". Each operation rounds on its own, so a
// rule must be written in the same order as its spec formula.
//
// Why an emulation
// ----------------
// Turn resolution must give the same result on every machine and must not use
// the host's floating point. `Ext` reproduces the extended format exactly with
// integer operations only. It has no compiler extensions and no floating-point
// types, so its results are identical on every platform and compiler. Every
// function here is constexpr.
//
// Typical use
//   trunc(x × p %)              pctTrunc(x, p)
//   round(x × p %)              pctRound(x, p)
//   round(P × ((R / 100) / 10)) (Ext(P) * (percent(R) / Ext(10))).round()
//   a > b × (100 + d) %         Ext(a) > Ext(b) * percent(100 + d)
//   round(pool / N)             divRoundHalfEven(pool, N)
//   a counter stored as double  c = (c + Ext(speed) / Ext(30)).roundedTo(kDoubleBits)
//
// Limits (never reached by game values). Results beyond the extended format's
// exponent range saturate to the largest finite value or flush to zero. x87
// would give an infinity or a denormal there. Dividing by zero gives zero.
// Converting a value outside the int64 range to an integer saturates.

#include <bit>
#include <compare>
#include <cstdint>
#include <limits>

namespace opense4::game::xmath {

namespace detail {

// An unsigned 128-bit integer as two words, with just the operations the
// rounding code needs.
struct U128 {
    uint64_t hi = 0;
    uint64_t lo = 0;
    constexpr bool isZero() const { return (hi | lo) == 0; }
};

constexpr int leadingZeros(U128 v) { return v.hi != 0 ? std::countl_zero(v.hi) : 64 + std::countl_zero(v.lo); }

// v << n, for 0 <= n < 128.
constexpr U128 shiftLeft(U128 v, int n) {
    if (n == 0) return v;
    if (n >= 64) return {v.lo << (n - 64), 0};
    return {(v.hi << n) | (v.lo >> (64 - n)), v.lo << n};
}

// v >> n, for n >= 0. `lost` tells whether any 1 bit was shifted out.
constexpr U128 shiftRight(U128 v, int64_t n, bool& lost) {
    if (n == 0) {
        lost = false;
        return v;
    }
    if (n >= 128) {
        lost = !v.isZero();
        return {};
    }
    if (n >= 64) {
        const int k = static_cast<int>(n - 64);
        lost = v.lo != 0 || (k > 0 && (v.hi << (64 - k)) != 0);
        return {0, v.hi >> k};
    }
    const int k = static_cast<int>(n);
    lost = (v.lo << (64 - k)) != 0;
    return {v.hi >> k, (v.lo >> k) | (v.hi << (64 - k))};
}

constexpr U128 add(U128 a, U128 b) {
    const uint64_t lo = a.lo + b.lo;
    return {a.hi + b.hi + (lo < a.lo ? 1u : 0u), lo};
}

// a - b, for a >= b.
constexpr U128 sub(U128 a, U128 b) { return {a.hi - b.hi - (a.lo < b.lo ? 1u : 0u), a.lo - b.lo}; }

// The full 128-bit product of two 64-bit words.
constexpr U128 mul(uint64_t a, uint64_t b) {
    constexpr uint64_t kLow = 0xffffffffu;
    const uint64_t ll = (a & kLow) * (b & kLow);
    const uint64_t lh = (a & kLow) * (b >> 32);
    const uint64_t hl = (a >> 32) * (b & kLow);
    const uint64_t hh = (a >> 32) * (b >> 32);
    const uint64_t mid = (ll >> 32) + (lh & kLow) + (hl & kLow);
    return {hh + (lh >> 32) + (hl >> 32) + (mid >> 32), (mid << 32) | (ll & kLow)};
}

// floor(a × 2^65 / b) for a and b in [2^63, 2^64): a quotient of 65 or 66 bits.
// `inexact` tells whether the division left a remainder. Long division, one bit
// per step; the running remainder needs 65 bits, so its top bit is kept apart.
constexpr U128 divide(uint64_t a, uint64_t b, bool& inexact) {
    U128 q;
    uint64_t r = a;
    bool carry = false;
    for (int i = 0; i < 66; ++i) {
        const bool bit = carry || r >= b;
        if (bit) r -= b; // modulo 2^64; the true difference is below b
        q = shiftLeft(q, 1);
        q.lo |= bit ? 1u : 0u;
        carry = (r >> 63) != 0;
        r <<= 1;
    }
    inexact = carry || r != 0;
    return q;
}

} // namespace detail

// Significand widths for Ext::roundedTo: what a store to a 64-bit double or a
// 32-bit float keeps.
inline constexpr int kDoubleBits = 53;
inline constexpr int kSingleBits = 24;

// A number in the x87 extended format: ±significand × 2^exponent. The
// significand is 64 bits wide with its top bit set, or 0 for zero. Every
// operation rounds its exact result once, to 64 bits, to nearest with ties to
// even. Zero has no sign.
class Ext {
public:
    constexpr Ext() = default; // zero

    // Exact: every int64 fits in 64 bits.
    explicit constexpr Ext(int64_t v) {
        if (v == 0) return;
        neg_ = v < 0;
        const uint64_t mag = neg_ ? uint64_t{0} - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
        const int shift = std::countl_zero(mag);
        mant_ = mag << shift;
        exp_ = -shift;
    }

    friend constexpr Ext operator+(Ext a, Ext b) {
        if (b.isZero()) return a;
        if (a.isZero()) return b;
        if (magnitudeLess(a, b)) {
            const Ext t = a;
            a = b;
            b = t;
        }
        // Both significands are placed at bit 126 of a 128-bit word, so that the
        // sum cannot overflow. The smaller one is then shifted right to line up.
        // Bits shifted out count as a fraction below the last bit.
        const int64_t exp = int64_t{a.exp_} - 63;
        const detail::U128 wa{a.mant_ >> 1, a.mant_ << 63};
        bool lost = false;
        const detail::U128 wb = detail::shiftRight({b.mant_ >> 1, b.mant_ << 63}, int64_t{a.exp_} - b.exp_, lost);
        if (a.neg_ == b.neg_) return pack(a.neg_, exp, detail::add(wa, wb), lost);
        // wa − (wb + f), with f in (0, 1) when bits were lost, is (wa − wb − 1) + (1 − f).
        detail::U128 diff = detail::sub(wa, wb);
        if (lost) diff = detail::sub(diff, {0, 1});
        return pack(a.neg_, exp, diff, lost);
    }
    friend constexpr Ext operator-(Ext a, Ext b) { return a + -b; }
    friend constexpr Ext operator*(Ext a, Ext b) {
        if (a.isZero() || b.isZero()) return {};
        return pack(a.neg_ != b.neg_, int64_t{a.exp_} + b.exp_, detail::mul(a.mant_, b.mant_), false);
    }
    // b = 0 gives zero (see the file comment).
    friend constexpr Ext operator/(Ext a, Ext b) {
        if (a.isZero() || b.isZero()) return {};
        bool inexact = false;
        const detail::U128 q = detail::divide(a.mant_, b.mant_, inexact);
        return pack(a.neg_ != b.neg_, int64_t{a.exp_} - b.exp_ - 65, q, inexact);
    }
    constexpr Ext operator-() const {
        Ext r = *this;
        if (!isZero()) r.neg_ = !neg_;
        return r;
    }
    constexpr Ext& operator+=(Ext o) { return *this = *this + o; }
    constexpr Ext& operator-=(Ext o) { return *this = *this - o; }
    constexpr Ext& operator*=(Ext o) { return *this = *this * o; }
    constexpr Ext& operator/=(Ext o) { return *this = *this / o; }

    friend constexpr bool operator==(Ext, Ext) = default; // the representation is canonical
    friend constexpr std::strong_ordering operator<=>(Ext a, Ext b) {
        if (a.neg_ != b.neg_) return a.neg_ ? std::strong_ordering::less : std::strong_ordering::greater;
        if (a == b) return std::strong_ordering::equal;
        return magnitudeLess(a, b) != a.neg_ ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // To an integer by truncation, toward zero.
    constexpr int64_t trunc() const {
        if (isZero() || exp_ <= -64) return 0;
        if (exp_ >= 0) return saturated();
        const uint64_t mag = mant_ >> -exp_;
        return neg_ ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    }

    // To the nearest integer, ties to even.
    constexpr int64_t round() const {
        if (isZero() || exp_ < -64) return 0; // below one half
        if (exp_ >= 0) return saturated();
        const int shift = -exp_; // 1..64
        uint64_t whole = shift == 64 ? 0 : mant_ >> shift;
        const uint64_t half = uint64_t{1} << (shift - 1);
        const uint64_t frac = mant_ & (half + (half - 1));
        if (frac > half || (frac == half && (whole & 1) != 0)) ++whole;
        if (whole > uint64_t{std::numeric_limits<int64_t>::max()}) return saturated();
        return neg_ ? -static_cast<int64_t>(whole) : static_cast<int64_t>(whole);
    }

    // The value with its significand rounded to `bits` bits (1 to 64), to
    // nearest with ties to even. Use it where the original stores an
    // intermediate as a double (kDoubleBits) or a float (kSingleBits). The
    // narrower formats' exponent ranges are not modelled.
    constexpr Ext roundedTo(int bits) const {
        if (isZero() || bits >= 64) return *this;
        const int drop = 64 - (bits < 1 ? 1 : bits); // 1..63
        const uint64_t half = uint64_t{1} << (drop - 1);
        const uint64_t frac = mant_ & (half + (half - 1));
        uint64_t kept = mant_ >> drop;
        if (frac > half || (frac == half && (kept & 1) != 0)) ++kept;
        return pack(neg_, int64_t{exp_} + drop, {0, kept}, false);
    }

    constexpr bool isZero() const { return mant_ == 0; }
    constexpr bool isNegative() const { return neg_; }
    // value = ±significand() × 2^exponent(); significand() has bit 63 set unless zero.
    constexpr uint64_t significand() const { return mant_; }
    constexpr int exponent() const { return exp_; }

private:
    // The extended format's range: 2^-16382 ≤ |value| < 2^16384.
    static constexpr int64_t kMinExp = -16382 - 63;
    static constexpr int64_t kMaxExp = 16383 - 63;

    bool neg_ = false;
    int32_t exp_ = 0;
    uint64_t mant_ = 0;

    static constexpr bool magnitudeLess(Ext a, Ext b) {
        if (a.isZero() || b.isZero()) return a.mant_ < b.mant_;
        return a.exp_ != b.exp_ ? a.exp_ < b.exp_ : a.mant_ < b.mant_;
    }

    // The integer conversions' answer for |value| ≥ 2^63.
    constexpr int64_t saturated() const {
        if (neg_) return std::numeric_limits<int64_t>::min(); // exact for −2^63
        return std::numeric_limits<int64_t>::max();
    }

    // Rounds (m + f) × 2^exp to 64 bits, to nearest with ties to even. f is a
    // fraction strictly between 0 and 1 when `sticky` is set, and 0 otherwise.
    // When `sticky` is set, m must have at least 65 significant bits, so that
    // the bit just below the kept 64 (the guard bit) is one of m's own bits.
    static constexpr Ext pack(bool neg, int64_t exp, detail::U128 m, bool sticky) {
        if (m.isZero()) return {};
        const int lz = detail::leadingZeros(m);
        m = detail::shiftLeft(m, lz);
        exp += 64 - lz;
        uint64_t mant = m.hi;
        const bool guard = (m.lo >> 63) != 0;
        const bool rest = (m.lo << 1) != 0 || sticky;
        if (guard && (rest || (mant & 1) != 0)) {
            ++mant;
            if (mant == 0) { // carried out of the top: 2^64
                mant = uint64_t{1} << 63;
                ++exp;
            }
        }
        Ext r;
        if (exp < kMinExp) return r;
        if (exp > kMaxExp) {
            exp = kMaxExp;
            mant = ~uint64_t{0};
        }
        r.neg_ = neg;
        r.exp_ = static_cast<int32_t>(exp);
        r.mant_ = mant;
        return r;
    }
};

// The factor p / 100, rounded to 64 bits: what a percentage multiplies by.
constexpr Ext percent(int64_t p) { return Ext(p) / Ext(100); }

// trunc(x × (p / 100)): the spec's "truncate(x × p %)".
constexpr int64_t pctTrunc(int64_t x, int64_t p) { return (Ext(x) * percent(p)).trunc(); }

// round(x × (p / 100)), ties to even: the spec's "round(x × p %)".
constexpr int64_t pctRound(int64_t x, int64_t p) { return (Ext(x) * percent(p)).round(); }

// a / b in exact integer arithmetic, rounded to nearest with ties to even.
// b = 0 gives 0; the one overflowing case (INT64_MIN / −1) saturates. For every
// a and b ≠ 0 this equals (Ext(a) / Ext(b)).round(). The rounded 64-bit
// quotient never reaches a half-integer that the exact one does not. So a
// floating-point "round(pool / N)" can use this. Likewise a / b (C++
// truncation) equals (Ext(a) / Ext(b)).trunc().
constexpr int64_t divRoundHalfEven(int64_t a, int64_t b) {
    if (b == 0) return 0;
    if (b == -1) return a == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int64_t>::max() : -a;
    const int64_t q = a / b;
    const int64_t r = a % b;
    if (r == 0) return q;
    const uint64_t absR = r < 0 ? uint64_t{0} - static_cast<uint64_t>(r) : static_cast<uint64_t>(r);
    const uint64_t absB = b < 0 ? uint64_t{0} - static_cast<uint64_t>(b) : static_cast<uint64_t>(b);
    const uint64_t other = absB - absR; // distance to the next integer away from zero
    if (absR > other || (absR == other && (q & 1) != 0)) return (a < 0) != (b < 0) ? q - 1 : q + 1;
    return q;
}

} // namespace opense4::game::xmath
