#pragma once

// Read-only questions about the game state shared by every subsystem and the
// UI: where things are, who owns what, what abilities apply at a place.

#include "game/design.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <vector>

namespace opense4::game {

inline Location locationOf(const Galaxy& g, ObjectId o) {
    const SpaceObject& obj = g.object(o);
    return {obj.system, obj.sector};
}

// Planets (and asteroid fields) in a sector.
std::vector<ObjectId> planetsAt(const GameState& s, Location where);

// The game's object order (spec 03 §6.3 step 5, §19 Q62): the order of slots
// in the game's one list of objects, which stellar objects and vehicles share
// (SpaceObject::slot, Vehicle::slot; objectOrder in state.hpp). A sort key for
// a planet or a vehicle; smaller comes first. Combat's start positions use it
// (spec 04 §19.2 Q57).
uint64_t objectOrderKey(const GameState& s, ObjectId planet);
uint64_t objectOrderKey(const Vehicle& v);
// The colony owned by `empire` in a sector, if any.
const Colony* ownColonyAt(const GameState& s, EmpireId empire, Location where);

// Abilities of a colony's facilities (+ planet abilities). Facilities do not
// work on a planet with zero population, except where noted by the caller.
std::vector<ParsedAbility> colonyAbilities(const Rules& r, const GameState& s, const Colony& c);
// Space yard capacity: a facility on an own colony, or an intact component on an own vehicle.
// colonyHasSpaceYard is the facility (the yard's rate, the one-yard limit);
// a colony's yard works only while the colony is not cloaked (spec 01 §6.9,
// confirmed: binary): colonyHasWorkingYard, which spaceYardAt uses.
bool colonyHasSpaceYard(const Rules& r, const Colony& c);
bool colonyHasWorkingYard(const Rules& r, const Colony& c);
bool vehicleHasSpaceYard(const Rules& r, const GameState& s, const Vehicle& v);
bool spaceYardAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Best "Resource Reclamation" percentage available to `empire` in a sector (0 if none).
int reclamationPercentAt(const Rules& r, const GameState& s, EmpireId empire, Location where);

// Number of facilities a colony can hold (planet size, domed when the race
// cannot breathe the atmosphere).
int facilitySlots(const Rules& r, const GameState& s, const Colony& c);
int64_t maxPopulation(const Rules& r, const GameState& s, const Colony& c);
int64_t colonyCargoCapacity(const Rules& r, const GameState& s, const Colony& c);
bool breathable(const GameState& s, const Colony& c);
const ruleset::PlanetSize* planetSize(const Rules& r, const SpaceObject& planet, bool domed = false);

// Empires with which `a` is at war (or otherwise fights on contact).
bool hostile(const GameState& s, EmpireId a, EmpireId b);
bool allied(const GameState& s, EmpireId a, EmpireId b);  // Military Alliance or Partnership

// Number of ships (non-unit vehicles) and units an empire owns.
int shipCount(const Rules& r, const GameState& s, EmpireId e);
int unitCount(const Rules& r, const GameState& s, EmpireId e);
bool isUnitType(ruleset::VehicleType t);

// Next free design id / message id.
DesignId addDesign(GameState& s, Design d);

} // namespace opense4::game
