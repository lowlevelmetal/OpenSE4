#pragma once

// The host of a network game (docs/spec/05 §9.4, docs/MULTIPLAYER.md): runs
// the lobby, creates the game, collects every human empire's orders for the
// turn, processes the turn (the computer plays empires whose orders are
// missing) and sends the new state to everyone. The host is authoritative.
//
// Turn-based games (GameOptions::simultaneous off, spec 05 §8): the host
// runs the game one player turn after another. Only the player whose turn it
// is may act: its commands are carried out on the host at once
// (game::applyLive) and it gets its new view after each; End Turn runs
// game::endPlayerTurn, the computer players' turns run here, and everyone
// gets their view when the turn passes on. A player who is away is waited
// for until the turn time limit, a forced turn or a hand-over to the
// computer, which then plays the rest of that turn (as for missing orders).
//
// Single-threaded: call poll() regularly (every frame in a UI, or in a loop
// with a timeout in the dedicated server); everything happens inside poll()
// and the methods below. Not thread-safe.

#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"
#include "net/crypto.hpp"
#include "net/discovery.hpp"
#include "net/socket.hpp"
#include "net/types.hpp"
#include "net/upnp.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opense4::net {

namespace proto {
struct BaseState;
}

// In-game hosting: the host is also a player (lobby slot of its own).
struct LocalPlayer {
    std::string name;
    std::string password;              // the host's own player's password (optional; Argon2id at start)
    game::EmpireSetup setup;
};

struct HostConfig {
    std::string gameName = "OpenSE4 game";
    std::string bindAddress;           // empty: every IPv4 interface
    uint16_t port = kDefaultPort;      // 0: pick a free port (see HostSession::port)
    int humanSlots = 2;                // human players, the local player included
    std::optional<LocalPlayer> localPlayer;
    game::GameSetup setup;             // seed and options; the empires come from the lobby
    std::string joinPassword;          // the password needed to join; empty: open game
    std::string masterPassword;        // the master password: grants remote admin rights (empty: none)
    std::string masterPasswordVerifier;  // instead: its verifier in this game (net::passwordVerifier, with gameId)
    uint64_t gameId = 0;               // the new game's id, which salts its passwords; 0: a random one
    std::string dataSet;               // empty: dataSetIdentity() of the rules
    // The host's long-term key (secure::loadOrCreateHostKey), which players
    // pin; none: a new one for this session only.
    std::optional<crypto::KeyPair> hostKey;
    // A game of OpenSE4 0.6 (resume): a player whose password is still in that
    // version's form may move it to the current form by showing the old
    // form's hash once (with consent, to a host whose key the player trusts).
    // False: refused; such players get a password only by Reset Passwords.
    bool passwordMigration = true;
    bool autoStart = false;            // start as soon as every human slot is taken and ready
    int turnTimeoutSeconds = 0;        // process the turn after this long even if orders are missing; 0: wait
    PortMapperOptions upnp;            // UPnP port mapping (on by default)
    bool lanDiscovery = true;          // answer LAN discovery queries (net/discovery.hpp)
    uint16_t discoveryPort = kDiscoveryPort;
    size_t maxOrdersBytes = size_t{16} << 20;  // largest message a joined client may send
    size_t maxConnections = 64;
    int handshakeTimeoutSeconds = 10;
    int keepaliveSeconds = 5;
    int timeoutSeconds = 60;           // drop a client that sent nothing for this long
};

enum class HostPhase : uint8_t { Stopped, Lobby, Playing, GameOver };

class HostSession {
public:
    HostSession(const game::Rules& rules, HostConfig config);
    ~HostSession();
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    // Opens the lobby for a new game.
    std::expected<void, std::string> start();
    // Continues a saved game: players reconnect by name and password. A save
    // with a master password needs config.masterPassword (or the same
    // masterPasswordVerifier) to match.
    std::expected<void, std::string> resume(game::GameState state, const game::SaveInfo& info);
    // Says goodbye to everyone, removes the UPnP mapping and closes the port.
    void stop(std::string_view reason = "The host closed the game.");

    // Handles the network and timers; waits up to timeoutMs for activity.
    std::vector<Event> poll(int timeoutMs = 0);

    HostPhase phase() const { return phase_; }
    uint16_t port() const { return port_; }
    const HostConfig& config() const { return config_; }
    PortMapStatus portMapping() const { return mapper_.status(); }
    // How this game appears to LAN discovery.
    LanGame lanGame() const;
    bool lanDiscoveryRunning() const { return discovery_.running(); }
    // The public half of the host's long-term key, and its fingerprint as
    // players compare it (crypto::fingerprint).
    const crypto::Key& hostKey() const { return hostKey_.publicKey; }
    // The game's id (salts its passwords); 0 before start() or resume().
    uint64_t gameId() const { return gameId_; }
    std::string hostFingerprint() const { return crypto::fingerprint(hostKey_.publicKey); }
    const LobbyInfo& lobby() const { return lobby_; }

    // ---- Lobby ------------------------------------------------------------------------------
    std::expected<uint32_t, std::string> addComputerEmpire(game::EmpireSetup setup = {});
    std::expected<void, std::string> removeSlot(uint32_t slot);
    // Disconnects a player (in the lobby their slot opens up) and refuses the
    // name from then on. In a game their empire is handed to the computer.
    std::expected<void, std::string> kick(uint32_t slot, std::string reason = {});
    // Setup of the local player's slot, or of a computer slot.
    std::expected<void, std::string> setSlotSetup(uint32_t slot, game::EmpireSetup setup);
    std::expected<void, std::string> setLocalReady(bool ready);
    uint32_t localSlot() const;
    // Why the game cannot start yet (empty: it can).
    std::string startProblem(bool force = false) const;
    // Creates the game (open human slots are dropped) and sends it out.
    // force: start even if some players are not ready.
    std::expected<void, std::string> startGame(bool force = false);
    // For tests and tools: runs on the new game before it is sent out, so
    // every player receives the changed state.
    void setGameCreatedHook(std::function<void(game::GameState&)> hook) { gameCreated_ = std::move(hook); }

    // ---- Game -------------------------------------------------------------------------------
    const game::GameState* state() const { return state_ ? &*state_ : nullptr; }
    game::EmpireId localEmpire() const;
    game::EmpireId empireOfSlot(uint32_t slot) const;
    const TurnStatus& turnStatus() const { return turnStatus_; }
    // Orders for any empire (the host may play any empire's turn).
    std::expected<void, std::string> submitOrders(game::EmpireOrders orders);
    // Processes the turn now, with the computer playing missing empires.
    // Turn-based games: ends the turn in progress, the computer playing the
    // rest of it; with no turn in progress (every human is played by the
    // computer) plays one game turn.
    std::expected<void, std::string> processTurnNow();

    // ---- Turn-based games ----------------------------------------------------------------------
    bool turnBased() const;
    // Whose turn is in progress (invalid: none, the host waits).
    game::EmpireId activeEmpire() const;
    // Carries out commands for the empire whose turn it is, one after another
    // (the host's own player, or any player the host plays for).
    std::expected<game::TurnResult, std::string> playCommands(game::EmpireId empire, std::vector<game::Command> commands);
    // Ends that empire's turn; computer players move and the next player's turn starts.
    std::expected<void, std::string> endPlayerTurn(game::EmpireId empire);
    void setTurnTimeout(int seconds);
    // "Toggle Empire AI On/Off" (spec 05 §9.4): flips only the empire's
    // computer-controlled mark (the host stops waiting for its orders, and
    // the computer plays it every turn) or hands it back to its player.
    std::expected<void, std::string> setAiControl(game::EmpireId empire, bool ai);

    // ---- Reset Passwords (spec 06 §1.9, spec 05 §9.2, confirmed: binary) -------------------------
    struct PasswordReset {
        game::EmpireId empire;
        std::string password;
        std::string verifier;   // its verifier in this game (made at once: the host runs Argon2id now, not at the turn)
    };
    // The host of a simultaneous game gives each listed empire a new six-digit
    // password (net::resetPassword, not the game's random numbers). Every reset
    // chosen earlier and not yet applied is discarded first. The passwords are
    // written into the empires when the host next processes a turn, after the
    // orders are read, and are kept nowhere else: not saved, lost if the host
    // stops first. Returns them, to show to the host only.
    std::expected<std::vector<PasswordReset>, std::string> resetPasswords(const std::vector<game::EmpireId>& empires);
    // Discards the resets not yet applied (a new Reset Passwords click does).
    void clearPasswordResets() { resets_.clear(); }
    const std::vector<PasswordReset>& pendingPasswordResets() const { return resets_; }

    void chat(std::string_view text);

    game::SaveInfo saveInfo() const;
    std::expected<void, std::string> save(const std::filesystem::path& file) const;

    struct Peer;
    struct Slot;

private:
    void emit(EventType type, std::string text = {}, std::string player = {}, uint32_t slot = kNoSlot, game::EmpireId empire = {},
              uint32_t turn = 0);
    std::expected<void, std::string> openPort();
    void acceptPeers();
    void handleFrame(Peer& peer, uint8_t type, std::span<const uint8_t> payload, bool sealed);
    void handleClientHello(Peer& peer, std::span<const uint8_t> payload);
    void handleLogin(Peer& peer, std::span<const uint8_t> payload);
    void handleAdmin(Peer& peer, std::span<const uint8_t> payload);
    void handleOrders(Peer& peer, std::span<const uint8_t> payload);
    void handlePlay(Peer& peer, std::span<const uint8_t> payload);
    void handleEndTurn(Peer& peer, std::span<const uint8_t> payload);
    void reject(Peer& peer, uint8_t reason, std::string text);
    // Compares a player's copy of the game with the State last sent to it;
    // on a difference tells the player which parts differ, logs it and sends
    // the state again.
    void checkBase(Peer& peer, const proto::BaseState& base);
    void dropPeer(Peer& peer, std::string reason, bool sayBye);
    void peerGone(Peer& peer, const std::string& reason);
    void runTimers();
    void flushAll();

    Slot* findSlot(uint32_t id);
    const Slot* findSlot(uint32_t id) const;
    Slot* slotOfPeer(const Peer& peer);
    Peer* peerOfSlot(const Slot& slot);
    size_t slotIndex(const Slot& slot) const;
    void refreshLobby();
    void broadcastLobby();
    void refreshTurnStatus();
    void broadcastTurnStatus();
    void sendState(Peer& peer, bool gameStart, bool resync = false);
    void broadcastState(bool gameStart);
    void beginTurn();
    bool allOrdersIn() const;
    using StateBlob = std::shared_ptr<const std::vector<uint8_t>>;
    std::vector<StateBlob> redactedState() const;
    void notifyPlayer(game::EmpireId empire, const std::string& text);
    void notifyMessages(const game::TurnResult& result);
    void notifyRejections(const game::TurnResult& result, uint32_t turn);
    // Turn-based games.
    game::LiveOptions liveOptions() const;
    bool anyHumanToPlay() const;
    std::string playerName(game::EmpireId empire) const;
    game::TurnResult runLive(game::EmpireId empire, const std::vector<game::Command>& commands);
    std::expected<void, std::string> turnBasedStep(const std::function<game::TurnResult()>& step);
    std::expected<void, std::string> skipPlayerTurn();

    const game::Rules& rules_;
    HostConfig config_;
    HostPhase phase_ = HostPhase::Stopped;
    uint16_t port_ = 0;
    uint64_t gameId_ = 0;
    crypto::KeyPair hostKey_;
    crypto::Key joinKey_{};             // net::joinKey of the join password, made at start
    std::string masterVerifier_;        // the master password's verifier in this game (empty: none)
    uint32_t nextSerial_ = 1;           // State::serial
    Socket listener_;
    PortMapper mapper_;
    DiscoveryResponder discovery_;
    NativeSocket discoveryNative() const { return discovery_.native(); }
    std::vector<std::unique_ptr<Peer>> peers_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<std::string> banned_;
    uint32_t nextSlotId_ = 0;
    uint64_t nextPeerId_ = 1;
    LobbyInfo lobby_;
    TurnStatus turnStatus_;
    std::optional<game::GameState> state_;
    std::function<void(game::GameState&)> gameCreated_;
    std::vector<std::optional<game::EmpireOrders>> orders_;
    std::optional<std::chrono::steady_clock::time_point> deadline_;
    std::vector<StateBlob> stateCache_;  // per-empire views of the current turn (+ spectator)
    std::vector<Event> events_;
    std::vector<PasswordReset> resets_;  // Reset Passwords not applied yet (never saved)
};

} // namespace opense4::net
