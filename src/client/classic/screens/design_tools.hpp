#pragma once

// Logic behind the Designs, Create Design and Weapons Report windows that
// needs no UI, so it can be unit tested: upgrading a design to the newest
// components, grouping entries for the condensed view, filtering the
// component catalogue, weapon mounts and design-name suggestions.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// Ships and bases are "ship designs"; everything else is a unit (docs/spec/03 §1).
bool isUnitHull(ruleset::VehicleType t);

// The vehicle types the empire can design, those it has at least one hull of,
// in type order: Create in Designs asks for one of them first (observed, spec 07
// session 3).
std::vector<ruleset::VehicleType> designableTypes(const game::Rules& r, const game::Empire& e);
// The empire's hulls of one vehicle type in data order (the Size picker); `keep`
// is listed too when it is of that type (a design being edited).
std::vector<uint32_t> hullsOfType(const game::Rules& r, const game::Empire& e, ruleset::VehicleType t, std::optional<uint32_t> keep = std::nullopt);
// The designer's title for a vehicle type: "Ship Design", "Weapon Platform Design".
std::string designWindowTitle(ruleset::VehicleType t);
// The design rules a hull's report lists, in our own words, as the designer
// checks them (docs/spec/03 §2.2, §4.2): a bridge; at most one auxiliary
// control when `Can Have Aux Con` is set (nothing is checked otherwise);
// life support and crew quarters minimums; no engines, at most N or no limit
// (`Max Engines` 0) and engines per movement point; the minimum shares of
// fighter bays, colony modules and cargo. The original's Ship Size report
// shows no such list (spec 06 §1.4); ours adds it.
std::vector<std::string> hullRuleLines(const ruleset::VehicleSize& h);

// Upgrade (docs/spec/03 §4.1, confirmed: binary): replaces every entry's
// component, on its own, with the last component of its Family in data-file
// order that the empire has researched (Rules::componentUpgradeTarget), any
// family, 0 included; numerals and names play no part, neither the hull's
// vehicle type nor the mount is checked, and each entry keeps its mount.
// With none researched the entry stays. Returns true if anything changed.
bool upgradeEntries(const game::Rules& r, const game::Empire& e, std::vector<game::DesignEntry>& entries);

// True if Upgrade would keep `component` as it is (upgradeEntries).
bool isLatestComponent(const game::Rules& r, const game::Empire& e, uint32_t component);

// Identical (component, mount) entries collapsed for the condensed view, in
// order of first appearance. `last` is the index of the last occurrence.
struct EntryGroup {
    game::DesignEntry entry;
    int count = 0;
    size_t last = 0;
};
std::vector<EntryGroup> groupEntries(std::span<const game::DesignEntry> entries);

// Components the empire has researched that fit on the hull's vehicle type, in data
// order. `group` filters by General Group (empty = all); `onlyLatest` keeps the last of
// each run of neighbouring components of one family among them (spec 02 §6.4).
std::vector<uint32_t> designerComponents(const game::Rules& r, const game::Empire& e, uint32_t hull, std::string_view group, bool onlyLatest);
// The General Groups among designerComponents(), sorted.
std::vector<std::string> componentGroups(const game::Rules& r, const game::Empire& e, uint32_t hull);

// Mounts that at least one of the empire's components can use on this hull.
std::vector<uint32_t> hullMounts(const game::Rules& r, const game::Empire& e, uint32_t hull);
// The mount a newly added component gets: `mount` if it applies, else -1.
int32_t mountFor(const game::Rules& r, uint32_t hull, uint32_t component, int32_t mount);
// Whether a mount's weapon-type requirement accepts a component, ignoring the hull (Weapons Report).
bool mountFitsWeapon(const game::Rules& r, uint32_t mount, uint32_t component);

// Weapons the empire has researched. `kind` None = every weapon type.
std::vector<uint32_t> knownWeapons(const game::Rules& r, const game::Empire& e, ruleset::WeaponKind kind, bool onlyLatest);

// Whether one of the empire's designs already uses this name.
bool designNameTaken(const game::GameState& s, const game::Empire& e, std::string_view name);
// "Lancer" -> "Lancer II", "Lancer II" -> "Lancer III"; skips names already taken.
std::string nextVersionName(const game::GameState& s, const game::Empire& e, std::string_view name);
// The first name of `list`, starting at `start` and wrapping, that no own design uses. Sets
// `start` past it. Empty if every name is taken.
std::string suggestDesignName(const game::GameState& s, const game::Empire& e, std::span<const std::string> list, size_t& start);
// Reads a design-name list (one name per line, Windows-1252) into UTF-8 strings.
std::vector<std::string> parseNameList(std::string_view fileContents);

// Roman numerals 1..39 ("" for anything else).
std::string romanNumeral(int n);

} // namespace opense4::client::classic
