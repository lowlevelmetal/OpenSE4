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
std::unique_ptr<Screen> makeJettisonCargo(const ScreenArgs& args);    // cargo_transfer.cpp: ScreenArgs::vehicle or ::planet
std::unique_ptr<Screen> makeConvertResources(const ScreenArgs& args); // convert_resources.cpp: ScreenArgs::planet
std::unique_ptr<Screen> makeLaunchRecover(const ScreenArgs& args);
std::unique_ptr<Screen> makeScrap(const ScreenArgs& args);
std::unique_ptr<Screen> makeAbandonPlanet(const ScreenArgs& args);   // scrap.cpp: ScreenArgs::planet
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
// Borders: the systems the empires claim, a window of its own over Empires.
std::unique_ptr<Screen> makeBorders(const ScreenArgs& args);

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

// combat_replay.cpp: the Combat Replay window (GameState::combats[index]) and its options.
std::unique_ptr<Screen> makeCombatReplay(const ScreenArgs& args);
std::unique_ptr<Screen> makeCombatReplayOptions(const ScreenArgs& args);

// tactical.cpp: the Tactical Combat window and its Orders, Launch Units,
// Combat Options and Combat Piece Report (piece `index`) windows, for the
// battle in ClassicSession::tactical().
std::unique_ptr<Screen> makeTacticalCombat(const ScreenArgs& args);
// Every order the player's side gave in tactical battles so far, oldest first,
// as learn::battleOrderId kinds (for the lessons' battle_order).
std::vector<std::string>& tacticalOrderLog();
std::unique_ptr<Screen> makeTacticalOrders(const ScreenArgs& args);
std::unique_ptr<Screen> makeTacticalOptions(const ScreenArgs& args);
std::unique_ptr<Screen> makeTacticalLaunch(const ScreenArgs& args);
std::unique_ptr<Screen> makeCombatPieceReport(const ScreenArgs& args);

// strategic_combat.cpp: the Strategic Combat window and the Ground Combat
// window. Strategic Combat shows GameState::combats[index] (a battle the host
// of a network or PBEM game fought, played back); with index -1 the session's
// fight without player sides (a simulation, or a game battle the strategies
// fight while it is shown); with kStrategicQuestion the session's battle
// question: Strategic and Tactical buttons when it asks, else Begin and
// Close. Ground Combat shows ground combat `sub` of that battle (index -1 and
// the session's tactical fight: its record; no battle given: the last one
// with a ground combat), or with kGroundQuestion the colony owner's
// end-of-turn fight the session's question holds.
inline constexpr int kStrategicQuestion = -2;
inline constexpr int kGroundQuestion = -3;
std::unique_ptr<Screen> makeStrategicCombat(const ScreenArgs& args);
std::unique_ptr<Screen> makeGroundCombat(const ScreenArgs& args);

// finale_screen.cpp: the ending window (finale.hpp). ScreenArgs::index is the
// FinaleKind, or ScreenArgs::text names it ("victory", "lose", "human-dead").
std::unique_ptr<Screen> makeFinale(const ScreenArgs& args);

// simulator.cpp: the Combat Simulator window (Designs -> Simulator). ScreenArgs::text
// "demo" fills it with a sample battle (automation, screenshots).
std::unique_ptr<Screen> makeCombatSimulator(const ScreenArgs& args);
// A sample simulated battle for automation: the player's warships against
// copies of them, the player driving the first side. False when the player has none.
bool startDemoSimulation(UiContext& ui, bool tactical);
// A sample ground combat for automation: two of the player's troop transports
// full of its troops against its homeworld, fought by the strategies in the
// Strategic Combat window, begun at once; Ground Combat opens when the troops
// land. The reason when it cannot start (no troop design...), else empty.
std::string startDemoGroundCombat(UiContext& ui);
// Fleets For Plr and Change Cargo (spec 06 §1.10.4, confirmed: binary) open the
// Fleet Transfer and Cargo Transfer windows with ScreenArgs::text
// kSimulatorWindow: those windows then work on a sandbox built from the
// simulator's setup, never on the real game, and the simulator takes back
// what was changed there when they close. drawInSimulatorSandbox() runs a
// window's drawing with a UiContext over that sandbox (false, drawing
// nothing, when there is none); simulatorSandboxClosed() tells the simulator
// the window has gone.
inline constexpr const char* kSimulatorWindow = "simulator";
bool drawInSimulatorSandbox(UiContext& ui, const std::function<bool(UiContext&)>& draw);
void simulatorSandboxClosed();
// Change Cargo's two lists (spec 06 §7 Q80): every row of the Combat Vehicles
// list as a holder, and the temporary Storehouse; null without a cargo sandbox.
struct SimulatorSandbox;
const SimulatorSandbox* simulatorCargoSandbox();
// Opened with ScreenArgs::text kViewOnly (Fleet Transfer's Existing Fleets,
// spec 06 §7 Q79), the Ships\Units window is for viewing only: a left-click
// on a row does nothing.
inline constexpr const char* kViewOnly = "view";
// A tactical simulation is being fought: Designs closes, and the Tactical
// Combat window opens it again with the simulator afterwards (spec 06 §1.10.4).
bool tacticalSimulationRunning(const UiContext& ui);
// Set when Designs closed for one, so that it opens again afterwards.
bool& designsClosedForSimulation();

// settings_screen.cpp: the per-computer Options window and OpenSE4's Settings.
std::unique_ptr<Screen> makeOptions(const ScreenArgs& args);
std::unique_ptr<Screen> makeSettings(const ScreenArgs& args);
// The Sound page (classic sound and music preferences), shared with the front end.
void soundSettingsPage(float px);
// The Modding page: OpenSE4's views for modders (the AI notes view), shared with the front end.
void moddingSettingsPage(float px);
// player_errors.cpp: Computer Player Errors, the failures of the game's computer players of mods.
std::unique_ptr<Screen> makePlayerErrors(const ScreenArgs& args);

// learn_screens.cpp: the Learn window (ScreenArgs::text: "tutorials", "training"
// or "manual", the tab to show) and the manual (ScreenArgs::text: "slug" or
// "slug#anchor"; empty: the first page). docs/LEARNING.md.
std::unique_ptr<Screen> makeLearn(const ScreenArgs& args);
std::unique_ptr<Screen> makeManual(const ScreenArgs& args);

// A stand-in for windows that are not written yet.
std::unique_ptr<Screen> makePlaceholder(ScreenId id);

} // namespace opense4::client::classic
