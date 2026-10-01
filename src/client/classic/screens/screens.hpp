#pragma once

// One factory per classic window. Each group of windows lives in its own file
// (see registry.cpp for the mapping).

#include "client/classic/ui.hpp"

namespace opense4::client::classic {

// game_menu.cpp
std::unique_ptr<Screen> makeGameMenu(const ScreenArgs& args);
std::unique_ptr<Screen> makeSaveGame(const ScreenArgs& args);
std::unique_ptr<Screen> makeLoadGame(const ScreenArgs& args);

// designs.cpp
std::unique_ptr<Screen> makeDesigns(const ScreenArgs& args);
std::unique_ptr<Screen> makeCreateDesign(const ScreenArgs& args);

// planets.cpp
std::unique_ptr<Screen> makePlanets(const ScreenArgs& args);
std::unique_ptr<Screen> makeColonies(const ScreenArgs& args);

// queues.cpp
std::unique_ptr<Screen> makeQueues(const ScreenArgs& args);
std::unique_ptr<Screen> makeSetQueue(const ScreenArgs& args);

// ships.cpp
std::unique_ptr<Screen> makeShips(const ScreenArgs& args);
std::unique_ptr<Screen> makeFleetTransfer(const ScreenArgs& args);
std::unique_ptr<Screen> makeCargoTransfer(const ScreenArgs& args);
std::unique_ptr<Screen> makeLaunchRecover(const ScreenArgs& args);
std::unique_ptr<Screen> makeScrap(const ScreenArgs& args);
std::unique_ptr<Screen> makeViewOrders(const ScreenArgs& args);
std::unique_ptr<Screen> makeSelectWaypoint(const ScreenArgs& args);
std::unique_ptr<Screen> makeStellarManipulation(const ScreenArgs& args);
std::unique_ptr<Screen> makeRename(const ScreenArgs& args);

// research.cpp
std::unique_ptr<Screen> makeResearch(const ScreenArgs& args);
std::unique_ptr<Screen> makeTechTree(const ScreenArgs& args);
std::unique_ptr<Screen> makeIntelligence(const ScreenArgs& args);

// empires.cpp
std::unique_ptr<Screen> makeEmpires(const ScreenArgs& args);
std::unique_ptr<Screen> makeCommunicate(const ScreenArgs& args);
std::unique_ptr<Screen> makeTreatyGrid(const ScreenArgs& args);
std::unique_ptr<Screen> makeScores(const ScreenArgs& args);
std::unique_ptr<Screen> makeComparisons(const ScreenArgs& args);
std::unique_ptr<Screen> makeHistory(const ScreenArgs& args);
std::unique_ptr<Screen> makeRaceReport(const ScreenArgs& args);
std::unique_ptr<Screen> makeVictoryConditions(const ScreenArgs& args);

// log.cpp
std::unique_ptr<Screen> makeLog(const ScreenArgs& args);

// empire_status.cpp
std::unique_ptr<Screen> makeEmpireStatus(const ScreenArgs& args);
std::unique_ptr<Screen> makeEmpireOptions(const ScreenArgs& args);
std::unique_ptr<Screen> makeMinisters(const ScreenArgs& args);
std::unique_ptr<Screen> makeSystemsToAvoid(const ScreenArgs& args);
std::unique_ptr<Screen> makeWaypoints(const ScreenArgs& args);
std::unique_ptr<Screen> makeStrategies(const ScreenArgs& args);
std::unique_ptr<Screen> makeRepairPriorities(const ScreenArgs& args);

// help.cpp
std::unique_ptr<Screen> makeHelp(const ScreenArgs& args);

// galaxy_map.cpp
std::unique_ptr<Screen> makeGalaxyMap(const ScreenArgs& args);

// combat_replay.cpp
std::unique_ptr<Screen> makeCombatReplay(const ScreenArgs& args);

// tactical.cpp: the Tactical Combat window and its Orders and Options windows,
// for the battle in ClassicSession::tactical().
std::unique_ptr<Screen> makeTacticalCombat(const ScreenArgs& args);
std::unique_ptr<Screen> makeTacticalOrders(const ScreenArgs& args);
std::unique_ptr<Screen> makeTacticalOptions(const ScreenArgs& args);

// strategic_combat.cpp: the watch-only Strategic Combat window and the Ground
// Combat window. Strategic Combat shows GameState::combats[index], or with
// index -1 the session's simulation fought by the strategies. Ground Combat
// shows ground combat `sub` of that battle (index -1 and the session's
// tactical fight: its record; no battle given: the last one with a ground combat).
std::unique_ptr<Screen> makeStrategicCombat(const ScreenArgs& args);
std::unique_ptr<Screen> makeGroundCombat(const ScreenArgs& args);

// simulator.cpp: the Combat Simulator window (Designs -> Simulator). ScreenArgs::text
// "demo" fills it with a sample battle (automation, screenshots).
std::unique_ptr<Screen> makeCombatSimulator(const ScreenArgs& args);
// A sample simulated battle for automation: the player's warships against
// copies of them, the player driving the first side. False when the player has none.
bool startDemoSimulation(UiContext& ui, bool tactical);

// settings_screen.cpp
std::unique_ptr<Screen> makeSettings(const ScreenArgs& args);
// The Sound page (classic sound and music preferences), shared with the front end.
void soundSettingsPage(float px);

// learn_screens.cpp: the Learn window (ScreenArgs::text: "tutorials", "training"
// or "manual", the tab to show) and the manual (ScreenArgs::text: "slug" or
// "slug#anchor"; empty: the first page). docs/LEARNING.md.
std::unique_ptr<Screen> makeLearn(const ScreenArgs& args);
std::unique_ptr<Screen> makeManual(const ScreenArgs& args);

// A stand-in for windows that are not written yet.
std::unique_ptr<Screen> makePlaceholder(ScreenId id);

} // namespace opense4::client::classic
