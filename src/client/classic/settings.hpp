#pragma once

// Client-side preferences of the classic client: the Empire Options switches
// (docs/spec/06 §1.2) and a few window preferences. They belong to whoever
// plays on this machine, not to the game, and live in
// <userDataDir>/classic_settings.toml. The minister switches belong to the
// empire (game::cmd::SetMinisters).

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

struct ClassicSettings {
    // General.
    bool showLogAtTurnStart = true;
    bool confirmEndTurn = false;
    bool confirmScrap = true;
    bool confirmStellarManipulation = true;
    bool confirmDeleteProjects = true;
    bool pickColonyTypeOnColonize = false;
    // Next / previous selection cycles.
    bool cycleSkipsUnderConstruction = true;
    bool cycleSkipsDamaged = false;
    bool cycleOncePerLocation = false;
    // Ship Movement and Ship Orders: part of the game (Empire::avoidTaggedMinefields,
    // avoidRestrictedSystems, clearOrdersOnEncounter; spec 03 §6.2, §6.4), set
    // with cmd::SetEncounterOptions from the Empire Options window.
    // System display.
    bool showWarpPointNames = true;
    bool showPlanetNames = false;
    bool showFacilityMarkers = false;
    bool showMovementLines = true;
    bool showWaypointMarkers = true;
    bool showColonizationMarkers = true;
    bool systemGrid = false;           // grid lines on the system panel (docs/spec/06 §2.4)
    bool animateShipMovement = true;   // ships glide to their new square instead of jumping
    // Galaxy Display (§1.9, §2.6): the galaxy panel's grid and warp lines.
    bool galaxyGridLines = true;
    bool galaxyWarpLines = true;

    // Sound and music (docs/spec/06 §5.5).
    bool soundOn = true;
    bool musicOn = true;
    bool remasteredSounds = true;
    float soundVolume = 0.8f;
    float musicVolume = 1.0f;   // in the six steps of client::musicStep (100 % on a fresh install, §1.9)

    // Combat Replay playback speed (1 = normal).
    float replaySpeed = 1.0f;

    // Tactical Combat Options (docs/spec/06 §1.6: animation, speed and display
    // options, not itemised by the manual; this list is ours).
    bool tacticalAnimate = true;     // play moves and shots; off: show the result at once
    float tacticalSpeed = 2.0f;      // animation speed (1 = the replay's normal pace)
    bool tacticalGrid = true;        // the square grid
    bool tacticalRanges = true;      // the selected piece's weapon ranges and movement
    bool tacticalNames = false;      // names under the pieces
    bool tacticalAutoEnd = true;     // end the phase when nothing is left to fight

    // The last game saved on this machine, which Resume Game loads (docs/spec/06 §1.9, §6.1).
    std::string lastSavedGame;

    // Learning to play (docs/LEARNING.md): the tutorials and training games
    // finished on this machine, as "tutorial:<slug>" and "training:<slug>".
    std::vector<std::string> learnDone;
};

// The settings of this machine, loaded on first use.
ClassicSettings& settings();
// Writes the settings file; returns false (and logs) on failure.
bool saveSettings();

// One on/off switch of the Empire Options window.
struct BoolOption {
    const char* group;   // heading in the Empire Options list
    const char* key;     // TOML key
    const char* label;   // our own wording
    bool ClassicSettings::*member;
};
std::span<const BoolOption> boolOptions();

// TOML round trip (used by settings()/saveSettings(), exposed for tests).
std::string settingsToToml(const ClassicSettings& s);
ClassicSettings settingsFromToml(std::string_view text, std::string* error = nullptr);

// A stable hash for empire passwords (cmd::SetEmpireOptions::passwordHash).
// An empty password gives an empty hash (no password).
std::string hashPassword(std::string_view password);

} // namespace opense4::client::classic
