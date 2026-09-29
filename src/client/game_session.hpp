#pragma once

#include "sim/commands.hpp"
#include "sim/setup.hpp"
#include "sim/state.hpp"

#include <string>
#include <vector>

namespace opense4::client {

// What the player has selected on the maps / in the UI.
struct Selection {
    enum class Kind { None, System, Planet, Ship, WarpPoint };
    Kind kind = Kind::None;
    sim::SystemId system;
    sim::PlanetId planet;
    sim::ShipId ship;
    sim::WarpPointId warpPoint;

    static Selection ofSystem(sim::SystemId s) { return {Kind::System, s, {}, {}, {}}; }
    static Selection ofPlanet(const sim::Planet& p) { return {Kind::Planet, p.system, p.id, {}, {}}; }
    static Selection ofShip(const sim::Ship& s) { return {Kind::Ship, s.location.system, {}, s.id, {}}; }
    static Selection ofWarpPoint(const sim::WarpPoint& w) { return {Kind::WarpPoint, w.system, {}, {}, w.id}; }
    bool operator==(const Selection&) const = default;
};

struct LogEntry {
    uint32_t turn = 0;
    sim::GameEvent event;
};

// The running game from the local player's point of view: owns the rules and
// state, routes the player's commands, and keeps the message log.
class GameSession {
public:
    GameSession(sim::Content content, sim::NewGame game);

    const sim::Content& content() const { return content_; }
    const sim::GameState& state() const { return state_; }
    sim::EmpireId player() const { return player_; }
    const sim::Empire& playerEmpire() const { return state_.empire(player_); }

    // Applies a command for the player. On failure the reason is shown to the player.
    bool issue(const sim::Command& command);
    void endTurn();
    // Lets the AI run the player's empire for `turns` turns (testing, screenshots).
    void autoplay(int turns);

    // Visibility rules for the local player.
    bool explored(sim::SystemId s) const { return playerEmpire().hasExplored(s); }
    bool seesShipsIn(sim::SystemId s) const;
    bool canSee(const sim::Ship& ship) const { return ship.owner == player_ || seesShipsIn(ship.location.system); }

    // Player ships without orders, in id order.
    std::vector<const sim::Ship*> idleShips() const;

    const std::vector<LogEntry>& log() const { return log_; }
    size_t newEventCount() const { return newEvents_; }
    void markEventsRead() { newEvents_ = 0; }

    const std::string& statusMessage() const { return status_; }
    double statusTime() const { return statusTime_; }
    void setStatus(std::string message);

private:
    void collectEvents();

    sim::Content content_;
    sim::GameState state_;
    sim::EmpireId player_;
    std::vector<LogEntry> log_;
    size_t newEvents_ = 0;
    std::string status_;
    double statusTime_ = -100.0;
};

} // namespace opense4::client
