#pragma once

// Turn processing (docs/spec/05 §8). All empires' command lists are applied,
// then the phases run in a fixed order. Deterministic: the same state and
// orders give the same result on every machine.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <string>
#include <vector>

namespace opense4::game {

// Something that changes population mood (Happiness.txt triggers, spec 02 §4).
struct MoodEvent {
    EmpireId empire;           // whose population reacts
    std::string trigger;       // Happiness.txt trigger identifier
    SystemId system;           // where it happened (invalid = empire-wide)
    ObjectId planet;           // the planet it happened at (optional)
    int count = 1;
};

// Transient data passed between the phases of one turn. Nothing here
// survives the turn; persistent results go into GameState.
struct TurnContext {
    const Rules& rules;
    GameState& state;

    std::vector<MoodEvent> moodEvents;
    std::vector<Location> battleSites;          // sectors where space combat happened
    std::vector<std::pair<EmpireId, std::string>> rejected;  // commands refused

    void mood(EmpireId e, std::string trigger, SystemId sys = {}, ObjectId planet = {}, int count = 1) {
        moodEvents.push_back({e, std::move(trigger), sys, planet, count});
    }
    void log(EmpireId e, LogCategory c, std::string title, std::string text = {}, std::optional<Location> where = std::nullopt,
             std::string picture = {}) {
        addLog(state, e, c, std::move(title), std::move(text), where, std::move(picture));
    }
};

struct TurnOptions {
    // Empires that sent no orders are played by the computer (spec 05 §9.2).
    bool aiForMissing = true;
};

struct TurnResult {
    std::vector<std::pair<EmpireId, std::string>> rejected;
};

// Processes one full turn: applies orders, runs every phase, advances the date.
TurnResult processTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options = {});

// Applies one empire's command list, collecting rejections.
void applyOrders(const Rules& r, GameState& s, const EmpireOrders& orders, std::vector<std::pair<EmpireId, std::string>>& rejected);

} // namespace opense4::game
