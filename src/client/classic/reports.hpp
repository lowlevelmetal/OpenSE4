#pragma once

// Object reports (docs/spec/06 §1.4), drawn into the current ImGui window.
// Used by the main window's report panel and by list windows.

#include "client/classic/ui.hpp"

namespace opense4::client::classic {

enum class ReportTab { Detail, Components, Cargo, Abilities, Facilities };

void vehicleReport(UiContext& ui, const game::Vehicle& v, ReportTab tab);
void fleetReport(UiContext& ui, const game::Fleet& f);
void planetReport(UiContext& ui, game::ObjectId planet, ReportTab tab);
void systemReport(UiContext& ui, game::SystemId sys);
// Stars, storms, warp points; `state`: another game than the session's (a
// battle's copy or a combat simulation's sandbox).
void objectReport(UiContext& ui, game::ObjectId object, const game::GameState* state = nullptr);
// Tab strip for reports; returns the chosen tab. Without `cargo` the Cargo tab is left out.
ReportTab reportTabs(UiContext& ui, ReportTab current, bool planet, bool cargo = true);
// One report tab at the cursor: the 72x30 cell of TabBtns.bmp in `column`
// (Detail, Comps, Cargo, Ability, Facil, Descr, Race, Tech), lit when selected;
// `label` names it for input scripts (and is drawn without the picture). True when clicked.
bool reportTab(UiContext& ui, int column, const char* label, bool selected);

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
