#pragma once

// Sound effects and music from the player's installed classic game
// (docs/spec/06 §5.5): Sounds/*.wav (or the remastered Sounds/New/ set) and
// the MP3 playlists named in Settings.txt. One SDL3 audio stream is bound to
// the device; the device's audio thread pulls the mix from it
// (client/audio_mixer.hpp), so sound keeps going while the main thread is
// busy (a turn being processed, a window being dragged). Music is decoded on
// a thread of its own. Without an audio device (headless runs, --no-audio)
// everything is silent and every call is a cheap no-op. What happens goes to
// the log (opense4.log): the device, the settings, each track and every file
// that cannot be played, with why (docs/SETUP.md "Sound and music").

#include "assets/assets.hpp"
#include "core/rng.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::ruleset {
class Settings;
}

namespace opense4::client {

struct AudioOptions {
    bool sound = true;
    bool music = true;
    float soundVolume = 0.8f;   // 0..1
    float musicVolume = 1.0f;   // 0..1, played in the six steps of musicStep()
    bool remastered = true;     // prefer Sounds/New/ when the install has it
    bool muteInBackground = true;  // silent while the game's window is in the background
};

// The three playlists of Settings.txt ("Num Intro Songs", "Intro Song N
// Filename", ...; without a file name, "Intro Song N" gives a CD track T and
// the file is the install's MP3 numbered T - 1). All empty when Settings.txt
// does not allow music (docs/spec/06 §5.5).
struct Playlists {
    std::vector<std::string> intro, background, combat;
};
Playlists readPlaylists(const ruleset::Settings& settings);

// The tracks of the playlists the install lacks ("Music/<file>", each once).
std::vector<std::string> missingTracks(const Playlists& lists, const assets::InstallFiles& files);
// Writes to the log what music the game has: the playlists, or why there is
// none, and any track they name that the install lacks (docs/SETUP.md).
void reportPlaylists(const ruleset::Settings& settings, const Playlists& lists, const assets::InstallFiles& files);

// The sound of a stellar manipulation the player sees, from the title of the
// engine's log entry about it (docs/spec/06 §5.5); empty for other entries.
std::string_view stellarSound(std::string_view logTitle);

// Music volume (docs/spec/06 §5.5): Music Off, then 20, 40, 60, 80 and 100 %,
// played at -30, -20, -10, -5 and 0 dB. A volume 0..1 maps to its step 0..5.
int musicStep(float volume);
float musicGain(int step);   // linear gain of a step (0 for Off)

// When the music changes (docs/spec/06 §5.5, confirmed: binary). Every change
// picks one random track of a list and loops it. The tracks come from their
// own random source, never the game's.
enum class MusicCue {
    IntroOpened,     // the intro screen opens: an intro track
    GameLoaded,      // Resume Game, Load Game, Tutorial or Scenario from the intro: background
    TurnProcessed,   // a new turn: a new background track when its number is a multiple of 5
    CombatOpened,    // Tactical Combat or a Combat Replay opens: a combat track
    ReplayClosed,    // a Combat Replay closes: background
    NothingPlaying,  // music on and silent: intro in the front end, background in a game
};
class MusicDirector {
public:
    explicit MusicDirector(uint64_t seed) : rng_(seed) {}
    // The track to switch to (a file in Music/), or empty to keep what plays.
    // New Game and Quick Start give no cue: the intro track plays on into the game.
    std::string cue(MusicCue c, const Playlists& lists, uint32_t turn = 0, bool inGame = true);

private:
    std::string pick(const std::vector<std::string>& list);
    Rng rng_;
};

// Sound file for a name: "button" -> "Sounds/New/button.wav" or "Sounds/button.wav";
// a name with an extension ("zap.wav", from Components.txt) is used as is.
std::vector<std::string> soundCandidates(std::string_view name, bool remastered);

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default playback device. False (silent from then on) if there is none.
    bool open();
    void close();
    bool active() const;

    // The install to read sounds and music from (nullptr: none).
    void setInstall(const assets::InstallFiles* files);
    void setOptions(const AudioOptions& options);
    const AudioOptions& options() const;

    // One effect at a time: a new one cuts off the one playing (docs/spec/06
    // §5.5), with a fade of a few milliseconds.
    void play(std::string_view name);
    // Loops one track (a file name in Music/); the same track keeps playing.
    // What played fades out first. A track that cannot be played is logged
    // and not tried again.
    void playTrack(const std::string& file);
    void stopMusic();  // fades out
    // A track is playing or starting (false again when it fails).
    bool musicPlaying() const;
    // The game's window went into the background (another window has the
    // focus, or it is minimized, hidden or covered) or came back. With
    // AudioOptions::muteInBackground, everything fades out meanwhile: the music
    // pauses where it is and goes on from there when the window comes back, and
    // sound effects asked for meanwhile are dropped (docs/SETUP.md "Sound and music").
    void setBackground(bool background);
    // Call every frame: reports the music's progress and problems to the log.
    void update();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The application's audio.
Audio& audio();

} // namespace opense4::client
