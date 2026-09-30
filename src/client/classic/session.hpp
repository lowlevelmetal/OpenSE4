#pragma once

// A classic-rules game as the client sees it: the rules, the game state, the
// local player and the orders given this turn. Local and hotseat games own
// the authoritative state; a network client holds a copy and hands its
// orders to a TurnTransport (src/net) at End Turn.
//
// Every change the player makes goes through issue(): the command is
// validated and applied to the local state at once (so every window shows
// it immediately) and recorded for the turn.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace opense4::client::classic {

// Moves a player's orders to whoever processes turns, and brings back new
// states. Implemented over TCP by the multiplayer layer.
class TurnTransport {
public:
    virtual ~TurnTransport() = default;
    virtual void submitOrders(const game::EmpireOrders& orders) = 0;
    // Returns a newly processed state when one has arrived.
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

    // Validates and applies a command for the local player.
    game::CommandResult issue(game::Command c);
    const std::vector<game::Command>& ordersThisTurn() const { return orders_; }

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
};

// Where OpenSE4 keeps saves and settings (created on demand).
std::filesystem::path userDataDir();
std::filesystem::path savesDir();

} // namespace opense4::client::classic
