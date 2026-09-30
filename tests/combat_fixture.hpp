#pragma once

// The combat test fixture: the engine fixture's rules plus invented weapons,
// armor and hulls ("CT ..."), an arena to fight in (a fresh game with every
// empire at war and an empty sector), and helpers shared by the combat,
// tactical and simulator tests. All content is invented for the tests.

#include "engine_fixture.hpp"

#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::ctest {

inline constexpr ruleset::VehicleTypeMask kAll = 0xFF;
extern const std::vector<std::string> kHitsAll;

ruleset::Ability ab(game::AbilityKind k, int64_t v1 = 0, int64_t v2 = 0);
ruleset::Component& part(ruleset::Ruleset& rs, std::string name, int structure, std::vector<ruleset::Ability> abilities,
                         ruleset::VehicleTypeMask mask = kAll, int family = 0);
ruleset::Component& gun(ruleset::Ruleset& rs, std::string name, ruleset::WeaponKind kind, std::vector<int> damage, std::string type,
                        std::vector<std::string> targets = kHitsAll, int modifier = 0);

// The engine fixture plus the combat parts (built fresh / shared).
ruleset::Ruleset buildCombatRuleset();
const game::Rules& combatRules();
int32_t mountIndex(const game::Rules& r, std::string_view name);

void setTreaty(game::GameState& s, game::EmpireId a, game::EmpireId b, game::Treaty t);

struct Arena {
    game::GameState s;
    game::Location loc;               // an empty sector in empire A's home system
    game::EmpireId a{0u}, b{1u}, c{2u};
};
// A fresh game on the given rules with no vehicles, every empire at war, and an empty sector to fight in.
Arena makeArena(const game::Rules& rules, uint64_t seed = 7, int empires = 2);
Arena makeArena(uint64_t seed = 7, int empires = 2);

game::DesignId design(game::GameState& s, game::EmpireId owner, std::string_view name, std::string_view hull,
                      std::initializer_list<std::string_view> parts);
game::VehicleId spawn(game::GameState& s, game::DesignId d, game::Location where, int count = 1);
// A crewed frigate with `engines` engines plus the given parts, and a fuel pod
// last (a ship without supply storage can never fire, spec 04 §6).
game::DesignId frigate(game::GameState& s, game::EmpireId owner, std::string_view name, int engines,
                       std::initializer_list<std::string_view> extra, const game::Rules& r = combatRules());
void useStrategy(game::GameState& s, game::EmpireId e, std::vector<std::pair<std::string, std::string>> settings);
// Marks a vehicle as having moved in this turn from the sector (dx, dy) away.
void arriveFrom(game::GameState& s, game::VehicleId id, int dx, int dy);
// Marks a vehicle as having come through a warp point this turn: it starts in
// the small box at the map's centre (spec 04 §3), beside the pieces already there.
void warpIn(game::GameState& s, game::VehicleId id);

int moodCount(const game::TurnContext& ctx, game::EmpireId e, std::string_view trigger);
int damageTaken(const game::GameState& s, game::VehicleId id);
int countEvents(const game::CombatRecord& rec, game::CombatEvent::Kind k);
int pieceOf(const game::CombatRecord& rec, game::VehicleId id);
// Damage recorded by Hit events on a piece (the hit's value before shields).
int64_t hitsOn(const game::CombatRecord& rec, int piece);
game::TurnContext context(game::GameState& s, const game::Rules& r = combatRules());
bool destroyed(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, size_t entry);

// Varied battles for comparing ways of fighting the same battle: fleets in
// formation, carriers with fighters and drones, seekers and point defense,
// boarding, ramming and evasive strategies, crew conversion, tractor beams,
// satellites, a third empire, and (every fifth variant) an invasion of a
// planet with weapon platforms. Returns the state before the battle and the
// sector to fight in.
std::pair<game::GameState, game::Location> battleScenario(int variant, uint64_t seed);

} // namespace opense4::ctest
