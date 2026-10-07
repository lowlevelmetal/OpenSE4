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
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::ruleset {
class Settings;
}

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
    // How fast ships turn and slide in the system window, as they move and in
    // the movement log replay (Options, under OpenSE4): the original's waits
    // divided by this, one of kMovementSpeeds (movement_pace.hpp). 1, the
    // original's waits, by default.
    double systemMovementSpeed = 1.0;
    // Sound effects and music fade out while the game's window is in the
    // background (another window has the focus, or it is minimized), the
    // music pausing where it is. On by default.
    bool muteInBackground = true;
    // Combat Replay lists each combat turn's events in words, and the battle's
    // summary at its end, in place of its empty weapon grid (Combat Replay
    // Options; the original has no such list, spec 06 §7 Q39). Off by default.
    bool replayEvents = false;

    // The last game saved on this computer, which Resume Game loads (spec 06 §1.9, §6.1).
    std::string lastSavedGame;

    // Mods (docs/sdk/packages-and-data.md): the ids of the mods in
    // <user data>/Mods to play with, in the player's order. --mod on the
    // command line takes their place for one run.
    std::vector<std::string> enabledMods;
    // The AI notes view (docs/sdk/python-api.md "Watching a player think"):
    // the notes script computer players write, on the maps and in the
    // reports (Settings, Modding; Ctrl+Shift+N). Off by default.
    bool showAiNotes = false;
    // The language of the mods' text (their text/<lang>.toml files,
    // docs/sdk/interface.md "Text"): English unless chosen (Settings, Modding).
    std::string modLanguage = "en";

    // Learning to play (docs/LEARNING.md): the tutorials and training games
    // finished on this machine, as "tutorial:<slug>" and "training:<slug>".
    std::vector<std::string> learnDone;
    // Free Play (the lesson panel's switch): tutorials do not lock the input
    // to the step's action. Off by default.
    bool learnFreePlay = false;
    // A lesson was started on this machine (the intro's hint by Tutorial
    // shows until then).
    bool learnStarted = false;
    // Tutorials left before their end, to resume (docs/LEARNING.md "Resuming
    // a lesson"); the game of each is kept beside the settings
    // (learn_content.hpp lessonResumeFile), the most recently left last.
    struct ResumeRecord {
        std::string lesson;        // "tutorial:<slug>"
        uint32_t leftAt = 0;       // the active step when the player left it (0-based)
        uint32_t resumeAt = 0;     // the step it resumes at (learn::resumeStep)
        std::string fingerprint;   // learn::lessonFingerprint then, in hex
        bool operator==(const ResumeRecord&) const = default;
    };
    std::vector<ResumeRecord> learnResume;
};

// The Options window's music steps (spec 06 §1.9).
inline constexpr std::array<int, 5> kMusicVolumes{20, 40, 60, 80, 100};

// Music and Settings.txt (spec 06 §1.9, confirmed: binary). `Allow CD Music`
// (TRUE when the key is missing): with FALSE no track ever plays.
bool musicAllowed(const ruleset::Settings& data);
// The Options window opens: with music off, or not allowed, its rows light
// "Music Off" and the computer's music is stored off at once. Music is
// stored on again only when the player picks a volume lamp (musicOn is that
// lamp state, saved as it changes). True when the setting changed.
bool openMusicRows(ClassicSettings& s, bool allowed);
// The Combat Options "Music On" lamp (spec 06 §1.10.3): lit only when music
// is on and allowed.
bool musicLampLit(const ClassicSettings& s, bool allowed);

// The settings of this machine, loaded on first use.
ClassicSettings& settings();
// Writes the settings file; returns false (and logs) on failure.
bool saveSettings();
// A new game was started from the game setup (or Quick Start, or hosted on
// the network): a simultaneous one switches Display Ship Movement Lines on and
// stores it at once, so it stays on until the player turns it off (spec 06
// §1.9, confirmed: binary). Loading or joining a game changes nothing.
void newGameStarted(bool simultaneous);
// Records a game file just saved as the one Resume Game loads (spec 06 §6.1,
// §7 Q53: every successful save of a game, autosaves included) and writes
// the settings file.
void rememberSavedGame(const std::string& file);

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
