#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace opense4 {

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

    template <class T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    Hasher& add(T v) {
        bytes(&v, sizeof v);
        return *this;
    }

    Hasher& add(std::string_view s) {
        add(s.size());
        bytes(s.data(), s.size());
        return *this;
    }

    uint64_t value() const { return h_; }

private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

} // namespace opense4
