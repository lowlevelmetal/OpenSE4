#pragma once

// Mod versions and the version ranges of a manifest's [requires]
// (docs/sdk/packages-and-data.md): whole numbers separated by dots, compared
// part by part with missing parts as 0, so 1.2 equals 1.2.0.

#include <compare>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {

struct Version {
    std::vector<uint32_t> parts;  // "1.2.0" -> {1, 2, 0}
    std::string text;             // as written

    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
    friend bool operator==(const Version& a, const Version& b) { return (a <=> b) == 0; }
};

// One to four whole numbers separated by dots ("1", "1.2", "1.2.0").
std::optional<Version> parseVersion(std::string_view text);

// A condition on a version: every bound must hold. Written as one or more
// comparisons separated by commas or spaces: ">=1.0", ">=1.0, <2", "=1.2.3",
// "1.2" (any 1.2.x), "^1.2" (at least 1.2, below 2), "~1.2" (at least 1.2,
// below 1.3), "*" (any).
struct VersionRange {
    enum class Op : uint8_t { Equal, Greater, GreaterEqual, Less, LessEqual, Prefix };
    struct Bound {
        Op op = Op::Equal;
        Version version;
    };
    std::vector<Bound> bounds;  // empty: any version
    std::string text;           // as written

    bool contains(const Version& v) const;
};

std::expected<VersionRange, std::string> parseVersionRange(std::string_view text);

} // namespace opense4::mods
