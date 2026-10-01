#pragma once

// Client-side preferences of the classic client that belong to this computer,
// not to a game: the Options window (Game Menu → Options) and the tactical
// display switches of Combat Options (docs/spec/06 §1.9, §1.10.3), plus a few
// of OpenSE4's own. They live in <userDataDir>/classic_settings.toml.
//
// The Empire Options belong to the empire and are saved with the game
// (game::InterfaceOptions, UiContext::options()); so do the minister switches
// and the game's autosave choice.

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

struct ClassicSettings {
    // Options window (spec 06 §1.9, confirmed: binary); the defaults are a
    // fresh install's.
    bool animateSystemMovement = true;   // ships glide to their new square in the system window
    bool animateCombatMovement = true;   // pieces move and fire step by step in tactical combat
    bool soundOn = true;
    bool classicSoundEffects = false;    // the original set in Sounds/ instead of the remastered Sounds/New/
    bool musicOn = true;
    int musicVolume = 100;               // percent, one of kMusicVolumes
    bool fastTacticalCombat = false;     // no pauses between animation steps
    bool showMovementLines = false;      // the system window's movement lines (Ctrl+L)

    // Combat Options' display switches (spec 06 §1.10.3), shared by every
    // tactical battle on this computer.
    bool showGroupIdentifiers = false;
    bool showViewingRectangle = true;    // the dotted box on the overview map
    bool centerOnCurrentShip = true;
    bool showToHitChances = false;
    bool tacticalGrid = false;

    // OpenSE4's own.
    float soundVolume = 0.8f;            // effects volume (the original has none)

    // The last game saved on this computer, which Resume Game loads (spec 06 §1.9, §6.1).
    std::string lastSavedGame;

    // Learning to play (docs/LEARNING.md): the tutorials and training games
    // finished on this machine, as "tutorial:<slug>" and "training:<slug>".
    std::vector<std::string> learnDone;
};

// The Options window's music steps (spec 06 §1.9).
inline constexpr std::array<int, 5> kMusicVolumes{20, 40, 60, 80, 100};

// The settings of this machine, loaded on first use.
ClassicSettings& settings();
// Writes the settings file; returns false (and logs) on failure.
bool saveSettings();

// One on/off switch kept in the settings file.
struct BoolOption {
    const char* key;     // TOML key
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
