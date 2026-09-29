#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>

namespace opense4 {

// Strongly typed integer id. Different entity kinds cannot be mixed up.
template <class Tag>
struct Id {
    static constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

    uint32_t value = kInvalid;

    constexpr Id() = default;
    template <std::integral T>
    constexpr explicit Id(T v) : value(static_cast<uint32_t>(v)) {}

    constexpr bool valid() const { return value != kInvalid; }
    constexpr explicit operator bool() const { return valid(); }
    constexpr size_t index() const { return value; }
    constexpr auto operator<=>(const Id&) const = default;
};

} // namespace opense4

template <class Tag>
struct std::hash<opense4::Id<Tag>> {
    size_t operator()(opense4::Id<Tag> id) const noexcept { return std::hash<uint32_t>{}(id.value); }
};
