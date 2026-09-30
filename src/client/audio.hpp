#pragma once

// Sound effects and music from the player's installed classic game
// (docs/spec/06 §5.5): Sounds/*.wav (or the remastered Sounds/New/ set) and
// the MP3 playlists named in Settings.txt. Plays through SDL3 audio streams;
// without an audio device (headless runs, --no-audio) everything is silent
// and every call is a cheap no-op.

#include "assets/assets.hpp"

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
    float musicVolume = 0.5f;
    bool remastered = true;     // prefer Sounds/New/ when the install has it
};

// The three playlists of Settings.txt ("Num Intro Songs", "Intro Song N Filename", ...).
struct Playlists {
    std::vector<std::string> intro, background, combat;
};
Playlists readPlaylists(const ruleset::Settings& settings);

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

    void play(std::string_view name);
    // Plays the list (file names in Music/), shuffled, looping; the same list keeps playing.
    void playMusic(const std::vector<std::string>& files);
    void stopMusic();
    // Call every frame: keeps the music fed and frees finished sounds.
    void update();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The application's audio.
Audio& audio();

} // namespace opense4::client
