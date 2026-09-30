#pragma once

// A player's connection to a network game (docs/MULTIPLAYER.md): joins the
// lobby, submits the empire setup and ready flag, receives the game state
// every turn and sends the player's orders. Reconnecting (connect() again,
// same name and password) resumes the current turn.
//
// Single-threaded: call poll() every frame; everything happens inside poll()
// and the methods below. Not thread-safe.

#include "game/commands.hpp"
#include "game/state.hpp"
#include "net/types.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opense4::net {

struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = kDefaultPort;
    std::string playerName;
    std::string passwordHash;          // hashPassword() of the player's password (optional)
    std::string joinPasswordHash;      // hashPassword() of the game password, if the host set one
    std::string masterPasswordHash;    // hashPassword() of the master password: admin rights (optional)
    std::string dataSet;               // game::dataSetIdentity() of the local rules
    size_t maxMessageBytes = size_t{512} << 20;  // largest state accepted from the host
    int connectTimeoutSeconds = 10;
    int keepaliveSeconds = 5;
    int timeoutSeconds = 90;           // the host may be busy processing a turn
};

enum class ClientPhase : uint8_t { Disconnected, Connecting, Handshaking, Lobby, Playing };

class ClientSession {
public:
    explicit ClientSession(ClientConfig config);
    ~ClientSession();
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    // Starts connecting (or reconnecting); poll() completes it.
    std::expected<void, std::string> connect();
    // Leaves politely.
    void disconnect(std::string_view reason = "Left the game.");

    std::vector<Event> poll(int timeoutMs = 0);

    ClientPhase phase() const { return phase_; }
    ClientConfig& config() { return config_; }
    bool admin() const { return admin_; }
    uint32_t slot() const { return slot_; }
    const std::string& gameName() const { return gameName_; }
    uint64_t gameId() const { return gameId_; }
    const LobbyInfo& lobby() const { return lobby_; }
    const TurnStatus& turnStatus() const { return turnStatus_; }
    // The latest game state from the host (redacted: no password data).
    const game::GameState* state() const { return state_ ? &*state_ : nullptr; }
    game::GameState* mutableState() { return state_ ? &*state_ : nullptr; }
    game::EmpireId empire() const { return empire_; }
    // The host has stored our orders for the current turn.
    bool ordersAccepted() const { return ordersAccepted_; }

    // ---- Lobby ------------------------------------------------------------------------------
    void submitSetup(const game::EmpireSetup& setup);
    void setReady(bool ready);

    // ---- Game -------------------------------------------------------------------------------
    // Sends this turn's orders (again, to replace earlier ones). empire and
    // turn are filled in when left default.
    std::expected<void, std::string> submitOrders(game::EmpireOrders orders);
    void chat(std::string_view text);

    // ---- Administration (needs the master password) ------------------------------------------
    void requestStart(bool force = false);
    void requestAddComputer(const game::EmpireSetup& setup = {});
    void requestRemoveSlot(uint32_t slot);
    void requestKick(uint32_t slot, std::string_view reason = {});
    void requestProcessTurn();
    void requestAiControl(game::EmpireId empire, bool ai);
    void requestTurnTimeout(int seconds);

    struct Impl;

private:
    void emit(EventType type, std::string text = {}, std::string player = {}, uint32_t slot = kNoSlot, game::EmpireId empire = {},
              uint32_t turn = 0);
    void handleFrame(uint8_t type, std::span<const uint8_t> payload);
    void closeConnection(std::string reason, bool rejected = false);

    ClientConfig config_;
    std::unique_ptr<Impl> impl_;
    ClientPhase phase_ = ClientPhase::Disconnected;
    bool admin_ = false;
    uint32_t slot_ = kNoSlot;
    std::string gameName_;
    uint64_t gameId_ = 0;
    LobbyInfo lobby_;
    TurnStatus turnStatus_;
    std::optional<game::GameState> state_;
    game::EmpireId empire_;
    bool ordersAccepted_ = false;
    std::vector<Event> events_;
};

} // namespace opense4::net
