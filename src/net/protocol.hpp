#pragma once

// Wire protocol between HostSession and ClientSession (docs/MULTIPLAYER.md).
//
// A TCP stream of frames: u32 little-endian length L (of what follows), then
// a u8 message type and L-1 bytes of payload encoded with the save-format
// archive (game/serialize_io.hpp). Game states and order lists travel as
// complete serializeState()/serializeOrders() blobs with their own checksum.
//
// The client opens with Hello; the host answers Welcome or Reject. Before
// the Welcome the host accepts only small frames (kMaxHandshakeBytes).

#include "game/serialize_io.hpp"
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
    game::serial::fields(ar, l.gameName, l.gameId, l.humanSlots, l.started, l.turnTimeoutSeconds, l.seed, l.options, l.slots);
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
    Hello = 1,
    SubmitSetup = 2,
    SetReady = 3,
    SubmitOrders = 4,
    ChatSend = 5,
    Admin = 6,
    PlayCommands = 7,   // turn-based games
    EndTurn = 8,
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
    // both ways
    Ping = 64,
    Pong = 65,
    Bye = 66,
};

enum class RejectReason : uint8_t { Protocol, DataSet, Password, Name, Full, NotInGame, Banned, ShuttingDown };

enum class AdminAction : uint8_t { StartGame, AddComputer, RemoveSlot, Kick, ProcessTurn, SetAiControl, SetTurnTimeout };

struct Hello {
    uint32_t magic = kMagic;
    uint32_t protocol = kProtocolVersion;
    std::string app;
    std::string dataSet;
    std::string player;
    std::string passwordHash;
    std::string joinPasswordHash;
    std::string masterPasswordHash;
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

struct SubmitOrders {
    uint32_t turn = 0;
    std::vector<uint8_t> orders;  // serializeOrders()
};

struct ChatSend {
    std::string text;
};

// Turn-based games: commands the host carries out at once, one after
// another, for the sender's empire in its turn. The host answers with the
// new State, then a PlayResult with the same request number.
struct PlayCommands {
    uint32_t turn = 0;
    uint32_t request = 0;
    std::vector<uint8_t> orders;  // serializeOrders() of the commands, in order
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
    int32_t value = 0;            // StartGame: force; SetAiControl: on; SetTurnTimeout: seconds
    game::EmpireSetup setup;      // AddComputer
    std::string text;             // Kick: reason
};

struct State {
    uint32_t turn = 0;
    game::EmpireId empire;        // the receiving player's empire
    bool gameStart = false;
    std::vector<uint8_t> state;   // serializeState()
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

template <class Ar>
void io(Ar& ar, Hello& m) {
    game::serial::fields(ar, m.magic, m.protocol, m.app, m.dataSet, m.player, m.passwordHash, m.joinPasswordHash, m.masterPasswordHash);
}
template <class Ar>
void io(Ar& ar, Welcome& m) {
    game::serial::fields(ar, m.protocol, m.app, m.gameName, m.gameId, m.slot, m.admin, m.dataSet);
}
template <class Ar> void io(Ar& ar, Reject& m) { game::serial::fields(ar, m.reason, m.text); }
template <class Ar> void io(Ar& ar, SubmitSetup& m) { game::serial::fields(ar, m.setup); }
template <class Ar> void io(Ar& ar, SetReady& m) { game::serial::fields(ar, m.ready); }
template <class Ar> void io(Ar& ar, SubmitOrders& m) { game::serial::fields(ar, m.turn, m.orders); }
template <class Ar> void io(Ar& ar, ChatSend& m) { game::serial::fields(ar, m.text); }
template <class Ar> void io(Ar& ar, PlayCommands& m) { game::serial::fields(ar, m.turn, m.request, m.orders); }
template <class Ar> void io(Ar& ar, EndTurn& m) { game::serial::fields(ar, m.turn, m.request); }
template <class Ar> void io(Ar& ar, PlayResult& m) { game::serial::fields(ar, m.request, m.turn, m.ok, m.text, m.refused); }
template <class Ar> void io(Ar& ar, Admin& m) { game::serial::fields(ar, m.action, m.slot, m.value, m.setup, m.text); }
template <class Ar> void io(Ar& ar, State& m) { game::serial::fields(ar, m.turn, m.empire, m.gameStart, m.state); }
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

// Short printable form of a player-supplied string (logs, names).
std::string sanitize(std::string_view text, size_t maxLength);
bool validPlayerName(std::string_view name);

} // namespace opense4::net::proto
