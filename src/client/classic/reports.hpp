#pragma once

// Object reports (docs/spec/06 §1.4), drawn into the current ImGui window.
// Used by the main window's report panel and by list windows.

#include "client/classic/ability_lines.hpp"
#include "client/classic/screens/item_ref.hpp"
#include "client/classic/ui.hpp"

#include <optional>

namespace opense4::client::classic {

// Every report filled for an object opens on Detail; only a redraw in place
// keeps its tab (spec 06 §2.5, §7 Q107).
enum class ReportTab { Detail, Components, Cargo, Abilities, Facilities };

// A right-click on a facility of the Facil page or a component of the Comps
// page returns that item, whose report the report's owner opens (an
// ItemReportPopup; spec 06 §1.4, §7 Q105); left-clicks there do nothing.
// `simulator`: a report in the Combat Simulator, whose Ability page leaves
// the racial and culture lines out.
std::optional<ItemRef> vehicleReport(UiContext& ui, const game::Vehicle& v, ReportTab tab, bool simulator = false);
// The Fleet Report (spec 06 §2.5 "The Fleet Report", spec 03 §17): the
// fleet's figures and its members at its location, each row with its
// picture, name ("(Fleet Leader)" on the leader's) and status icons. A
// left-click on a member asks to make it the leader, a right-click for its
// Ship Report as a popup; the caller acts on them.
struct FleetReportClick {
    std::optional<game::VehicleId> leader;
    std::optional<game::VehicleId> report;
};
FleetReportClick fleetReport(UiContext& ui, const game::Fleet& f);
std::optional<ItemRef> planetReport(UiContext& ui, game::ObjectId planet, ReportTab tab, bool simulator = false);
void systemReport(UiContext& ui, game::SystemId sys);
// Stars, storms, warp points; `state`: another game than the session's (a
// battle's copy or a combat simulation's sandbox).
void objectReport(UiContext& ui, game::ObjectId object, const game::GameState* state = nullptr);
// Tab strip for reports; returns the chosen tab. Without `cargo` the Cargo tab is left out.
// `clicked`: set when a tab was clicked (even the one shown).
ReportTab reportTabs(UiContext& ui, ReportTab current, bool planet, bool cargo = true, bool* clicked = nullptr);
// One report tab at the cursor: the 72x30 cell of TabBtns.bmp in `column`
// (Detail, Comps, Cargo, Ability, Facil, Descr, Race, Tech), lit when selected;
// `label` names it for input scripts (and is drawn without the picture). True when clicked.
bool reportTab(UiContext& ui, int column, const char* label, bool selected);

// Draws the lines: the blue lamp of General.bmp at (0,2) of each, the text from
// x 13, wrapped 23 px short of the page's width. An empty page stays empty.
void abilityPage(UiContext& ui, const std::vector<std::string>& lines);

// One-line descriptions for lists. `viewer` names warp points the way that
// empire knows them (sight::warpPointName: the destination once explored);
// without one they show their plain name.
std::string vehicleSummary(const UiContext& ui, const game::Vehicle& v);
// A vehicle's design, "Name xN" for a unit group, or for a group that mixes
// designs each design with its units (the first `shown`, then "and N more").
std::string groupDesigns(const game::GameState& s, const game::Vehicle& v, size_t shown = 3);
std::string ordersSummary(const game::GameState& s, const game::Vehicle& v, game::EmpireId viewer = {});
std::string orderText(const game::GameState& s, const game::Order& o, game::EmpireId viewer = {});
std::string sectorName(const game::GameState& s, game::Location where, game::EmpireId viewer = {});
// A stellar object's name as the viewer knows it (warp points: sight::warpPointName).
std::string objectName(const game::GameState& s, game::ObjectId id, game::EmpireId viewer);

// Small pictures.
Sprite vehicleMini(UiContext& ui, const game::Vehicle& v);
Sprite vehiclePortrait(UiContext& ui, const game::Vehicle& v);
Sprite objectSprite(UiContext& ui, const game::SpaceObject& o);

// What the local empire knows about vehicle v (own, or visible now).
bool knownVehicle(const UiContext& ui, const game::Vehicle& v);

} // namespace opense4::client::classic
