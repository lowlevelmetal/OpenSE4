#pragma once

// What the system panel draws in one sector (docs/spec/06 §2.4 "Sector
// contents", §7 Q49, confirmed: binary): the stellar object shown and how many
// there are, and the vehicles as one sprite with a count or as owner flags
// with counts. Headless, tested in tests/test_main_window.cpp.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <span>
#include <vector>

namespace opense4::client::classic {

struct SectorView {
    // The stellar object drawn (planet, asteroid field, star, storm, warp
    // point, comet): the first in the system's object list unless a later
    // planet has a larger planet-size Stellar Size; and how many there are
    // (their number is drawn when above 1).
    std::optional<game::ObjectId> stellar;
    int stellarCount = 0;

    // Vehicles, per owner in player-number order, each object counting 1.
    struct Owner {
        game::EmpireId empire;
        int count = 0;
    };
    std::vector<Owner> owners;

    // Owner flags (one owner beside a stellar object, or several owners): each
    // at (X, Y + k * flagStep) with its count at (X + 14, Y + k * flagStep).
    bool flags = false;
    int flagStep = 10;

    // One owner and no stellar object: the vehicle drawn (the largest by hull
    // size, a ship before any unit group), as its fleet's icon when it is one
    // of the viewer's ships in a fleet; its count at the bottom right when the
    // owner has more than one object, or a lone unit group's unit count (in
    // white, without the black box); the dotted ring when it is cloaked.
    std::optional<game::VehicleId> sprite;
    bool fleetIcon = false;
    std::optional<int> count;
    bool unitCount = false;
    bool cloakRing = false;
};

// `objects`: the sector's stellar objects in the system's list order;
// `vehicles`: the vehicles the viewer sees there. `cellHeight`: 36 or 50.
SectorView sectorView(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, std::span<const game::ObjectId> objects,
                      std::span<const game::Vehicle* const> vehicles, int cellHeight);

// The spacing of stacked owner flags: 10 px, or the cell height div the
// number of owners when they would not fit.
int flagStep(int owners, int cellHeight);

// A planet's size for the choice above: its PlanetSize's Stellar Size, Tiny 1
// to Huge 5; 0 for anything that is not a planet.
int stellarSizeRank(const game::Rules& r, const game::SpaceObject& o);

} // namespace opense4::client::classic
