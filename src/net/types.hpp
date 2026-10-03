#pragma once

// Public types of the multiplayer library: lobby, turn status and the events
// HostSession::poll() and ClientSession::poll() return (docs/MULTIPLAYER.md).

#include "game/setup.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::net {

inline constexpr uint16_t kDefaultPort = 6720;       // TCP, as in the classic game
inline constexpr uint32_t kProtocolVersion = 5;
inline constexpr uint32_t kNoSlot = 0xffffffffu;
inline constexpr size_t kMaxPlayerNameLength = 32;
inline constexpr size_t kMaxChatLength = 500;

std::string_view appVersion();  // "OpenSE4 x.y.z"

// ---- Lobby ---------------------------------------------------------------------------------

enum class SlotKind : uint8_t { Human, Computer };

struct LobbySlot {
    uint32_t id = 0;                 // stable while the lobby lasts
    SlotKind kind = SlotKind::Human;
    std::string player;              // login name; empty = open human slot
    bool local = false;              // the hosting player (in-game hosting)
    bool connected = false;
    bool ready = false;
    bool aiControl = false;          // in game: the computer plays this human empire
    game::EmpireSetup setup;         // passwordHash is never sent to clients

    bool open() const { return kind == SlotKind::Human && player.empty(); }
};

struct LobbyInfo {
    std::string gameName;
    uint64_t gameId = 0;
    uint32_t humanSlots = 0;
    bool started = false;
    int turnTimeoutSeconds = 0;      // 0 = none
    uint64_t seed = 0;
    game::GameOptions options;
    std::vector<LobbySlot> slots;    // in empire order once started

    const LobbySlot* slot(uint32_t id) const {
        for (const LobbySlot& s : slots)
            if (s.id == id) return &s;
        return nullptr;
    }
};

// ---- Turns -----------------------------------------------------------------------------------

struct EmpireTurnStatus {
    game::EmpireId empire;
    std::string empireName;
    std::string player;              // empty for computer empires
    bool human = false;
    bool alive = true;
    bool connected = false;
    bool aiControl = false;          // human empire currently played by the computer
    bool submitted = false;          // orders received for this turn (turn-based: set for every empire but the active one)
    bool active = false;             // turn-based: this empire's turn is in progress

    // The host waits for this player: for its orders (simultaneous), or for
    // it to play and end its turn (turn-based).
    bool awaited() const { return human && alive && !aiControl && !submitted; }
};

struct TurnStatus {
    uint32_t turn = 0;
    bool processing = false;
    int32_t secondsLeft = -1;        // turn timeout countdown (turn-based: of the active player's turn); -1 = no timeout
    // Turn-based games (spec 05 §8): players take their turns one after
    // another and their commands are carried out at once.
    bool turnBased = false;
    game::EmpireId active;           // turn-based: whose turn is in progress (invalid: none; the host waits)
    std::vector<EmpireTurnStatus> empires;

    const EmpireTurnStatus* activeStatus() const {
        for (const EmpireTurnStatus& e : empires)
            if (e.active) return &e;
        return nullptr;
    }
};

// ---- Events --------------------------------------------------------------------------------

enum class EventType : uint8_t {
    Info,                // text: something worth logging
    Warning,             // text
    Error,               // text
    Listening,           // host: accepting connections (text: address)
    PortMapping,         // host: UPnP result or fallback advice (text)
    Connected,           // client: TCP connection made, handshaking
    Joined,              // client: the host accepted us (slot)
    Rejected,            // client: the host refused us (text: why); disconnected
    Disconnected,        // client: connection closed (text: why)
    PlayerJoined,        // player, slot
    PlayerReconnected,   // player, slot
    PlayerLeft,          // player, slot, text: why
    LobbyChanged,        // lobby() changed
    Chat,                // player, text
    GameStarted,         // turn; state() is available
    TurnStatusChanged,   // turnStatus() changed
    OrdersReceived,      // host: empire, player submitted orders for turn (turn-based: commands, text: how many)
    OrdersAccepted,      // client: the host stored our orders for turn
    OrdersRejected,      // client: text: why
    TurnProcessing,      // turn being processed
    NewTurn,             // turn: a new state() is available
    GameOver,            // empire: the winner, if any
    // Turn-based games.
    PlayerTurn,          // the turn passed: empire (invalid: none, the host waits), player, turn
    StateUpdated,        // a new state() within the same game turn (host: empire's view changed)
    CommandsDone,        // client: the host carried out our commands (request); text: the refusals, if any
    // Both.
    Desync,              // a player's copy of the game differed from the host's (turn, text: which parts); the host sent it again
};

std::string_view displayName(EventType t);

struct Event {
    EventType type = EventType::Info;
    std::string text;
    std::string player;
    uint32_t slot = kNoSlot;
    game::EmpireId empire;
    uint32_t turn = 0;
    uint32_t request = 0;            // CommandsDone: the number play() returned
};

// One line for logs: "[chat] alice: hello".
std::string describe(const Event& e);

} // namespace opense4::net
