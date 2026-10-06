#pragma once

// The engine's side of script and external computer players
// (docs/sdk/ai-protocol.md, docs/MODDING_SDK.md §6). The game asks an
// empire's controller (Empire::controller) at every decision the built-in AI
// makes, through a Players session made for one engine call; the modding
// SDK (src/sdk) provides it, so the game itself stays free of scripts.
//
// A session exists only when the game has a living computer empire whose
// controller is not the built-in AI and a factory is installed
// (setPlayersFactory; the client, the server and the tests install the SDK's).
// Without one every empire is played by the built-in AI.
//
// For an empire its player plays (playedByController):
//   - the built-in AI's own steps do not run: the AI state machine
//     (ai::updateAiState), the political step and its marks (anger), the
//     territory claims (ai::claimTerritory), the decisions and counters
//     ai::recordAiDecisions keeps and the memory of the turn
//     (ai::rememberAiEvents). What applies to every computer player still
//     does: its difficulty is assigned, and the computer bonuses apply;
//   - every decision goes to the player first. An answer of "nothing" (null)
//     or a failed request falls back to the classic answer for that decision
//     (the planners, colonyTypeAtColonization, entering, the Ship Cloaking
//     minister, the combat strategies); after three failures in a game turn
//     the classic answers stand for the rest of that turn.

#include "game/commands.hpp"
#include "game/hooks.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::game {

struct TurnContext;

// The text form of a controller, as setup files and the command line write
// it: "builtin", "<mod id>:<player>", or "external:<slot>".
std::string controllerText(const Controller& c);
// Reads that form; nullopt when it is none of them.
std::optional<Controller> parseController(std::string_view text);

// Whether the empire is played by a script or external player: a living
// computer-controlled empire (not human) whose controller is not the built-in AI.
bool hasPlayerController(const GameState& s, EmpireId e);
// The same, in an engine call that has a session: the empire's decisions
// go to its player, and the built-in AI's own steps do not run for it.
bool playedByController(const TurnContext& ctx, EmpireId e);
// Failures of a player in a game turn after which the classic answers
// stand for the rest of that turn (docs/sdk/ai-protocol.md §7).
inline constexpr int kPlayerFailuresPerTurn = 3;

// The planning calls (docs/sdk/ai-protocol.md §3).
enum class PlanCall : uint8_t { Politics, Orders, Economy, Count };
std::string_view callName(PlanCall c);   // "politics", "orders", "economy"

// How the engine carries out a planning call's commands at this moment of
// the turn: each one as it comes (`apply`: game::apply, plus what this
// moment notes about it), then, once, what the moment does with orders given
// (`settle`: messages delivered, moves carried out). The classic planner's
// commands go the same way.
struct CommandSink {
    std::function<CommandResult(const Command&)> apply;
    std::function<void()> settle;
};

// A battle as a side sees it at the start of its phase in a combat turn
// (battle_round, docs/sdk/ai-protocol.md §5).
struct BattleRound {
    Location where;
    int round = 0;
    int roundsMax = 0;
    std::span<const EmpireId> phaseOrder;
    std::span<const combat::TacticalPiece> pieces;
};

enum class DecloakReason : uint8_t { Order, Attack };

// One engine call's script and external players. Every method gets the
// call's context: the state it works on is ctx.state.
class Players {
public:
    virtual ~Players();

    // A planning call: true when the player answered (its commands went
    // through `sink`, settle included); false when the classic planner must
    // plan instead (no answer, a failure, or the player is out for the turn).
    virtual bool plan(TurnContext& ctx, EmpireId e, PlanCall call, const CommandSink& sink) = 0;
    // A new colony's type; nullopt: the classic choice.
    virtual std::optional<std::string> colonyType(TurnContext& ctx, EmpireId e, ObjectId planet, VehicleId ship) = 0;
    // Whether a group enters a sector with enemies it sees; nullopt: it enters.
    virtual std::optional<bool> enterSector(TurnContext& ctx, EmpireId e, std::span<const VehicleId> vehicles, Location where,
                                            std::span<const EmpireId> enemies) = 0;
    // Whether a cloaked vehicle (or a colony, by its planet) decloaks for an
    // order or an attack; nullopt: as the Ship Cloaking minister does.
    virtual std::optional<bool> decloak(TurnContext& ctx, EmpireId e, VehicleId vehicle, ObjectId planet, DecloakReason reason) = 0;
    // A side's tactical orders for its phase of a combat turn; nullopt: its
    // strategies play it. `refused` reports the orders the battle refused
    // (index, reason), for the player's next request.
    virtual std::optional<std::vector<combat::TacticalOrder>> battleRound(TurnContext& ctx, EmpireId e, const BattleRound& battle) = 0;
    virtual void refused(EmpireId e, std::vector<std::pair<size_t, std::string>> refusals) = 0;
    // Answers to give before asking, in order (a battle's, decided while a
    // window showed it: BattleAnswer::decisions).
    virtual void replay(std::span<const JournalEntry> entries) = 0;
    // The end of the engine call: each player asked in it gets end_session.
    virtual void endSession(TurnContext& ctx) = 0;
    // The rules hooks of the game's mods in this engine call (hooks.hpp);
    // null when the game has none.
    virtual RulesHooks* hooks() { return nullptr; }
    // The engine call's context, as the session starts (CallSession): what a
    // mod's order given during the call (game::apply) works with.
    virtual void begin(TurnContext&) {}
};

// Makes the session of one engine call. The SDK installs one
// (sdk::installPlayers); none is installed by default. It makes one only for
// a game that needs it (an empire played by a script or external player, or
// mods with rules scripts) and gives null otherwise.
using PlayersFactory = std::function<std::unique_ptr<Players>(const Rules& r, GameState& s)>;
void setPlayersFactory(PlayersFactory factory);
// The session for an engine call on `s`: null without a factory, or when the
// game needs none.
std::unique_ptr<Players> makePlayers(const Rules& r, GameState& s);

// An engine call's session, set up on its context: the players and the rules
// hooks (hooks.hpp). end() delivers the events still waiting and ends the
// session (Players::endSession). Leaving without end() (a battle stop, a
// fault) drops the session as it is: the call is put back and made again.
class CallSession {
public:
    explicit CallSession(TurnContext& ctx);
    ~CallSession();
    CallSession(const CallSession&) = delete;
    CallSession& operator=(const CallSession&) = delete;
    // Delivers the waiting events and ends the session; the context loses it.
    void end();

private:
    TurnContext& ctx_;
    std::unique_ptr<Players> players_;
};

// Plays a turn again with the answers it was played with: `again` is the
// state as the turn began, `played` the state after it. The answers of
// again's turn wait in again.journal.replay, so its players are not asked
// (the client's movement replay, docs/sdk/ai-protocol.md §8).
void replayJournal(GameState& again, const GameState& played);

// Drops journal entries older than the game turn before the current one.
void pruneJournal(GameState& s);

} // namespace opense4::game
