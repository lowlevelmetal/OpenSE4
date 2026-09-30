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
// political operations). Events cannot use them.
bool needsSource(Effect e);
// Sabotage (harms the target) as opposed to espionage (only learns things);
// the AI's "stop sabotage" / "stop espionage" demands use this split.
bool isSabotage(Effect e);
// A harmful outcome for the affected empire (the Luck trait and bad-event
// abilities only reduce these). Signed effects are harmful when `amount` < 0.
bool isBad(Effect e, int amount);

// ---- Targets and application -----------------------------------------------------------------

// What an effect acts on. `empire` is the affected (victim) empire; the other
// fields are filled by pickTarget. Callers may preset `vehicle`, `object`,
// `system` or `other` to request a specific target ("Any" when invalid).
struct Target {
    EmpireId empire;
    EmpireId source;    // the acting empire (intelligence), invalid for events
    EmpireId other;     // a third empire (political operations)
    VehicleId vehicle;
    ObjectId object;    // planet, asteroid field, star or warp point
    SystemId system;
};

// Validates a requested target, or chooses one at random among the valid
// candidates ("Any"). nullopt: nothing to act on.
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

// Sum of a "- System" chance ability in a system for an empire: system-wide
// abilities plus the best (lowest) value among the empire's colonies there.
int64_t systemChanceModifier(const Rules& r, const GameState& s, EmpireId empire, SystemId sys, AbilityKind k);

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

// The chance (percent) that an empire has an event this turn, from the
// Event Frequency option and `Event Percent Chance Low/Medium/High`.
int eventChance(const Rules& r, const GameState& s);
// Events.txt records eligible under the Maximum Event Severity option and
// with an effect type we implement, in data order.
std::vector<uint32_t> eligibleEvents(const Rules& r, const GameState& s);

// Starts (or, for immediate events, fires) event record `eventType` against
// `target`. Returns false when the target was not valid.
bool trigger(TurnContext& ctx, uint32_t eventType, const effects::Target& target, Rng& rng);

// Turn phase 9: fires timed events that are due, then rolls new events (one
// roll per empire per turn, inferred).
void runEvents(TurnContext& ctx);

} // namespace opense4::game::events
