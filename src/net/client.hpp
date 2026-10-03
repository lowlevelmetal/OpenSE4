#pragma once

// A player's connection to a network game (docs/MULTIPLAYER.md): joins the
// lobby, submits the empire setup and ready flag, receives the game state
// every turn and sends the player's orders. Reconnecting (connect() again,
// same name and password) resumes the current turn: orders given while the
// connection was down, or sent but never acknowledged, go out again, and
// the host recognizes a turn-based request it has already carried out.
//
// The connection is encrypted (net/secure.hpp). The host's long-term key is
// checked against ClientConfig::hostKey when one is given; otherwise the
// first key seen is kept for the reconnects of this session.
//
// Turn-based games (turnBased()): in the player's own turn (myTurn()) each
// command goes to the host with play(), which carries it out at once and
// sends back the new state (StateUpdated) and the result (CommandsDone);
// endTurn() passes the turn on. The state also arrives when the turn passes
// (PlayerTurn) and when a battle the player fought in someone else's turn
// changed it.
//
// Single-threaded: call poll() every frame; everything happens inside poll()
// and the methods below. Not thread-safe.

#include "game/commands.hpp"
#include "game/state.hpp"
#include "net/crypto.hpp"
#include "net/types.hpp"

#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    // The host key this player trusts for the host (secure::KnownHosts); a
    // host with another key is refused. None: trust the first key seen, which
    // is then kept here for reconnects.
    std::optional<crypto::Key> hostKey;
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
    // Drops the connection at once, as a network failure does: what was
    // queued is not sent (for tests and tools).
    void dropConnection(std::string_view reason = "The connection was lost.");

    std::vector<Event> poll(int timeoutMs = 0);

    ClientPhase phase() const { return phase_; }
    ClientConfig& config() { return config_; }
    // The key the host showed on the last connection (even one refused), and
    // why that connection was refused: the key is not ClientConfig::hostKey
    // (changed), or the host asked for the password hash of an OpenSE4 0.6
    // game while its key is not one the player trusted beforehand and no
    // join password vouches for it (unconfirmed). Either way the player may
    // compare the key with the host's and trust it.
    const std::optional<crypto::Key>& seenHostKey() const { return seenHostKey_; }
    bool hostKeyChanged() const { return hostKeyChanged_; }
    bool hostKeyUnconfirmed() const { return hostKeyUnconfirmed_; }
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

    // ---- Turn-based games ---------------------------------------------------------------------
    bool turnBased() const { return turnStatus_.turnBased || (state_ && !state_->options.simultaneous); }
    // Whose turn is in progress, as the host last said (invalid: none).
    game::EmpireId activeEmpire() const { return turnStatus_.active; }
    // Our turn is in progress: we may play commands and end it.
    bool myTurn() const;
    // Sends commands for the host to carry out now, one after another. Returns
    // the request number the CommandsDone event carries (after the new state).
    std::expected<uint32_t, std::string> play(std::vector<game::Command> commands);
    std::expected<uint32_t, std::string> play(game::Command command);
    // Ends our turn.
    std::expected<uint32_t, std::string> endTurn();
    // Requests sent (or waiting for the connection) that the host has not answered yet.
    size_t pendingRequests() const { return unanswered_.size(); }
    // Our open Attack Sector questions (spec 03 §6.2), oldest first: answer
    // each with play(cmd::EnterSector{...}).
    std::span<const game::EntryQuestion> questions() const;

    // ---- Administration (needs the master password) ------------------------------------------
    void requestStart(bool force = false);
    void requestAddComputer(const game::EmpireSetup& setup = {});
    void requestRemoveSlot(uint32_t slot);
    void requestKick(uint32_t slot, std::string_view reason = {});
    void requestProcessTurn();
    void requestAiControl(game::EmpireId empire, bool ai);
    void requestTurnTimeout(int seconds);
    // Reset Passwords (spec 06 §1.9) for these empires; the host answers with
    // one "Password Reset" notice per empire, to us only.
    void requestPasswordReset(const std::vector<game::EmpireId>& empires);

    struct Impl;

private:
    void emit(EventType type, std::string text = {}, std::string player = {}, uint32_t slot = kNoSlot, game::EmpireId empire = {},
              uint32_t turn = 0);
    void handleFrame(uint8_t type, std::span<const uint8_t> payload, bool sealed);
    void handleServerHello(std::span<const uint8_t> payload);
    void closeConnection(std::string reason, bool rejected = false);
    bool connected() const;
    // Sends lastOrders_ with our copy of the state as it is now.
    void sendOrders();
    // Turn-based: whether a command may be queued while the connection is down.
    bool myTurnInCopy() const;

    struct Request {
        uint8_t type = 0;
        uint32_t request = 0;
        std::vector<uint8_t> payload;
    };

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
    std::optional<game::EmpireOrders> lastOrders_;  // simultaneous: this turn's orders, sent again after a reconnect
    uint64_t clientId_ = 0;                         // Login::clientId
    uint32_t nextRequest_ = 1;
    std::deque<Request> unanswered_;                // turn-based: requests without a PlayResult yet, in order
    bool resendAfterState_ = false;                 // a new connection: send again what got no answer
    uint32_t stateSerial_ = 0;                      // State::serial of state_
    std::optional<crypto::Key> seenHostKey_;
    bool hostKeyChanged_ = false;
    bool hostKeyUnconfirmed_ = false;
    bool keyPinnedBySession_ = false;               // config_.hostKey came from a Welcome, not from the player
    std::vector<Event> events_;
};

} // namespace opense4::net
