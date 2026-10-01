#pragma once

// Status icons (docs/spec/06 §4.4, confirmed: binary): which cells of the
// General picture's two icon rows an object shows, in the order they are
// drawn. Cells are 0-based (Art::statusCell); the manual numbers its images
// from 1. Ship icons are for the viewer's own ships, planet icons (except
// ruins) for the viewer's own colonies. Headless, tested in
// tests/test_client_logic.cpp.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <vector>

namespace opense4::client::classic {

// The cells the executable draws (cells 1, 7, 8, 13, 14, 21, 23, 24, 27, 28,
// 29, 31, 32, 34 and 35 are never drawn).
namespace status_cell {
inline constexpr int kSpaceYard = 0;
inline constexpr int kRepeatOrders = 2;
inline constexpr int kSentry = 3;
inline constexpr int kLowSupply = 4;
inline constexpr int kNoSupply = 5;
inline constexpr int kMothballed = 6;
inline constexpr int kCloaked = 9;
inline constexpr int kMinister = 10;
inline constexpr int kCanRepair = 11;
inline constexpr int kBuilding = 12;
inline constexpr int kRuins = 15;
inline constexpr int kRepairHere = 16;
inline constexpr int kDomed = 17;
inline constexpr int kFighters = 18;
inline constexpr int kSatellites = 19;
inline constexpr int kMines = 20;
inline constexpr int kPopulation = 22;
inline constexpr int kTroops = 25;
inline constexpr int kPlatforms = 26;
inline constexpr int kNotConnected = 30;
inline constexpr int kDamaged = 33;
inline constexpr int kRemoteMining = 36;
inline constexpr int kDrones = 37;
} // namespace status_cell

// A ship, base or unit group of the viewer's.
std::vector<int> vehicleStatusCells(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
// A colony of the viewer's; `connected`: its resources reach the empire
// (economy::ColonyOutput::connected).
std::vector<int> colonyStatusCells(const game::Rules& r, const game::GameState& s, const game::Colony& c, bool connected);
// A planet as `viewer` sees it: the colony's icons when it is the viewer's
// colony, and the ruins icon on any planet.
std::vector<int> planetStatusCells(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::ObjectId planet);
// A fleet of the viewer's.
std::vector<int> fleetStatusCells(const game::GameState& s, const game::Fleet& f);

} // namespace opense4::client::classic
