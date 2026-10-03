#pragma once

// Cryptography for network and play-by-e-mail games (docs/MULTIPLAYER.md,
// "Security"). Thin wrappers over Monocypher (X25519 key agreement,
// XChaCha20-Poly1305, BLAKE2b, EdDSA signatures) and the operating system's
// random source. Nothing here implements a primitive of its own.
//
// Keys and nonces come from randomBytes(), never from the game's random
// numbers (GameState::rng), which must stay deterministic.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace opense4::net::crypto {

using Key = std::array<uint8_t, 32>;
using Signature = std::array<uint8_t, 64>;
using Mac = std::array<uint8_t, 16>;

// Fills `out` from the operating system's cryptographic random source
// (getrandom or arc4random_buf, BCryptGenRandom on Windows). Ends the program
// if that fails: a key must never come from anything weaker.
void randomBytes(std::span<uint8_t> out);

// Overwrites secrets that are no longer needed.
void wipe(void* data, size_t size);
// Constant-time comparison of two keys.
bool equal(const Key& a, const Key& b);

// ---- X25519 -----------------------------------------------------------------------------------

struct KeyPair {
    Key secret{};
    Key publicKey{};
};
// A fresh random key pair.
KeyPair newKeyPair();
KeyPair keyPairFromSecret(const Key& secret);
// The shared secret of a Diffie-Hellman exchange. False when it is all zero,
// which only a hostile peer's low-order point produces.
bool agree(Key& shared, const Key& mySecret, const Key& theirPublic);
// Whether an X25519 or EdDSA public key is a point of small order (no key
// any secret could make: hostile, or broken).
bool smallOrderX25519(const Key& publicKey);
bool smallOrderEdDsa(const Key& publicKey);

// ---- BLAKE2b ----------------------------------------------------------------------------------

// A hash of several pieces, each prefixed by its length (so that the pieces
// cannot be shifted from one into the next). `key` empty: unkeyed.
class Hash {
public:
    explicit Hash(std::span<const uint8_t> key = {}, size_t size = 32);
    ~Hash();
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    Hash& add(std::span<const uint8_t> piece);
    Hash& add(std::string_view piece);
    Hash& add(uint64_t value);
    // The first 32 bytes of the digest (size 32 or 64).
    Key finish32();
    std::array<uint8_t, 64> finish64();

private:
    struct State;
    std::unique_ptr<State> state_;
    size_t size_ = 32;
};

// ---- Authenticated encryption (XChaCha20-Poly1305) ------------------------------------------------

// Encrypts `text` in place under `key`; the nonce is the 64-bit `counter`, so
// a key must never encrypt two messages with the same counter. `ad` is
// authenticated but not encrypted.
void seal(const Key& key, uint64_t counter, std::span<const uint8_t> ad, std::span<uint8_t> text, Mac& mac);
// Decrypts in place; false (and `text` unusable) when the message, its
// additional data, the key or the counter is not the one it was sealed with.
bool open(const Key& key, uint64_t counter, std::span<const uint8_t> ad, std::span<uint8_t> text, const Mac& mac);

// Encrypts `text` in place for the holder of `recipient`'s secret half, with
// a fresh key pair of our own (returned in `ephemeral`): the key is a BLAKE2b
// hash of `domain`, the agreement and both public keys. `ad` is
// authenticated. openFrom() undoes it; false when anything differs.
void sealTo(const Key& recipient, std::string_view domain, std::span<const uint8_t> ad, std::span<uint8_t> text, Key& ephemeral, Mac& mac);
bool openFrom(const KeyPair& recipient, const Key& ephemeral, std::string_view domain, std::span<const uint8_t> ad, std::span<uint8_t> text,
              const Mac& mac);

// Like sealTo, by a sender with a long-term key pair: the key also mixes in
// the agreement of the sender's secret half with the recipient's key (as
// NaCl's crypto_box does, with a fresh key pair besides), so a message that
// opens was sealed by the holder of `sender`'s secret (or the recipient's).
// openFromSender() names the sender's public key; false when anything differs.
void sealFromSender(const KeyPair& sender, const Key& recipient, std::string_view domain, std::span<const uint8_t> ad, std::span<uint8_t> text,
                    Key& ephemeral, Mac& mac);
bool openFromSender(const KeyPair& recipient, const Key& sender, const Key& ephemeral, std::string_view domain, std::span<const uint8_t> ad,
                    std::span<uint8_t> text, const Mac& mac);

// ---- Password hashing (Argon2id) -----------------------------------------------------------------

// Argon2id of `password` with `salt` (16 bytes or more) and the given work:
// `kibibytes` of memory and `passes` over it, one lane.
Key argon2id(std::string_view password, std::span<const uint8_t> salt, uint32_t kibibytes, uint32_t passes);

// ---- Signatures (EdDSA over Curve25519 with BLAKE2b) ----------------------------------------------

struct SigningKey {
    std::array<uint8_t, 64> secret{};
    Key publicKey{};
    SigningKey() = default;
    ~SigningKey();
    SigningKey(const SigningKey&) = default;
    SigningKey& operator=(const SigningKey&) = default;
};
// The key pair a 32-byte seed stands for (the same seed, the same keys).
SigningKey signingKey(const Key& seed);
Signature sign(const SigningKey& key, std::span<const uint8_t> message);
bool verify(const Signature& signature, const Key& publicKey, std::span<const uint8_t> message);

// ---- Text forms ---------------------------------------------------------------------------------

std::string hex(std::span<const uint8_t> bytes);
std::optional<Key> keyFromHex(std::string_view text);
// A key as people compare it: eight groups of four hex digits of a hash of it.
std::string fingerprint(const Key& publicKey);

} // namespace opense4::net::crypto
