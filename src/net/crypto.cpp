#include "net/crypto.hpp"

#include <monocypher.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace opense4::net::crypto {

namespace {

[[noreturn]] void noRandomness(const char* why) {
    std::fprintf(stderr, "OpenSE4: the system's random number source failed (%s); stopping rather than making weak keys.\n", why);
    std::abort();
}

// The 24-byte XChaCha20 nonce of a message: its 64-bit counter, little-endian.
std::array<uint8_t, 24> nonceOf(uint64_t counter) {
    std::array<uint8_t, 24> nonce{};
    for (size_t i = 0; i < 8; ++i) nonce[i] = static_cast<uint8_t>(counter >> (8 * i));
    return nonce;
}

} // namespace

void randomBytes(std::span<uint8_t> out) {
    if (out.empty()) return;
#if defined(_WIN32)
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        noRandomness("BCryptGenRandom");
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(out.data(), out.size());
#else
    size_t done = 0;
    while (done < out.size()) {
        const ssize_t n = getrandom(out.data() + done, out.size() - done, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            noRandomness("getrandom");
        }
        done += static_cast<size_t>(n);
    }
#endif
}

void wipe(void* data, size_t size) { crypto_wipe(data, size); }

bool equal(const Key& a, const Key& b) { return crypto_verify32(a.data(), b.data()) == 0; }

// ---- X25519 -----------------------------------------------------------------------------------

KeyPair newKeyPair() {
    Key secret{};
    randomBytes(secret);
    KeyPair k = keyPairFromSecret(secret);
    wipe(secret.data(), secret.size());
    return k;
}

KeyPair keyPairFromSecret(const Key& secret) {
    KeyPair k;
    k.secret = secret;
    crypto_x25519_public_key(k.publicKey.data(), k.secret.data());
    return k;
}

bool agree(Key& shared, const Key& mySecret, const Key& theirPublic) {
    crypto_x25519(shared.data(), mySecret.data(), theirPublic.data());
    const Key zero{};
    return !equal(shared, zero);
}

// ---- BLAKE2b ----------------------------------------------------------------------------------

struct Hash::State {
    crypto_blake2b_ctx ctx{};
    ~State() { crypto_wipe(&ctx, sizeof ctx); }
};

Hash::Hash(std::span<const uint8_t> key, size_t size) : state_(std::make_unique<State>()), size_(size == 64 ? 64 : 32) {
    if (key.empty()) crypto_blake2b_init(&state_->ctx, size_);
    else crypto_blake2b_keyed_init(&state_->ctx, size_, key.data(), std::min<size_t>(key.size(), 64));
}

Hash::~Hash() = default;

Hash& Hash::add(std::span<const uint8_t> piece) {
    add(static_cast<uint64_t>(piece.size()));
    crypto_blake2b_update(&state_->ctx, piece.data(), piece.size());
    return *this;
}

Hash& Hash::add(std::string_view piece) {
    return add(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(piece.data()), piece.size()));
}

Hash& Hash::add(uint64_t value) {
    std::array<uint8_t, 8> bytes{};
    for (size_t i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    crypto_blake2b_update(&state_->ctx, bytes.data(), bytes.size());
    return *this;
}

Key Hash::finish32() {
    std::array<uint8_t, 64> full{};
    crypto_blake2b_final(&state_->ctx, full.data());
    Key out{};
    std::copy_n(full.begin(), out.size(), out.begin());
    wipe(full.data(), full.size());
    return out;
}

std::array<uint8_t, 64> Hash::finish64() {
    std::array<uint8_t, 64> full{};
    crypto_blake2b_final(&state_->ctx, full.data());
    return full;
}

// ---- Authenticated encryption -------------------------------------------------------------------

void seal(const Key& key, uint64_t counter, std::span<const uint8_t> ad, std::span<uint8_t> text, Mac& mac) {
    const auto nonce = nonceOf(counter);
    crypto_aead_lock(text.data(), mac.data(), key.data(), nonce.data(), ad.data(), ad.size(), text.data(), text.size());
}

bool open(const Key& key, uint64_t counter, std::span<const uint8_t> ad, std::span<uint8_t> text, const Mac& mac) {
    const auto nonce = nonceOf(counter);
    return crypto_aead_unlock(text.data(), mac.data(), key.data(), nonce.data(), ad.data(), ad.size(), text.data(), text.size()) == 0;
}

// ---- Signatures -------------------------------------------------------------------------------

SigningKey::~SigningKey() { crypto_wipe(secret.data(), secret.size()); }

SigningKey signingKey(const Key& seed) {
    SigningKey k;
    Key copy = seed;  // crypto_eddsa_key_pair wipes the seed it is given
    crypto_eddsa_key_pair(k.secret.data(), k.publicKey.data(), copy.data());
    return k;
}

Signature sign(const SigningKey& key, std::span<const uint8_t> message) {
    Signature s{};
    crypto_eddsa_sign(s.data(), key.secret.data(), message.data(), message.size());
    return s;
}

bool verify(const Signature& signature, const Key& publicKey, std::span<const uint8_t> message) {
    return crypto_eddsa_check(signature.data(), publicKey.data(), message.data(), message.size()) == 0;
}

// ---- Text forms ---------------------------------------------------------------------------------

std::string hex(std::span<const uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        s.push_back(kDigits[b >> 4]);
        s.push_back(kDigits[b & 15]);
    }
    return s;
}

std::optional<Key> keyFromHex(std::string_view text) {
    if (text.size() != 64) return std::nullopt;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Key k{};
    for (size_t i = 0; i < k.size(); ++i) {
        const int hi = digit(text[2 * i]);
        const int lo = digit(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        k[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return k;
}

std::string fingerprint(const Key& publicKey) {
    const Key h = Hash().add("OpenSE4 host key fingerprint v1").add(publicKey).finish32();
    const std::string digits = hex(std::span(h).first(16));
    std::string out;
    for (size_t i = 0; i < digits.size(); i += 4) {
        if (!out.empty()) out.push_back(' ');
        out += digits.substr(i, 4);
    }
    return out;
}

} // namespace opense4::net::crypto
