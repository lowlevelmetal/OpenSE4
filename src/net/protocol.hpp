#pragma once

// Wire protocol between HostSession and ClientSession (docs/MULTIPLAYER.md).
//
// A TCP stream of frames: u32 little-endian length L (of what follows), then
// a u8 message type and L-1 bytes of payload encoded with the save-format
// archive (game/serialize_io.hpp). Game states and order lists travel as
// complete serializeState()/serializeOrders() blobs with their own checksum.
//
// The handshake (net/secure.hpp) is in the clear: the client's ClientHello,
// the host's ServerHello, or a Reject. Every frame after it is sealed: its
// type byte is kSealedFrame, and the message type with the payload follow
// encrypted and authenticated (net/connection.hpp). The client's first sealed
// message is its Login; the host answers Welcome or Reject. Before the
// Welcome the host accepts only small frames (kMaxHandshakeBytes).

#include "game/serialize_io.hpp"
#include "net/crypto.hpp"
#include "net/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opense4::net {

// Serialization of the public types (types.hpp).
template <class Ar>
void io(Ar& ar, LobbySlot& s) {
    game::serial::fields(ar, s.id, s.kind, s.player, s.local, s.connected, s.ready, s.aiControl, s.setup);
}
template <class Ar>
void io(Ar& ar, LobbyInfo& l) {
    game::serial::fields(ar, l.gameName, l.gameId, l.humanSlots, l.started, l.turnTimeoutSeconds, l.seed, l.options, l.slots, l.mods);
}
template <class Ar>
void io(Ar& ar, EmpireTurnStatus& e) {
    game::serial::fields(ar, e.empire, e.empireName, e.player, e.human, e.alive, e.connected, e.aiControl, e.submitted, e.active);
}
template <class Ar>
void io(Ar& ar, TurnStatus& t) {
    game::serial::fields(ar, t.turn, t.processing, t.secondsLeft, t.turnBased, t.active, t.empires);
}

} // namespace opense4::net

namespace opense4::net::proto {

inline constexpr uint32_t kMagic = 0x3445534f;  // "OSE4"
inline constexpr size_t kMaxHandshakeBytes = 64 * 1024;

enum class MsgType : uint8_t {
    // client -> host
    ClientHello = 1,    // in the clear: opens the handshake
    SubmitSetup = 2,
    SetReady = 3,
    SubmitOrders = 4,
    ChatSend = 5,
    Admin = 6,
    PlayCommands = 7,   // turn-based games
    EndTurn = 8,
    Login = 9,          // the first sealed message: who the player is
    // host -> client
    Welcome = 32,
    Reject = 33,
    Lobby = 34,
    State = 35,
    TurnStatus = 36,
    OrdersAck = 37,
    Chat = 38,
    Notice = 39,
    PlayResult = 40,    // turn-based games
    ServerHello = 41,   // in the clear: the host's half of the handshake
    Desync = 42,        // the player's copy of the game differed from the host's
    // both ways
    Ping = 64,
    Pong = 65,
    Bye = 66,
};

// The type byte of every frame after the handshake: the real type is inside.
inline constexpr uint8_t kSealedFrame = 0xf0;

// OldPassword: the player's empire still has a verifier of OpenSE4 0.6; the
// host needs that password's old hash once (Login::legacyPasswordHash), which
// the player's game sends only when the player agrees.
// Mods: the player's game-affecting mods differ from the host's (protocol 7).
enum class RejectReason : uint8_t { Protocol, DataSet, Password, Name, Full, NotInGame, Banned, ShuttingDown, OldPassword, Mods };

enum class AdminAction : uint8_t { StartGame, AddComputer, RemoveSlot, Kick, ProcessTurn, SetAiControl, SetTurnTimeout, ResetPasswords };

// The start of every client's first message, in every protocol version: a
// host reads this much first and refuses another version with a reason the
// client can show.
struct VersionProbe {
    uint32_t magic = 0;
    uint32_t protocol = 0;
    std::string app;
};

// The client's opening, in the clear. It is laid out like protocol 4's
// greeting (magic, protocol, program version and five strings), so that a
// host of OpenSE4 0.6 reads it and answers with a readable refusal.
struct ClientHello {
    uint32_t magic = kMagic;
    uint32_t protocol = kProtocolVersion;
    std::string app;
    std::string ephemeralKey;   // 32 bytes: the client's X25519 key for this connection
    std::array<std::string, 4> reserved;
};

// The host's answer, in the clear (net/secure.hpp).
struct ServerHello {
    uint32_t protocol = kProtocolVersion;
    std::string app;
    crypto::Key ephemeralKey{};   // the host's X25519 key for this connection
    crypto::Key hostKey{};        // the host's long-term key (clients may pin it)
    bool joinPassword = false;    // the session keys include the join password's
    uint64_t gameId = 0;          // salts the password and join keys (net/auth.hpp)
};

// The client's first sealed message. The proofs sign secure::loginDigest()
// of this very session, so they are worthless anywhere else.
struct Login {
    std::string dataSet;
    std::vector<ruleset::ModRecord> mods;  // the player's mods, in load order (protocol 7)
    std::string player;
    uint64_t clientId = 0;               // random per ClientSession: a request repeated after a reconnect is recognized
    std::string passwordVerifier;        // passwordVerifier() of the player's password in this game (empty: none)
    crypto::Signature passwordProof{};   // the password's signature of the session
    bool master = false;                 // the player gives the master password
    crypto::Signature masterProof{};
    // Only after the host refused with OldPassword and the player agreed:
    // the password's OpenSE4 0.6 hash, which the host checks once and then
    // keeps `passwordVerifier` instead.
    std::string legacyPasswordHash;
};

struct Welcome {
    uint32_t protocol = kProtocolVersion;
    std::string app;
    std::string gameName;
    uint64_t gameId = 0;
    uint32_t slot = kNoSlot;
    bool admin = false;
    std::string dataSet;
};

struct Reject {
    RejectReason reason = RejectReason::Protocol;
    std::string text;
};

struct SubmitSetup {
    game::EmpireSetup setup;
};

struct SetReady {
    bool ready = false;
};

// What the player's copy of the game looked like when it sent orders or
// commands: the State it last received, and that state's checksum and part
// hashes as the client computes them now (game::statePartHashes). The host
// compares them with what it sent; a difference is a desync.
struct BaseState {
    uint32_t serial = 0;
    uint64_t checksum = 0;
    std::vector<uint64_t> parts;
};

struct SubmitOrders {
    uint32_t turn = 0;
    std::vector<uint8_t> orders;  // serializeOrders()
    BaseState base;
};

struct ChatSend {
    std::string text;
};

// Turn-based games: commands the host carries out at once, one after
// another, for the sender's empire in its turn. The host answers with the
// new State, then a PlayResult with the same request number.
struct PlayCommands {
    uint32_t turn = 0;
    uint32_t request = 0;         // increasing per ClientSession (Login::clientId)
    std::vector<uint8_t> orders;  // serializeOrders() of the commands, in order
    BaseState base;
};

// Turn-based games: the sender ends its turn. The host answers with a
// PlayResult, then sends everyone the state once the turn has passed on.
struct EndTurn {
    uint32_t turn = 0;
    uint32_t request = 0;
};

struct PlayResult {
    uint32_t request = 0;
    uint32_t turn = 0;
    bool ok = false;                   // false: nothing was done (text says why)
    std::string text;
    std::vector<std::string> refused;  // commands the rules refused
};

struct Admin {
    AdminAction action = AdminAction::StartGame;
    uint32_t slot = kNoSlot;
    int32_t value = 0;            // StartGame: force; SetAiControl: on; SetTurnTimeout: seconds; ResetPasswords: bit i = empire i
    game::EmpireSetup setup;      // AddComputer
    std::string text;             // Kick: reason
};

struct State {
    uint32_t turn = 0;
    game::EmpireId empire;        // the receiving player's empire
    bool gameStart = false;
    std::vector<uint8_t> state;   // serializeState()
    uint32_t serial = 0;          // increasing per host: what BaseState::serial refers to
    bool resync = false;          // sent again after a desync (Desync came first)
};

// The host found the player's copy of the game different from what it sent
// (BaseState); the State that follows replaces it.
struct Desync {
    uint32_t turn = 0;
    std::vector<std::string> parts;   // which parts of the state differ
    std::string text;
};

struct OrdersAck {
    uint32_t turn = 0;
    bool ok = false;
    std::string text;
};

struct Chat {
    std::string from;
    std::string text;
};

struct Notice {
    std::string text;
};

struct Ping {
    uint64_t token = 0;
};

struct Bye {
    std::string reason;
};

template <class Ar> void io(Ar& ar, VersionProbe& m) { game::serial::fields(ar, m.magic, m.protocol, m.app); }
template <class Ar>
void io(Ar& ar, ClientHello& m) {
    game::serial::fields(ar, m.magic, m.protocol, m.app, m.ephemeralKey, m.reserved);
}
template <class Ar>
void io(Ar& ar, ServerHello& m) {
    game::serial::fields(ar, m.protocol, m.app, m.ephemeralKey, m.hostKey, m.joinPassword, m.gameId);
}
template <class Ar>
void io(Ar& ar, Login& m) {
    game::serial::fields(ar, m.dataSet, m.mods, m.player, m.clientId, m.passwordVerifier, m.passwordProof, m.master, m.masterProof,
                         m.legacyPasswordHash);
}
template <class Ar> void io(Ar& ar, BaseState& m) { game::serial::fields(ar, m.serial, m.checksum, m.parts); }
template <class Ar>
void io(Ar& ar, Welcome& m) {
    game::serial::fields(ar, m.protocol, m.app, m.gameName, m.gameId, m.slot, m.admin, m.dataSet);
}
template <class Ar> void io(Ar& ar, Reject& m) { game::serial::fields(ar, m.reason, m.text); }
template <class Ar> void io(Ar& ar, SubmitSetup& m) { game::serial::fields(ar, m.setup); }
template <class Ar> void io(Ar& ar, SetReady& m) { game::serial::fields(ar, m.ready); }
template <class Ar> void io(Ar& ar, SubmitOrders& m) { game::serial::fields(ar, m.turn, m.orders, m.base); }
template <class Ar> void io(Ar& ar, ChatSend& m) { game::serial::fields(ar, m.text); }
template <class Ar> void io(Ar& ar, PlayCommands& m) { game::serial::fields(ar, m.turn, m.request, m.orders, m.base); }
template <class Ar> void io(Ar& ar, EndTurn& m) { game::serial::fields(ar, m.turn, m.request); }
template <class Ar> void io(Ar& ar, PlayResult& m) { game::serial::fields(ar, m.request, m.turn, m.ok, m.text, m.refused); }
template <class Ar> void io(Ar& ar, Admin& m) { game::serial::fields(ar, m.action, m.slot, m.value, m.setup, m.text); }
template <class Ar> void io(Ar& ar, State& m) { game::serial::fields(ar, m.turn, m.empire, m.gameStart, m.state, m.serial, m.resync); }
template <class Ar> void io(Ar& ar, Desync& m) { game::serial::fields(ar, m.turn, m.parts, m.text); }
template <class Ar> void io(Ar& ar, OrdersAck& m) { game::serial::fields(ar, m.turn, m.ok, m.text); }
template <class Ar> void io(Ar& ar, Chat& m) { game::serial::fields(ar, m.from, m.text); }
template <class Ar> void io(Ar& ar, Notice& m) { game::serial::fields(ar, m.text); }
template <class Ar> void io(Ar& ar, Ping& m) { game::serial::fields(ar, m.token); }
template <class Ar> void io(Ar& ar, Bye& m) { game::serial::fields(ar, m.reason); }

// Protocol messages carry the archive version of the protocol, not of saves.
inline constexpr uint32_t kArchiveVersion = game::kSaveVersion;

template <class T>
std::vector<uint8_t> encode(const T& message) {
    return game::serial::encode(message, kArchiveVersion);
}

template <class T>
bool decode(std::span<const uint8_t> payload, T& out, std::string& error) {
    return game::serial::decode(payload, out, error, kArchiveVersion);
}

// Reads the VersionProbe at the start of a client's first message, whatever follows.
bool probeVersion(std::span<const uint8_t> payload, VersionProbe& out);

// Whether commands a player sent set only password values a host may keep
// (cmd::SetEmpireOptions::passwordHash): none, or a verifier of the current
// kind (net::usableVerifier). Anything else could lock the empire out, or
// pose as an OpenSE4 0.6 verifier.
bool usablePasswordValues(const std::vector<game::Command>& commands);

// Short printable form of a player-supplied string (logs, names).
std::string sanitize(std::string_view text, size_t maxLength);
bool validPlayerName(std::string_view name);

} // namespace opense4::net::proto
