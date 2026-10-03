#pragma once

// Password handling for network and PBEM games (docs/MULTIPLAYER.md).
//
// Plain-text passwords never leave the player's machine and are never stored:
//
//   password --hashPassword()--> password hash --passwordVerifier()--> verifier
//
// The password hash is the seed of a signing key (passwordKey()), and the
// verifier is that key's public half ("pk1:" and 64 hex digits). A player
// proves the password by signing: a network login signs the encrypted
// session it is made in, an orders file signs its own contents. Hosts and
// saved games keep only verifiers, and a verifier is not enough to log in
// with or to sign, so neither a copy of a game file nor a password change
// carried in an orders file reveals anything a player could use.
//
// Verifiers written by OpenSE4 0.6 and older (64 hex digits: a second
// SHA-256 of the hash) cannot check a signature. Hosts accept the password
// hash itself for them, sent only inside an encrypted connection or an
// orders file, and replace such a verifier by the new kind the first time
// its player logs in or sends orders.

#include "net/crypto.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace opense4::net {

// SHA-256 (FIPS 180-4).
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t size);
    void update(std::string_view s) { update(s.data(), s.size()); }
    std::array<uint8_t, 32> finish();

    static std::array<uint8_t, 32> of(std::string_view s) {
        Sha256 h;
        h.update(s);
        return h.finish();
    }

private:
    void block(const uint8_t* p);

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_{};
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

std::string toHex(std::span<const uint8_t> bytes);

// Empty password -> empty hash (no password).
std::string hashPassword(std::string_view password);
// The signing key a password hash stands for.
crypto::SigningKey passwordKey(std::string_view passwordHash);
// Empty hash -> empty verifier.
std::string passwordVerifier(std::string_view passwordHash);
// The verifier OpenSE4 0.6 and older kept (for tests and old games).
std::string legacyPasswordVerifier(std::string_view passwordHash);
bool isLegacyVerifier(std::string_view verifier);
// The public key in a verifier of the current kind.
std::optional<crypto::Key> verifierKey(std::string_view verifier);
// True when `verifier` is empty (no password set) or matches the hash
// (either kind of verifier).
bool checkPassword(std::string_view verifier, std::string_view passwordHash);

// Signs `message` with the password's key (empty hash: an all-zero signature).
crypto::Signature signWithPassword(std::string_view passwordHash, std::span<const uint8_t> message);
// True when `verifier` is empty (no password set), or of the current kind and
// `signature` is its key's signature of `message`. A legacy verifier checks
// no signature (false): the caller asks for the hash instead.
bool checkPasswordSignature(std::string_view verifier, std::span<const uint8_t> message, const crypto::Signature& signature);
// Comparison whose duration does not depend on where the strings differ.
bool constantTimeEquals(std::string_view a, std::string_view b);

// A random 64-bit id (game ids, keepalive tokens) from the system's
// cryptographic random source; not for game rules.
uint64_t randomId();
// A password the host's Reset Passwords gives (spec 06 §1.9): three random
// numbers from 11 to 99 written one after another, six digits, drawn from
// randomId(), a source apart from the game's random numbers.
std::string resetPassword();

} // namespace opense4::net
