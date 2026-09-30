#pragma once

// Password handling for network and PBEM games (docs/MULTIPLAYER.md).
//
// Plain-text passwords never leave the player's machine and are never stored:
//
//   password --hashPassword()--> password hash --passwordVerifier()--> verifier
//
// Clients send the password hash (in the network handshake and in .plr
// files). Hosts and saved games keep only the verifier, a hash of the hash,
// so a copy of a saved game (which every PBEM player receives) does not
// reveal anything a player could log in with. The connection itself is not
// encrypted; see the security notes in docs/MULTIPLAYER.md.

#include <array>
#include <cstdint>
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
// Empty hash -> empty verifier.
std::string passwordVerifier(std::string_view passwordHash);
// True when `verifier` is empty (no password set) or matches the hash.
bool checkPassword(std::string_view verifier, std::string_view passwordHash);
// Comparison whose duration does not depend on where the strings differ.
bool constantTimeEquals(std::string_view a, std::string_view b);

// A random 64-bit id (game ids, keepalive tokens); not for game rules.
uint64_t randomId();

} // namespace opense4::net
