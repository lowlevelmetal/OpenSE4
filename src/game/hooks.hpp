#pragma once

// The rules hooks of mods (docs/sdk/rules.md, docs/MODDING_SDK.md §7): the
// engine's side. Mods with rules scripts register Python functions for named
// moments of the game, and change the game from them only through effects
// that keep the state valid. The modding SDK (src/sdk) runs them, so the game
// itself stays free of scripts: the engine only says when, through the
// RulesHooks of the engine call's session (game/players.hpp), and does
// nothing when there is none.
//
// Two kinds of hooks:
//   - moments (setup, turn start, orders applied, movement days, battles,
//     the end-of-turn steps, victory, turn end) run where the engine calls
//     them, and their effects apply at once;
//   - events (a vehicle entered a sector or was destroyed, a colony founded,
//     a vehicle built, a tech level researched, a treaty changed, a message
//     delivered, an event fired) are noted where they happen and delivered,
//     in the order they happened, at the next safe point: after the movement
//     day or the live run, after each end-of-turn step, before the turn ends
//     and at the end of the engine call. So no script runs, and nothing is
//     added or removed, inside the engine's own loops.
//
// A game without rules mods has no session (or one without hooks), and every
// call here does nothing: it plays exactly as before.

#include "core/rng.hpp"
#include "game/commands.hpp"
#include "game/state.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace opense4::game {

struct TurnContext;
struct GameSetup;

enum class Hook : uint8_t {
    // Setup (createGame).
    NewGame, GenerateGalaxy, AfterGalaxy,
    // Turn start.
    TurnStart, OrdersApplied,
    // Movement and combat.
    MovementDay, VehicleEnteredSector, BeforeBattle, AfterBattle, VehicleDestroyed,
    // End of turn.
    EmpireEndOfTurn, ColonyEndOfTurn,
    // Events.
    ColonyFounded, VehicleBuilt, TechResearched, TreatyChanged, MessageSent, EventFired,
    // Turn end.
    CheckVictory, TurnEnd,
    Count
};
// "colony_end_of_turn" (the scripts' names).
std::string_view hookName(Hook h);
std::optional<Hook> hookByName(std::string_view name);
// The events: noted where they happen, delivered at the next safe point.
bool isEventHook(Hook h);

// The steps of an empire's end-of-turn processing that empire_end_of_turn
// runs before and after (empireEndOfTurn).
enum class EndStep : uint8_t { Intelligence, Research, Income, Maintenance, Population, Happiness, Construction, Repair, Supply, GroundCombat, Count };
std::string_view endStepName(EndStep s);   // "ground_combat"
std::optional<EndStep> endStepByName(std::string_view name);

// What a hook is about. Each hook fills the fields it names (docs/sdk/rules.md
// lists them); the others stay invalid or empty.
struct HookArgs {
    EmpireId empire;
    EmpireId other;              // treaty_changed: the other empire
    ObjectId planet;             // a colony (by its planet)
    VehicleId vehicle;
    DesignId design;             // vehicle_built: what was built
    std::optional<Location> where;
    int day = 0;                 // movement_day: 1 to 30 in a simultaneous turn; 0 in a turn-based game's live run
    EndStep step = EndStep::Count;
    bool after = false;          // empire_end_of_turn: after the step (else before)
    ruleset::TechAreaId area;    // tech_researched
    int level = 0;               // tech_researched
    int count = 0;               // vehicle_built: units built
    Treaty treaty = Treaty::None;      // treaty_changed: the new treaty
    Treaty oldTreaty = Treaty::None;   // treaty_changed: the treaty before
    MessageId message;           // message_sent
    std::string text;            // vehicle_destroyed: the cause; event_fired: the event's name
    std::optional<size_t> battle;   // after_battle: the battle's record in GameState::combats
    // vehicle_destroyed: the vehicle as it was (it may already be marked
    // dead); new_game: the setup the game is made from. Valid during run() only.
    const Vehicle* lost = nullptr;
    const GameSetup* setup = nullptr;
};

// One engine call's rules hooks (the session's, game/players.hpp).
class RulesHooks {
public:
    virtual ~RulesHooks();
    // Whether any mod has a function for the hook (for empire_end_of_turn,
    // for that step and side). Cheap: the call sites ask before building
    // what a hook needs.
    virtual bool wants(Hook h, const HookArgs* args = nullptr) const = 0;
    // A moment runs now; an event is noted (with what it needs from the
    // state as it is now) for the next deliver().
    virtual void run(TurnContext& ctx, Hook h, const HookArgs& args) = 0;
    // A safe point: the events noted so far run, in order.
    virtual void deliver(TurnContext& ctx) = 0;
    // The mods' events: each declared event's roll, after the classic event
    // step, with its generator.
    virtual void eventStep(TurnContext& ctx, Rng& rng) = 0;
    // A mod's intelligence project (an IntelProjects.txt record whose Type a
    // mod declares) took effect: true, false (it failed, with nothing
    // done), or nullopt when the project is not a mod's.
    virtual std::optional<bool> intelProject(TurnContext& ctx, EmpireId source, const IntelProjectOrder& order) = 0;
};

// The call sites' shorthand: nothing without hooks.
bool hookWanted(const TurnContext& ctx, Hook h, const HookArgs* args = nullptr);
void runHook(TurnContext& ctx, Hook h, const HookArgs& args = {});
void deliverHooks(TurnContext& ctx);
// vehicle_destroyed: `v` is lost (called just before it is marked dead).
// `cause`: "battle", "mines", "hazard", "event", "maintenance" or
// "empire_destroyed" (docs/sdk/rules.md).
void noteVehicleLost(TurnContext& ctx, const Vehicle& v, std::string_view cause);

// ---- Mod orders (cmd::ModCommand) -------------------------------------------------------------

// Applies a mod's order: checks it against the mod's declaration and runs its
// check and effect. The SDK installs one; without it every mod order is refused.
using ModCommandHandler = std::function<CommandResult(const Rules& r, GameState& s, EmpireId empire, const cmd::ModCommand& c)>;
void setModCommandHandler(ModCommandHandler handler);
CommandResult applyModCommand(const Rules& r, GameState& s, EmpireId empire, const cmd::ModCommand& c);

} // namespace opense4::game
