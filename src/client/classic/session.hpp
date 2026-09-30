#pragma once

// A classic-rules game as the client sees it: the rules, the game state, the
// local player and the orders given this turn. Local and hotseat games own
// the authoritative state; a network client holds a copy and hands its
// orders to a TurnTransport (src/net) at End Turn.
//
// Every change the player makes goes through issue(): the command is
// validated and applied to the local state at once (so every window shows
// it immediately) and recorded for the turn.
//
// Turn-based games (GameOptions::simultaneous off) play one player after
// another: issue() carries each order out at once (game::applyLive: ships
// move, battles are fought, messages take effect), End Turn runs the
// player's end-of-turn processing and the computer players' turns, and the
// turn passes to the next human. In a network game the host does this: in
// the player's turn issue() checks the command on the local copy and sends
// it to the host (TurnTransport::playCommand), whose new state replaces the
// copy when it arrives.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

// Moves a player's orders to whoever processes turns, and brings back new
// states. Implemented over TCP by the multiplayer layer.
class TurnTransport {
public:
    virtual ~TurnTransport() = default;
    virtual void submitOrders(const game::EmpireOrders& orders) = 0;
    // Turn-based games: one command for the host to carry out now, and the
    // end of our turn.
    virtual void playCommand(const game::Command& c) { (void)c; }
    virtual void endPlayerTurn() {}
    // Returns a newly processed state when one has arrived (turn-based games:
    // also within a turn).
    virtual std::optional<game::GameState> pollState() = 0;
    virtual std::string status() const = 0;
};

enum class SessionKind { Local, Hotseat, NetworkClient };

class ClassicSession {
public:
    ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player,
                   SessionKind kind = SessionKind::Local);

    const game::Rules& rules() const { return *rules_; }
    std::shared_ptr<const game::Rules> rulesPtr() const { return rules_; }
    const game::GameState& state() const { return state_; }
    game::EmpireId player() const { return player_; }
    const game::Empire& me() const { return state_.empire(player_); }
    SessionKind kind() const { return kind_; }

    // Bumped on every state change; windows use it to refresh cached views.
    uint64_t revision() const { return revision_; }

    // Validates and applies a command for the local player (turn-based games:
    // and carries it out).
    game::CommandResult issue(game::Command c);
    const std::vector<game::Command>& ordersThisTurn() const { return orders_; }

    bool turnBased() const { return game::turnBased(state_); }
    // Turn-based games: the local player's turn is in progress.
    bool myTurn() const;
    // Turn-based games: moves of the player's ships that stopped before a
    // sector with enemies, oldest first; answer() gives the Attack Sector
    // answer to the first (spec 03 §6.2).
    const std::vector<game::EntryQuestion>& questions() const;
    void answer(bool enter);
    // Turn-based games: the first battle the player's last order started, to
    // show at once (an index into GameState::combats), then forgotten.
    std::optional<size_t> takeNewBattle();

    // Ends the local player's turn. Local: every computer empire plays and the
    // turn is processed at once. Hotseat: moves to the next human who has not
    // ended the turn, processing when all have. Network: sends the orders.
    void endTurn();
    bool waitingForOthers() const { return waiting_; }
    // Call every frame: picks up new states from the transport.
    void poll();
    void setTransport(std::unique_ptr<TurnTransport> transport) { transport_ = std::move(transport); }
    const TurnTransport* transport() const { return transport_.get(); }
    TurnTransport* transport() { return transport_.get(); }

    // Messages the engine produced for the player on the last turn (rejected orders).
    const std::vector<std::string>& notices() const { return notices_; }
    // Called after a new turn begins (for the Log window auto-open etc.).
    std::function<void()> onNewTurn;

    // Saves and loads (the .gam equivalent).
    std::expected<void, std::string> save(const std::filesystem::path& file, const std::string& gameName) const;
    // The game's Autosave choice (spec 01 §2.2), applied after a turn has been
    // processed here (local and hotseat games): every N turns into one of ten
    // rotating slots in the saves folder. Returns what was written, if anything.
    std::optional<std::filesystem::path> autosave();
    // Where the last autosave went, or why it failed (for the status line).
    const std::string& autosaveNote() const { return autosaveNote_; }
    static std::expected<std::unique_ptr<ClassicSession>, std::string> load(std::shared_ptr<const game::Rules> rules,
                                                                            const std::filesystem::path& file);

    // Hotseat: switches the local player (after a password check by the UI).
    void setPlayer(game::EmpireId e);
    // Replaces the state (e.g. after loading or a network update).
    void replaceState(game::GameState s);
    // Automation: the computer plays every empire (humans too) for n turns.
    void simulateTurns(int n);

private:
    void beginTurn();
    // Turn-based games: plays up to a human player's turn and hands the
    // session to that player.
    void resumeTurnBased();
    void takeResult(const game::TurnResult& result);

    std::shared_ptr<const game::Rules> rules_;
    game::GameState state_;
    game::EmpireId player_;
    SessionKind kind_;
    uint64_t revision_ = 1;
    std::vector<game::Command> orders_;
    std::vector<uint8_t> ended_;  // hotseat: humans who ended this turn
    bool waiting_ = false;
    std::unique_ptr<TurnTransport> transport_;
    std::vector<std::string> notices_;
    std::string autosaveNote_;
    std::optional<size_t> newBattle_;
};

// Where OpenSE4 keeps saves and settings (created on demand).
std::filesystem::path userDataDir();
std::filesystem::path savesDir();

} // namespace opense4::client::classic
