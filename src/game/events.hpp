#pragma once

// Random and timed events (docs/spec/05 §4), plus the effect handlers and the
// message formatter they share with intelligence projects (§2.3): both data
// files name an effect by its `Type` identifier and parameterize it with
// `Effect Amount`, and both use the same [%Token] scheme in their texts.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::effects {

// ---- Message tokens -------------------------------------------------------------------------

// Values for the [%Token] placeholders of Events.txt, IntelProjects.txt and AI
// speech. A token whose field is empty is removed; unknown tokens are kept.
struct Tokens {
    std::string systemName, sectorName;
    std::string sourceEmperorName, sourceEmpireName;
    std::string targetEmperorName, targetEmpireName;
    std::string otherEmperorName, otherEmpireName;
    std::string vehicleName, vehicleSize, planetName, designName, techName, treatyName, facilityName;
    std::string starName, warpPointName;
    std::optional<int64_t> actualAmount;
};

// Replaces every known [%Token] (case-insensitive) in `text`.
std::string substitute(std::string_view text, const Tokens& t);
// "<Name> <Empire Type>" and "<Title> <Leader Name>", as the tokens want them.
std::string empireFullName(const Empire& e);
std::string emperorFullName(const Empire& e);
// Fills the source/target/other empire tokens (invalid ids are skipped).
void setEmpireTokens(Tokens& t, const GameState& s, EmpireId source, EmpireId target, EmpireId other = {});
// Picks one of `messages` at random; nullptr when there are none.
const ruleset::Message* pickMessage(std::span<const ruleset::Message> messages, Rng& rng);

// ---- Effect types ---------------------------------------------------------------------------

// X(enumerator, "Type identifier in data")
#define OPENSE4_EFFECTS(X)                                                        \
    X(ShipDamage, "Ship - Damage")                                                \
    X(ShipLoseMovement, "Ship - Lose Movement")                                   \
    X(ShipLoseSupply, "Ship - Lose Supply")                                       \
    X(ShipRebel, "Ship - Rebel")                                                  \
    X(ShipExperienceChange, "Ship - Experience Change")                           \
    X(ShipCargoDamage, "Ship - Cargo Damage")                                     \
    X(ShipOrdersChange, "Ship - Orders Change")                                   \
    X(ShipMoved, "Ship - Moved")                                                  \
    X(ShipLocations, "Ship - Locations")                                          \
    X(ShipConcentrations, "Ship - Concentrations")                                \
    X(ShipConstructionInfo, "Ship - Construction Info")                           \
    X(ShipDesignsSteal, "Ship Designs - Steal")                                   \
    X(UnitDesignsSteal, "Unit Designs - Steal")                                   \
    X(PlanetConditionsChange, "Planet - Conditions Change")                       \
    X(PlanetValueChange, "Planet - Value Change")                                 \
    X(PlanetPopulationChange, "Planet - Population Change")                       \
    X(PlanetPopulationAngerChange, "Planet - Population Anger Change")            \
    X(PlanetPopulationRiot, "Planet - Population Riot")                           \
    X(PlanetPopulationRebel, "Planet - Population Rebel")                         \
    X(PlanetCargoDamage, "Planet - Cargo Damage")                                 \
    X(PlanetFacilityDamage, "Planet - Facility Damage")                           \
    X(PlanetInfo, "Planet - Info")                                                \
    X(PlanetLocations, "Planet - Locations")                                      \
    X(PlanetPlague, "Planet - Plague")                                            \
    X(PlanetPlagueCured, "Planet - Plague Cured")                                 \
    X(PlanetCreated, "Planet - Created")                                          \
    X(PlanetDestroyed, "Planet - Destroyed")                                      \
    X(StarCreated, "Star - Created")                                              \
    X(StarDestroyed, "Star - Destroyed")                                          \
    X(WarpPointOpened, "Warp Point - Opened")                                     \
    X(WarpPointClosed, "Warp Point - Closed")                                     \
    X(PointsChange, "Points - Change")                                            \
    X(PointsSteal, "Points - Steal")                                              \
    X(ResearchSteal, "Research - Steal")                                          \
    X(ResearchDeleteProject, "Research - Delete Project")                         \
    X(IntelDeleteProject, "Intel - Delete Project")                               \
    X(PoliticsDisruptTrade, "Politics - Disrupt Trade")                           \
    X(PoliticsInterceptMessages, "Politics - Intercept Messages")                 \
    X(PoliticsFakeMessages, "Politics - Fake Messages")                           \
    X(PoliticsPreventMessages, "Politics - Prevent Messages")                     \
    X(PoliticsTreatyInfo, "Politics - Treaty Info")                               \
    X(SystemInfo, "System - Info")                                                \
    X(EmpireInfo, "Empire - Info")                                                \
    X(TechLevelInfo, "Tech Level - Info")                                         \
    X(IntelligenceDefense, "Intelligence Defense")

enum class Effect : uint8_t {
#define OPENSE4_EFFECT_ENUM(name, text) name,
    OPENSE4_EFFECTS(OPENSE4_EFFECT_ENUM)
#undef OPENSE4_EFFECT_ENUM
    Count
};

std::string_view identifier(Effect e);
// Case- and spacing-insensitive; nullopt for identifiers we do not know.
std::optional<Effect> parseEffect(std::string_view type);

// Effects that need an acting empire (theft, defection, espionage reports,
// political operations). As events they achieve nothing.
bool needsSource(Effect e);
// Sabotage (harms the target) as opposed to espionage (only learns things);
// the AI's "stop sabotage" / "stop espionage" demands use this split.
bool isSabotage(Effect e);
// A harmful outcome for the affected empire. Signed effects are harmful when
// `amount` < 0. (The event luck roll applies to every event, good or bad.)
bool isBad(Effect e, int amount);

// ---- Targets and application -----------------------------------------------------------------

// What an effect acts on. `empire` is the affected (victim) empire, invalid
// for an event on an object nobody owns; the other fields are filled by
// pickTarget. Callers may preset `vehicle`, `object`, `system`, `other` or
// `tech` to request a specific target ("Any" when invalid).
struct Target {
    EmpireId empire;
    EmpireId source;    // the acting empire (intelligence), invalid for events
    EmpireId other;     // a third empire (political operations)
    VehicleId vehicle;
    ObjectId object;    // planet, asteroid field, star or warp point
    SystemId system;
    ruleset::TechAreaId tech;  // Research - Steal
};

// Draws for an "Any" target and for an event target (spec 05 §2.1, §4).
inline constexpr int kTargetDraws = 1000;
// Empires the galaxy can hold; a rebel colony becomes a new empire only below it.
inline constexpr size_t kMaxEmpires = 20;

// An intelligence project's target (spec 05 §2.1): a requested target is
// validated; "Any" draws candidates of the right kind at random, up to
// kTargetDraws times, removing each one that fails the checks (including
// `Change Bad Intelligence Chance - System`, below). nullopt: nothing to act on.
std::optional<Target> pickTarget(const Rules& r, const GameState& s, Effect e, const Target& request, Rng& rng);
// Where the target is, for logs and location-based modifiers.
std::optional<Location> targetLocation(const GameState& s, const Target& t);

struct Outcome {
    bool applied = false;
    int64_t actual = 0;               // the realized amount ([%ActualAmount])
    Tokens tokens;                    // object tokens (vehicle, planet, system, ...)
    std::vector<std::string> report;  // espionage findings for the source
};

// Applies an effect with its data `amount` to a target from pickTarget.
Outcome apply(TurnContext& ctx, Effect e, const Target& t, int amount, Rng& rng);

// `Change Bad Intelligence/Event Chance - System` (spec 05 §2.4, confirmed:
// binary): only abilities of things that belong to no empire count (the
// system's own abilities and the stellar abilities of its objects), and only
// a positive value V (the largest, inferred). 0 when there is none.
int64_t unownedChanceValue(const GameState& s, SystemId sys, AbilityKind k);
// A candidate is rejected when V is set, is not 100, and a roll of 1–100 is
// at most V. So it never matters in the stock data.
bool chanceRejects(int64_t v, Rng& rng);

// A colony breaks away as a new independent empire (spec 05 §2.3, spec 02
// §4): a computer player with the race of the colony's largest population
// group, the owner's technology and data lists, at War with its former owner
// (inferred). The planet becomes its capital (anger capped at 80). The one
// way a new empire is founded: the rebellion event and the intelligence
// operation both use it. Invalid when kMaxEmpires are reached. Adding an
// empire invalidates references into GameState::empires.
EmpireId breakAway(TurnContext& ctx, ObjectId planet);

// Damage to a vehicle in the standard order: armor first (design order),
// then the other intact components at random. Returns the damage applied.
int64_t damageVehicle(const Rules& r, GameState& s, Vehicle& v, int64_t amount, Rng& rng);
// Removes a vehicle from its fleet (empty fleets are deleted).
void detachFromFleet(GameState& s, Vehicle& v);
// Destroys cargo: `amount` kT of it, or half of everything when `amount` <= 1
// (the stock records use 1 as "some"). Returns the kT destroyed.
int64_t damageCargo(const Rules& r, const GameState& s, Cargo& c, int64_t amount, Rng& rng);

} // namespace opense4::game::effects

namespace opense4::game::events {

enum class Severity : uint8_t { Low, Medium, High, Catastrophic };
// Unknown or blank severities count as Low (inferred).
Severity parseSeverity(std::string_view text);

// No new events before the date 2402.0, counted in turns since 2400.0
// (spec 05 §4, confirmed: binary).
inline constexpr uint32_t kFirstEventDate = 20;

// The chance (percent) of a new event this turn for the whole galaxy, from
// the Event Frequency option and `Event Percent Chance Low/Medium/High`.
int eventChance(const Rules& r, const GameState& s);
// N: the number of Events.txt records whose Severity is at most the Maximum
// Event Severity option.
uint32_t allowedRecordCount(const Rules& r, const GameState& s);
// The original's record pick (spec 05 §4, confirmed: binary): uniform among
// the first N records of the file, whatever their severity (N as above).
std::optional<uint32_t> pickRecord(const Rules& r, const GameState& s, Rng& rng);

// The kind of target an event type takes from the whole galaxy (spec 05 §4).
// None: the type has no target list, so such a record never fires.
enum class TargetKind : uint8_t { None, Ship, Planet, Empire, Star, WarpPoint };
TargetKind targetKind(effects::Effect e);
// Draws the target of event record `record` from the whole galaxy: up to
// kTargetDraws draws, removing candidates that no longer exist, homeworlds
// (and stars in their systems) for High and Catastrophic planet and star
// events, candidates whose owner fails the luck roll, and candidates rejected
// by `Change Bad Event Chance - System`. nullopt: no event.
std::optional<effects::Target> pickEventTarget(const Rules& r, const GameState& s, uint32_t record, Rng& rng);
// Whether an event's target still exists (a timed event whose target is gone
// is dropped silently).
bool targetExists(const Rules& r, const GameState& s, const effects::Target& t);

// Starts event record `record` against `target`: with `Time Till Completion`
// 0 the effect applies at once and a random message goes out; otherwise the
// start message goes out and the event strikes exactly that many turns later.
// Returns false when the record is unknown or the target does not exist.
bool trigger(TurnContext& ctx, uint32_t record, const effects::Target& target, Rng& rng);

// Event step parts (spec 05 §8 step 9, after movement::runStellarHazards):
// the timed events that are due strike, then the single galaxy-wide roll for
// a new event. `date` is the game date in turns since 2400.0, already
// advanced for this turn.
void fireDueEvents(TurnContext& ctx, Rng& rng);
void rollNewEvent(TurnContext& ctx, uint32_t date, Rng& rng);

} // namespace opense4::game::events
