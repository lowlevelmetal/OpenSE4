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
void objectReport(UiContext& ui, game::ObjectId object);  // stars, storms, warp points
// Tab strip for reports; returns the chosen tab.
ReportTab reportTabs(UiContext& ui, ReportTab current, bool planet);

// One-line descriptions for lists.
std::string vehicleSummary(const UiContext& ui, const game::Vehicle& v);
std::string ordersSummary(const game::GameState& s, const game::Vehicle& v);
std::string orderText(const game::GameState& s, const game::Order& o);
std::string sectorName(const game::GameState& s, game::Location where);

// Small pictures.
Sprite vehicleMini(UiContext& ui, const game::Vehicle& v);
Sprite vehiclePortrait(UiContext& ui, const game::Vehicle& v);
Sprite objectSprite(UiContext& ui, const game::SpaceObject& o);

// What the local empire knows about vehicle v (own, or visible now).
bool knownVehicle(const UiContext& ui, const game::Vehicle& v);

} // namespace opense4::client::classic
