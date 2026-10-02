#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace opense4 {

// Checksums, saves and network messages must have the same bytes on every
// platform (docs/ENGINE.md, "Same on every platform"). Every target is a 64-bit
// little-endian machine; the archive (game/serialize_io.hpp) writes integers
// little-endian explicitly, the hasher below hashes them as they lie in memory.
static_assert(std::endian::native == std::endian::little, "OpenSE4 assumes a little-endian machine");
static_assert(sizeof(size_t) == 8, "OpenSE4 assumes 64-bit sizes");

// The scalar types whose size is the same everywhere: bool, the fixed-width
// integers and enums. `long` is 32 bits on Windows and 64 on Linux, and
// `wchar_t` 16 and 32: the Windows builds reject them here (on Linux `long`
// is int64_t and cannot be told apart).
template <class T>
concept FixedWidthScalar =
    std::is_same_v<T, bool> || std::is_same_v<T, char> || std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> ||
    std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
    std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_enum_v<T>;

// FNV-1a, used for state checksums (determinism tests, desync detection).
class Hasher {
public:
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            h_ ^= p[i];
            h_ *= 0x100000001b3ull;
        }
    }

    template <FixedWidthScalar T>
    Hasher& add(T v) {
        bytes(&v, sizeof v);
        return *this;
    }

    Hasher& add(std::string_view s) {
        add(uint64_t{s.size()});
        bytes(s.data(), s.size());
        return *this;
    }

    uint64_t value() const { return h_; }

private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

} // namespace opense4
