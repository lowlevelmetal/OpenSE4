#pragma once

// The encrypted connection between a client and a host (docs/MULTIPLAYER.md,
// "Security"). The handshake follows the Noise pattern NX with a pre-shared
// key mixed in last (NXpsk2), on Monocypher's X25519 and BLAKE2b:
//
//   client -> host    ClientHello: protocol version, ephemeral key e_c        (clear)
//   host -> client    ServerHello: ephemeral key e_h, long-term key s_h,      (clear)
//                     whether the game has a join password
//   both              k = KDF(transcript; DH(e_c, e_h), DH(e_c, s_h), join key)
//   client -> host    Login: name, verifier, signatures of the session id     (sealed)
//   host -> client    Welcome or Reject                                        (sealed)
//
// The transcript is the two hellos as sent, so changing either breaks the
// keys. DH(e_c, s_h) means only the holder of s_h's secret half can read the
// Login, and clients pin s_h (KnownHosts) after the first connection. The
// join key comes from the join password (net::joinKey: Argon2id salted with
// the host's key and the game id), so without it a man in the middle cannot
// complete the handshake either; one who wins a first connection can still
// try to guess a weak join password offline, an Argon2id run per guess. Each direction has its own key and a
// message counter as its nonce (net/connection.hpp): a frame replayed,
// dropped or reordered fails to decrypt and ends the connection.

#include "net/crypto.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::net::secure {

struct SessionKeys {
    crypto::Key send{};
    crypto::Key receive{};
    crypto::Key sessionId{};   // the same on both sides: what logins sign
};

// The session keys of a handshake, on the client and on the host side.
// `clientHello` and `serverHello` are the two messages' payloads as sent.
// Fails when a key agreement comes out all zero (a hostile key).
std::expected<SessionKeys, std::string> clientKeys(const crypto::KeyPair& ephemeral, const crypto::Key& hostEphemeral,
                                                   const crypto::Key& hostKey, const crypto::Key& psk, std::span<const uint8_t> clientHello,
                                                   std::span<const uint8_t> serverHello);
std::expected<SessionKeys, std::string> hostKeys(const crypto::KeyPair& ephemeral, const crypto::KeyPair& hostKey, const crypto::Key& clientEphemeral,
                                                 const crypto::Key& psk, std::span<const uint8_t> clientHello, std::span<const uint8_t> serverHello);

// What a login signs: the session, the role ("player" or "master") and the
// player's name.
crypto::Key loginDigest(const crypto::Key& sessionId, std::string_view role, std::string_view player);

// ---- Long-term host keys and pins --------------------------------------------------------------

// A host's long-term key from `file` (64 hex digits of its secret half, one
// line). When the file does not exist yet, a new key is made and written
// there: the file is created exclusively (never over another one made at the
// same moment) and readable by the owner only, in a folder only the owner
// can enter when this makes it (POSIX).
std::expected<crypto::KeyPair, std::string> loadOrCreateHostKey(const std::filesystem::path& file);

// The host keys a player has trusted, one "<address>:<port> <64 hex digits>"
// line each (trust on first use, like ssh's known_hosts).
class KnownHosts {
public:
    explicit KnownHosts(std::filesystem::path file) : file_(std::move(file)) {}
    // The key trusted for this host and port, if any.
    std::optional<crypto::Key> find(std::string_view host, uint16_t port) const;
    // Trusts `key` for this host and port (replacing an earlier key) and saves.
    std::expected<void, std::string> remember(std::string_view host, uint16_t port, const crypto::Key& key);
    const std::filesystem::path& file() const { return file_; }

private:
    std::vector<std::pair<std::string, crypto::Key>> read() const;
    std::filesystem::path file_;
};

// Where OpenSE4 keeps a user's files when no SDL is around (the dedicated
// server): OPENSE4_USER_DIR, else the same folder the game uses (on Linux
// $XDG_DATA_HOME/OpenSE4 or ~/.local/share/OpenSE4, on Windows
// %APPDATA%\OpenSE4, on macOS ~/Library/Application Support/OpenSE4).
std::filesystem::path userDataDir();
inline constexpr std::string_view kHostKeyFileName = "host_key.txt";
inline constexpr std::string_view kKnownHostsFileName = "known_hosts.txt";

} // namespace opense4::net::secure
