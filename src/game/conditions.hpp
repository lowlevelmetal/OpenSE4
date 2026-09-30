#pragma once

// Planet conditions (docs/spec/02 §2, §13 Q46; confirmed: binary): a real
// number from 0 to 1.5, higher is better. The original keeps it as a 64-bit
// double and changes it in floating point without any rounding of its own, so
// the state keeps exactly that double, as its bit pattern, and every change is
// computed in the x87 format (xmath::Ext) and rounded to a double when it is
// stored.
//
// Who changes conditions, and how:
//   generation         R[0,10] / 10 + 0.5, asteroid fields half of that (spec 01 §5.6)
//   facilities         multiplied every 10th turn, at most 1.5, 0 becomes 0.1 (spec 02 §1.5, §2)
//   combat             lowered by D × 0.1 after shields, never below 0 (spec 04 §9.5, Q44)
//   events             added to, within 0 and 1.5 (spec 05 §2.3)
//   stellar manipulation  a created planet rolls as generation; a constructed one gets 1.5 (spec 01 §9)
// The bands (Deadly .. Optimal) and their reproduction points are in economy.hpp.

#include "game/xmath.hpp"

#include <compare>
#include <cstdint>

namespace opense4::game {

struct Conditions {
    uint64_t bits = 0;  // the IEEE 754 bit pattern of the double

    // The exact value.
    constexpr xmath::Ext value() const { return xmath::fromDoubleBits(bits); }
    // `v` stored into the double: rounded to nearest, ties to even.
    static constexpr Conditions of(xmath::Ext v) { return {xmath::toDoubleBits(v)}; }
    // n / 100 (150 = 1.5): our own inputs in hundredths, such as map files and tests.
    static constexpr Conditions hundredths(int64_t n) { return of(xmath::Ext(n) / xmath::Ext(100)); }
    // round(value × 100): our own outputs in hundredths, such as map files and
    // the percentages some lists show.
    constexpr int64_t inHundredths() const { return (value() * xmath::Ext(100)).round(); }

    friend constexpr bool operator==(Conditions, Conditions) = default;
    friend constexpr std::strong_ordering operator<=>(Conditions a, Conditions b) { return a.value() <=> b.value(); }
};

// The highest conditions, 1.5 (Optimal).
inline constexpr Conditions kOptimalConditions = Conditions::hundredths(150);

// Conditions + delta, kept within 0 and 1.5: the additive changes (events).
constexpr Conditions conditionsPlus(Conditions c, xmath::Ext delta) {
    const xmath::Ext v = c.value() + delta;
    if (v < xmath::Ext{}) return Conditions{};
    if (v > kOptimalConditions.value()) return kOptimalConditions;
    return Conditions::of(v);
}

} // namespace opense4::game
