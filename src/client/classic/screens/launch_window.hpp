#pragma once

// The window Launch \ Recover Units (docs/spec/06 §1.3, §1.10.2, spec 03
// §12), shared by the main window (order U) and a tactical battle (L, or
// Launch Units in the Orders window): its two lists and its amount radios.
// Each window fills the lists from what it shows and acts on the clicks
// (launch_recover.cpp on the map, tactical.cpp in a battle).
//
// Left: the carriers (on the map every own ship, base and colony of the
// sector that can take part, in a battle the selected piece), each with its
// "Can Launch:" lines for the four kinds and the units in its cargo; a click
// on a carrier selects it, a click on a unit launches it from that carrier.
// Right: the unit groups (on the map the own groups of the sector, in a
// battle the groups launched while this window has been open); a click on a
// group, or on one design of a mixed group, recovers it into the selected
// carrier. Move One, Move Five, Move Ten and Move All set how much a click
// moves; the empire's options keep the choice (InterfaceOptions::unitsStep).

#include "client/classic/art.hpp"
#include "client/classic/ui.hpp"

#include "game/state.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic::launchui {

struct UnitLine {
    game::DesignId design;
    std::string name;
    Sprite picture;
    int64_t count = 0;
};

struct CarrierRow {
    std::string name;
    Sprite picture;
    std::array<std::string, 4> canLaunch;   // fighters, satellites, mines, drones
    std::vector<UnitLine> units;
    bool selected = false;
};

struct GroupRow {
    std::string name;
    Sprite picture;
    std::string detail;
    std::vector<UnitLine> designs;   // listed under the group when it mixes designs
};

struct Lists {
    std::string carriersCaption = "Units Aboard", groupsCaption = "Units In Space";
    std::vector<CarrierRow> carriers;
    std::vector<GroupRow> groups;
    std::string noCarriers, noGroups;
};

struct Click {
    std::optional<size_t> carrier;                            // a carrier row: select it
    std::optional<std::pair<size_t, size_t>> launch;          // a carrier's unit line
    std::optional<size_t> recover;                            // a whole group
    std::optional<std::pair<size_t, size_t>> recoverDesign;   // one design of a mixed group
    std::optional<size_t> carrierReport, groupReport;         // right-clicks
};

// Draws both lists side by side in `size` (ImGui units).
Click drawLists(UiContext& ui, const Lists& lists, ImVec2 size);

// The "Can Launch:" line of one kind: on the map what is left this game turn
// against the rate ("3 / 4 Fighters"), in a battle what is left this combat
// turn ("3 Fighters"). Empty for a carrier without a launcher of the kind.
std::string canLaunchLine(int64_t left, int64_t rate, std::string_view kind, bool battle);

// The radios Move One, Move Five, Move Ten and Move All (`step` in the
// numbering of InterfaceOptions::unitsStep); true when the choice changed.
bool stepRadios(Dialog& d, uint8_t& step);
// What a click moves of `available`: 1, 5, 10 or all.
int64_t stepCount(uint8_t step, int64_t available);

// The step a battle could not keep in the empire's options (no command can be
// given while a battle waits); the next window on the map keeps it.
std::optional<uint8_t>& pendingStep();

} // namespace opense4::client::classic::launchui
