#pragma once

// Password handling for network and PBEM games (docs/MULTIPLAYER.md,
// "Security"). In each game a password stands for two key pairs:
//
//   password, game id --Argon2id--> seed --BLAKE2b--> signing key (EdDSA)
//                                                   + box key (X25519)
//
// The verifier is their public halves, after the Argon2id work they were made
// with ("pk2:<KiB>:<passes>:" and 128 hex digits), so a verifier is checked
// with its own work even after the default changes. A player
// proves the password by signing (a login signs its session, an orders file
// its contents), and a PBEM turn file is encrypted to the box key. Hosts and
// saved games keep only verifiers: enough to check a signature or to encrypt,
// not to sign or to decrypt. Guessing a password from a verifier costs an
// Argon2id run per guess, salted per game, so a verifier of one game helps
// with no other.
//
// Argon2id runs on the players' machines and when a host starts (its own
// player, the master and join passwords); hosts otherwise only check
// signatures. Results are kept in the process (passwordKeys and joinKey are
// cheap the second time), and wiped when the cache is emptied and at exit.
// A run needs its work's memory at once (128 MiB by default): when that is
// not to be had, the functions that run it throw PasswordWorkError.
//
// OpenSE4 0.6 kept a verifier made of a fast, unsalted hash of the password,
// and sent that hash in the clear. Such a verifier is checked against that
// hash (legacyPasswordHash) only to move the empire to a verifier of the
// current kind, made from the password itself: nothing is ever made of the
// old hash.

#include "net/crypto.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
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

// The Argon2id work of password keys. It is part of the key, and every
// verifier records the work it was made with: new keys use the work set here
// (only tests lower it), and a key checked against a verifier uses that
// verifier's work.
struct PasswordWork {
    uint32_t kibibytes = 128 * 1024;
    uint32_t passes = 3;
    friend bool operator==(const PasswordWork&, const PasswordWork&) = default;
};
// The work a verifier may name: 8 KiB (Argon2id's least) to 1 GiB, 1 to 16
// passes. More would let a hostile verifier tie up a player's computer.
inline constexpr uint32_t kMinPasswordKibibytes = 8;
inline constexpr uint32_t kMaxPasswordKibibytes = 1024 * 1024;
inline constexpr uint32_t kMaxPasswordPasses = 16;
bool usableWork(PasswordWork work);
// "128 MiB, 3 passes".
std::string describe(PasswordWork work);
void setPasswordWork(PasswordWork work);
PasswordWork passwordWork();

// A password key could not be made: the computer did not have the memory its
// Argon2id work needs. what() says so in words for the player.
class PasswordWorkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The keys a password stands for in one game.
struct PasswordKeys {
    crypto::SigningKey signing;   // signs logins and orders files
    crypto::KeyPair box;          // opens the turn files sent to the empire
    PasswordWork work;            // the Argon2id work they were made with
    std::string verifier() const;
};
// Empty password: none (no password). With the work set by setPasswordWork,
// or the given one. Throws PasswordWorkError.
std::optional<PasswordKeys> passwordKeys(std::string_view password, uint64_t gameId);
std::optional<PasswordKeys> passwordKeys(std::string_view password, uint64_t gameId, PasswordWork work);
// The keys to check against `verifier`: made with the work it records (one of
// another kind: the work set). Throws PasswordWorkError.
std::optional<PasswordKeys> passwordKeysFor(std::string_view verifier, std::string_view password, uint64_t gameId);
// Empty password: empty verifier. Throws PasswordWorkError.
std::string passwordVerifier(std::string_view password, uint64_t gameId);
// True when `verifier` is empty (no password set) or the password's, of
// either kind (a verifier of OpenSE4 0.6 is checked with legacyPasswordHash).
// Throws PasswordWorkError.
bool checkPassword(std::string_view verifier, std::string_view password, uint64_t gameId);

// The public keys in a verifier of the current kind, and the work they were
// made with; none when it is of another kind, malformed, names a work out of
// bounds, or holds a key of small order.
struct VerifierKeys {
    crypto::Key signing{};
    crypto::Key box{};
    PasswordWork work;
};
std::optional<VerifierKeys> verifierKeys(std::string_view verifier);
// A verifier a player may give (for a new slot, or as a new password): empty
// (no password), or a well-formed one of the current kind.
bool usableVerifier(std::string_view verifier);
// Why `verifier` is not one of the current kind, in words (empty when it is).
std::string verifierProblem(std::string_view verifier);

// Signs `message` with the password's key (none: an all-zero signature).
crypto::Signature signWith(const std::optional<PasswordKeys>& keys, std::span<const uint8_t> message);
// True when `verifier` is empty (no password set), or of the current kind and
// `signature` is its key's signature of `message`. A verifier of OpenSE4 0.6
// checks no signature (false).
bool checkPasswordSignature(std::string_view verifier, std::span<const uint8_t> message, const crypto::Signature& signature);

// ---- OpenSE4 0.6 ---------------------------------------------------------------------------------

// 0.6's password hash (a SHA-256 with a prefix; empty password: empty).
std::string legacyPasswordHash(std::string_view password);
// 0.6's verifier of that hash (a second SHA-256).
std::string legacyPasswordVerifier(std::string_view legacyHash);
bool isLegacyVerifier(std::string_view verifier);
bool checkLegacyPassword(std::string_view verifier, std::string_view legacyHash);

// ---- Join passwords --------------------------------------------------------------------------------

// The key a join password stands for with one host and one game: Argon2id
// salted with the host's key and the game id, with the work set (empty
// password: all zero). Throws PasswordWorkError.
crypto::Key joinKey(std::string_view joinPassword, const crypto::Key& hostKey, uint64_t gameId);

// Wipes the Argon2id results kept in this process (also done at exit).
void forgetPasswordKeys();

// Comparison whose duration does not depend on where the strings differ.
bool constantTimeEquals(std::string_view a, std::string_view b);

// A random 64-bit id (game ids, keepalive tokens) from the system's
// cryptographic random source; not for game rules.
uint64_t randomId();
// A password the host's Reset Passwords gives (spec 06 §1.9): random numbers
// from 11 to 99 written one after another, drawn from randomId(), a source
// apart from the game's random numbers. The original writes three (six
// digits); OpenSE4 writes six (twelve digits), as its verifiers can be
// guessed offline.
std::string resetPassword();

} // namespace opense4::net
