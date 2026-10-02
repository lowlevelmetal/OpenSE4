#include "net/auth.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <random>

namespace opense4::net {

namespace {

constexpr std::array<uint32_t, 64> kRound{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// Domain separation: our hashes differ from a plain SHA-256 of the password.
constexpr std::string_view kPasswordDomain = "OpenSE4 password v1\n";
constexpr std::string_view kVerifierDomain = "OpenSE4 verifier v1\n";

} // namespace

Sha256::Sha256() : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::block(const uint8_t* p) {
    std::array<uint32_t, 64> w{};
    for (size_t i = 0; i < 16; ++i)
        w[i] = static_cast<uint32_t>(p[4 * i]) << 24 | static_cast<uint32_t>(p[4 * i + 1]) << 16 | static_cast<uint32_t>(p[4 * i + 2]) << 8 |
               static_cast<uint32_t>(p[4 * i + 3]);
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (size_t i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    total_ += size;
    while (size > 0) {
        const size_t take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, p, take);
        buffered_ += take;
        p += take;
        size -= take;
        if (buffered_ == buffer_.size()) {
            block(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::array<uint8_t, 32> Sha256::finish() {
    const uint64_t bits = total_ * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t zero = 0;
    while (buffered_ != 56) update(&zero, 1);
    std::array<uint8_t, 8> length{};
    for (size_t i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(length.data(), length.size());
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 8; ++i)
        for (size_t j = 0; j < 4; ++j) out[4 * i + j] = static_cast<uint8_t>(state_[i] >> (24 - 8 * j));
    return out;
}

std::string toHex(std::span<const uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        s.push_back(kDigits[b >> 4]);
        s.push_back(kDigits[b & 15]);
    }
    return s;
}

std::string hashPassword(std::string_view password) {
    if (password.empty()) return {};
    Sha256 h;
    h.update(kPasswordDomain);
    h.update(password);
    return toHex(h.finish());
}

std::string passwordVerifier(std::string_view passwordHash) {
    if (passwordHash.empty()) return {};
    Sha256 h;
    h.update(kVerifierDomain);
    h.update(passwordHash);
    return toHex(h.finish());
}

bool constantTimeEquals(std::string_view a, std::string_view b) {
    unsigned diff = a.size() == b.size() ? 0u : 1u;
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const auto x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0u;
        const auto y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
        diff |= x ^ y;
    }
    return diff == 0;
}

bool checkPassword(std::string_view verifier, std::string_view passwordHash) {
    if (verifier.empty()) return true;
    return constantTimeEquals(verifier, passwordVerifier(passwordHash));
}

std::string resetPassword() {
    const uint64_t a = 11 + randomId() % 89;
    const uint64_t b = 11 + randomId() % 89;
    const uint64_t c = 11 + randomId() % 89;
    return std::format("{}{}{}", a, b, c);
}

uint64_t randomId() {
    std::random_device rd;
    const uint64_t a = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    const auto t = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t z = a ^ (t * 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return z == 0 ? 1 : z;
}

} // namespace opense4::net
