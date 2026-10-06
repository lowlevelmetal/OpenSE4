#pragma once

// A headless game between computer players, played to its end or a turn
// limit with the engine's own turn processing, and what each player did:
// the arena's games, the games of opense4-sdk test, the training
// environment's (docs/sdk/bots-and-arena.md).
//
// Every empire is a computer empire played by its seat's controller: the
// built-in AI, a mod's player, or an external bot. A seat with a bot command
// gets a bot of its own, started for the game with what it needs to connect
// in its environment (OPENSE4_BOT_HOST, OPENSE4_BOT_PORT, OPENSE4_BOT_TOKEN,
// OPENSE4_BOT_SLOT), and ended after it. The game is deterministic for its
// seed and its players' answers; the times measured are not part of it.

#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"
#include "sdk/players.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::sdk {

class BotHost;

struct Seat {
    game::Controller controller;   // builtin, "<mod>:<player>", or external
    // An external seat's bot, as a command line for the system's shell,
    // started for this game. Empty: a bot connects by itself (the
    // training environment's).
    std::string botCommand;
};

struct MatchSetup {
    // Seed, options and empires; each empire is played by the seat of the
    // same number (computer-controlled whatever its kind says).
    game::GameSetup game;
    std::vector<Seat> seats;
    uint32_t turns = 100;          // game turns to play at most
    // The sessions' setup: another package, more mods (tests), an observer
    // of its own (called after the match's own).
    PlayerSetup players;
    // External bots: the host they connect to (null: the match opens one
    // on this computer when a seat needs it), how long each request and the
    // first connection may take, what the bots' environment adds
    // (PYTHONPATH), and where each one's output goes (<dir>/bot-<slot>.log;
    // empty: discarded).
    BotHost* bots = nullptr;
    std::chrono::milliseconds botTimeout{60'000};
    std::chrono::milliseconds botConnectTimeout{30'000};
    std::vector<std::pair<std::string, std::string>> botEnvironment;
    std::filesystem::path botLogDir;
    // After each game turn, with the state (progress, the environment's own
    // checks); false ends the match there.
    std::function<bool(const game::GameState&)> afterTurn;
};

// One seat at the end of one game turn.
struct SeatTurn {
    uint32_t turn = 0;   // the game turn played (GameState::turn before it)
    bool alive = true;
    int64_t score = 0;
    int colonies = 0;
    int systems = 0;
    int planets = 0;
    int64_t population = 0;
    int ships = 0;
    int bases = 0;
    int units = 0;
    int techLevels = 0;
    int64_t research = 0;    // research points produced in the turn
    int battlesWon = 0;      // in the turn
    int battlesLost = 0;
    int battlesDrawn = 0;
};

struct SeatResult {
    game::EmpireId empire;
    std::string name;
    std::string race;
    std::vector<SeatTurn> turns;
    int battlesWon = 0;
    int battlesLost = 0;
    int battlesDrawn = 0;
    std::optional<uint32_t> eliminated;   // the game turn it was destroyed in
    // Its player: requests asked, given again from the journal, failed, and
    // decisions the classic AI made instead (failures, and requests skipped
    // after three failures in a turn); time its player took.
    int64_t requests = 0;
    int64_t replayed = 0;
    int64_t failures = 0;
    int64_t fallbacks = 0;
    std::chrono::nanoseconds playerTime{};
    // The most its player took: time in one game turn (all its requests of the
    // turn together), bytecodes in one planning request and in one other
    // request (script players).
    std::chrono::nanoseconds turnTimeMax{};
    int64_t planningBudgetMax = 0;
    int64_t callBudgetMax = 0;
    std::vector<std::string> errors;   // the first failures, as logged
};

struct MatchResult {
    uint64_t seed = 0;
    uint32_t turnsPlayed = 0;
    bool gameOver = false;        // a victory condition ended it
    game::EmpireId winner;
    std::string winnerBy;         // "victory", "last standing" or "score"
    uint64_t checksum = 0;        // game::stateChecksum of the final state
    std::vector<SeatResult> seats;
    game::GameState state;        // the final state
    std::chrono::nanoseconds time{};
};

// Plays the match. Fails when the game cannot be made or a bot never connects.
// Installs the SDK's sessions for its length (sdk::installPlayers), and
// removes them after.
std::expected<MatchResult, std::string> playMatch(const game::Rules& r, MatchSetup setup);

// Who won each battle of one game turn, per empire: a side that keeps
// pieces while every other side lost all of theirs won; one that lost all
// of its pieces while another kept some lost; otherwise drawn (inferred:
// the arena's own measure).
struct BattleOutcome {
    game::EmpireId empire;
    int won = 0, lost = 0, drawn = 0;
};
std::vector<BattleOutcome> battleOutcomes(const game::GameState& s, uint32_t turn);

} // namespace opense4::sdk
