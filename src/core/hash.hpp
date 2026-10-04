#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace opense4 {

// Checksums, saves and network messages must have the same bytes on every
// platform (docs/ENGINE.md, "Same on every platform"). Every target is a
// little-endian machine, 64-bit or 32-bit (armhf); the archive
// (game/serialize_io.hpp) writes integers little-endian explicitly, the hasher
// below hashes them as they lie in memory.
static_assert(std::endian::native == std::endian::little, "OpenSE4 assumes a little-endian machine");
static_assert(sizeof(size_t) == 8 || sizeof(size_t) == 4, "OpenSE4 assumes 32-bit or 64-bit sizes");

// The scalar types whose size is the same everywhere: bool, the fixed-width
// integers and enums. `long` is 32 bits on Windows and 32-bit Linux and 64 on
// 64-bit Linux and macOS, and size_t 32 bits on armhf and 64 elsewhere: a size
// is hashed or stored as uint64_t (addSize below, or a u32 count). Where one of
// them is not a fixed-width type it is rejected here: `long` on Windows, macOS
// and armhf, size_t (`unsigned long`) on macOS, and `wchar_t` (16 or 32 bits)
// everywhere. Elsewhere it is one of the fixed-width types and cannot be told
// apart, so the macOS build in CI catches a size_t passed as it is.
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

    // A size or count: 64 bits on every machine (size_t is 32 bits on armhf).
    Hasher& addSize(size_t n) { return add(uint64_t{n}); }

    Hasher& add(std::string_view s) {
        addSize(s.size());
        bytes(s.data(), s.size());
        return *this;
    }

    uint64_t value() const { return h_; }

private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

} // namespace opense4
