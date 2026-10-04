#pragma once

// The picture of a log entry (docs/spec/06 §4.1 "Pictures of the other
// entries", confirmed: binary): fixed by the kind of entry when the entry is
// made and resolved when it is shown, so LogEntry::picture names either a
// file of Pictures/Events/ (a plain name, as random events and intelligence
// projects give it) or a subject, "<kind>:<ids>":
//
//   race:E          empire E's race portrait
//   hull:D          design D's hull portrait for its owner's race (the
//                   generic race's when the race has none)
//   planet:O        planet O's picture (none once it is gone)
//   facility:F      facility F's picture
//   group:K:E       empire E's race's portrait of a fighter, mine or
//                   satellite group (K: fighter, mine, satellite; no fallback)
//   fleet:E         empire E's race's fleet portrait (generic fallback)
//   developed:K:I   an item developed or an intelligence project now
//                   available (K: component, facility, hull, intel): the Log
//                   shows the item's details instead of a picture and text
//
// Nothing here is a rule of the game: the strings are only shown.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace opense4::game::logpicture {

std::string race(EmpireId e);
std::string hull(DesignId d);
std::string planet(ObjectId p);
std::string facility(uint32_t f);
std::string fleet(EmpireId e);
// A colony founded: Colony<Surface><Atmosphere> of Pictures/Events/ (Rock,
// Ice or Gas; Oxygen, Methane, Hydrogen, CarbDiox, Argon or None).
std::string colonyFounded(const SpaceObject& planet);
// The acting object's own picture (refused or failed orders, and the
// entries about one vehicle): the hull portrait of a ship or base, the
// race's group portrait of a fighter, mine or satellite group (the hull's
// otherwise), the fleet portrait for a fleet member.
std::string vehicle(const Rules& r, const GameState& s, const Vehicle& v);
// The same for a unit group by its kind alone (a unit group gone already).
std::string unitGroup(ruleset::VehicleType type, EmpireId owner, DesignId design);
// An item developed: `index` in the order components, facilities, vehicle
// sizes, intelligence projects (research's availability order).
std::string developed(const Rules& r, size_t index);

struct Parsed {
    enum class Kind : uint8_t { None, Event, Race, Hull, Planet, Facility, Group, Fleet, Developed };
    Kind kind = Kind::None;
    std::string name;       // Event: the file name; Group: fighter, mine, satellite; Developed: component, facility, hull, intel
    uint32_t id = 0;        // Hull: design; Planet: object; Facility: index; Developed: the item's index
    EmpireId empire;        // Race, Group, Fleet
};
Parsed parse(std::string_view picture);

} // namespace opense4::game::logpicture
