// Sound and music lookup (client/audio.hpp; no audio device needed), and
// when the game's window counts as in the background (client/window_presence.hpp).

#include "assets/assets.hpp"
#include "client/audio.hpp"
#include "client/classic/settings.hpp"
#include "client/window_presence.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

using namespace opense4;

namespace {

SDL_Event windowEvent(Uint32 type, SDL_WindowID window = 7) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.window.windowID = window;
    return e;
}

} // namespace

TEST_CASE("audio: the game's window in the background, as each system reports it") {
    client::WindowPresence p;
    auto follow = [&](std::initializer_list<Uint32> types) {
        for (const Uint32 t : types) p.follow(windowEvent(t), 7);
        return p.background();
    };
    CHECK_FALSE(p.background());  // it starts in the foreground
    // Another window takes the focus (Alt+Tab, a click elsewhere), and back.
    CHECK(follow({SDL_EVENT_WINDOW_FOCUS_LOST}));
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_FOCUS_GAINED}));
    // Windows: minimized and restored; the focus comes back after the restore.
    CHECK(follow({SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MINIMIZED}));
    CHECK(follow({SDL_EVENT_WINDOW_RESTORED}));
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_FOCUS_GAINED}));
    // X11: minimized and covered, then shown, restored and exposed.
    CHECK(follow({SDL_EVENT_WINDOW_MINIMIZED, SDL_EVENT_WINDOW_OCCLUDED, SDL_EVENT_WINDOW_FOCUS_LOST}));
    CHECK(follow({SDL_EVENT_WINDOW_RESTORED, SDL_EVENT_WINDOW_FOCUS_GAINED}));  // still covered
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_EXPOSED}));
    // Wayland: a suspended window is only covered, even with the focus kept.
    CHECK(follow({SDL_EVENT_WINDOW_OCCLUDED}));
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_EXPOSED}));
    // macOS: the application hidden (Cmd+H), then shown again.
    CHECK(follow({SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_HIDDEN, SDL_EVENT_WINDOW_OCCLUDED}));
    CHECK(follow({SDL_EVENT_WINDOW_SHOWN, SDL_EVENT_WINDOW_EXPOSED}));
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_FOCUS_GAINED}));
    // Shown again counts as no longer minimized, as SDL keeps its flags.
    CHECK(follow({SDL_EVENT_WINDOW_MINIMIZED}));
    CHECK_FALSE(follow({SDL_EVENT_WINDOW_SHOWN}));
    // Other windows' events and other events change nothing.
    p.follow(windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, 8), 7);
    p.follow(windowEvent(SDL_EVENT_KEY_DOWN), 7);
    p.follow(windowEvent(SDL_EVENT_WINDOW_RESIZED), 7);
    CHECK_FALSE(p.background());
}

TEST_CASE("audio: muting in the background is on by default and kept with the sound settings") {
    CHECK(client::AudioOptions{}.muteInBackground);
    client::classic::ClassicSettings s;
    CHECK(s.muteInBackground);
    s.muteInBackground = false;
    const std::string text = client::classic::settingsToToml(s);
    CHECK(text.find("mute_in_background = false") != std::string::npos);
    CHECK_FALSE(client::classic::settingsFromToml(text).muteInBackground);
    CHECK(client::classic::settingsFromToml("[sound]\neffects_volume = 0.5\n").muteInBackground);  // older files
}

TEST_CASE("audio: playlists come from the Settings song keys") {
    ruleset::Settings s;
    s.set("Num Intro Songs", "1");
    s.set("Intro Song 1 Filename", "intro.mp3");
    s.set("Num Background Songs", "3");
    s.set("Background Song 1 Filename", "a.mp3");
    s.set("Background Song 2 Filename", "");  // blank entries are skipped
    s.set("Background Song 3 Filename", "c.mp3");
    const client::Playlists p = client::readPlaylists(s);
    CHECK(p.intro == std::vector<std::string>{"intro.mp3"});
    CHECK(p.background == std::vector<std::string>{"a.mp3", "c.mp3"});
    CHECK(p.combat.empty());
}

TEST_CASE("audio: sound names resolve to the classic sound folders") {
    CHECK(client::soundCandidates("button", true) == std::vector<std::string>{"Sounds/New/button.wav", "Sounds/button.wav"});
    CHECK(client::soundCandidates("button", false) == std::vector<std::string>{"Sounds/button.wav"});
    CHECK(client::soundCandidates("zap.wav", false) == std::vector<std::string>{"Sounds/zap.wav"});
}

TEST_CASE("audio: the older CD-track keys and the music switch of Settings") {
    ruleset::Settings s;
    s.set("Num Combat Songs", "2");
    s.set("Combat Song 1 Filename", "battle.mp3");
    s.set("Combat Song 2", "11");  // a CD track: the MP3 numbered one lower
    client::Playlists p = client::readPlaylists(s);
    CHECK(p.combat == std::vector<std::string>{"battle.mp3", "Space Empires IV - Track 10.mp3"});
    s.set("Allow CD Music", "FALSE");
    p = client::readPlaylists(s);
    CHECK(p.combat.empty());
}

TEST_CASE("audio: music volume is Off and five steps") {
    CHECK(client::musicStep(0.0f) == 0);
    CHECK(client::musicStep(0.2f) == 1);
    CHECK(client::musicStep(1.0f) == 5);
    CHECK(client::musicStep(0.5f) == 3);
    CHECK(client::musicGain(0) == 0.0f);
    CHECK(client::musicGain(5) == doctest::Approx(1.0f));
    CHECK(client::musicGain(3) == doctest::Approx(0.3162f).epsilon(0.001));   // -10 dB
    CHECK(client::musicGain(1) == doctest::Approx(0.03162f).epsilon(0.001));  // -30 dB
}

TEST_CASE("audio: when the music changes") {
    client::Playlists lists;
    lists.intro = {"intro.mp3"};
    lists.background = {"a.mp3", "b.mp3", "c.mp3"};
    lists.combat = {"fight.mp3"};
    client::MusicDirector d(7);
    using client::MusicCue;
    CHECK(d.cue(MusicCue::IntroOpened, lists) == "intro.mp3");
    CHECK(d.cue(MusicCue::CombatOpened, lists) == "fight.mp3");
    const std::string bg = d.cue(MusicCue::GameLoaded, lists);
    CHECK(std::find(lists.background.begin(), lists.background.end(), bg) != lists.background.end());
    // A new background track only on turns that are multiples of 5.
    CHECK(d.cue(MusicCue::TurnProcessed, lists, 4).empty());
    CHECK_FALSE(d.cue(MusicCue::TurnProcessed, lists, 5).empty());
    CHECK(d.cue(MusicCue::NothingPlaying, lists, 0, false) == "intro.mp3");
    CHECK_FALSE(d.cue(MusicCue::ReplayClosed, lists).empty());
    // The same seed picks the same tracks (its own source, never the game's).
    client::MusicDirector a(42), b(42);
    for (int i = 0; i < 10; ++i) CHECK(a.cue(MusicCue::GameLoaded, lists) == b.cue(MusicCue::GameLoaded, lists));
    CHECK(client::MusicDirector(1).cue(MusicCue::CombatOpened, client::Playlists{}).empty());
}

TEST_CASE("audio: stellar manipulation reports have their sounds") {
    CHECK(client::stellarSound("Planet Created: Rock") == "crteplnt");
    CHECK(client::stellarSound("Planet Destroyed: Rock") == "destplnt");
    CHECK(client::stellarSound("Storm destroyed in Sol") == "deststrm");
    CHECK(client::stellarSound("Black hole created in Sol") == "destsun");
    CHECK(client::stellarSound("Research Center I completed").empty());
}

TEST_CASE("installed data set: every sound and music track the game names is in the install (opt-in)") {
    // The lookups ignore case (as Windows does), so this checks that the names
    // the client plays and the ones the data files give exist in the player's
    // copy, in either sound set, on every platform alike.
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return;
    const auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
    REQUIRE(dir);
    const auto loaded = ruleset::loadRuleset(*dir);
    REQUIRE(loaded.ruleset);
    const assets::InstallFiles files(dir->parent_path());
    // The client's own sounds, and those of the stellar manipulation reports (stellarSound).
    std::set<std::string> sounds{"button", "ordbtn", "close", "cloakon", "cloakoff", "endturn", "boom1", "boom2", "boom3", "cmdbtn",
                                 "crteplnt", "destplnt", "crtesun", "destsun", "openwp", "closewp", "crtestrm", "deststrm"};
    for (const auto& c : loaded.ruleset->components)
        if (!c.weapon.sound.empty()) sounds.insert(c.weapon.sound);
    std::vector<std::string> missing;
    for (const std::string& s : sounds)
        for (const bool remastered : {false, true}) {
            const auto candidates = client::soundCandidates(s, remastered);
            if (std::none_of(candidates.begin(), candidates.end(), [&](const std::string& c) { return files.find(c).has_value(); }))
                missing.push_back(std::format("{} ({})", s, remastered ? "remastered" : "classic"));
        }
    const client::Playlists lists = client::readPlaylists(loaded.ruleset->settings);
    for (const auto* list : {&lists.intro, &lists.background, &lists.combat})
        for (const std::string& track : *list)
            if (!files.find("Music/" + track)) missing.push_back("Music/" + track);
    std::string all;
    for (const std::string& m : missing) all += m + "\n";
    INFO(all);
    CHECK(missing.empty());
}
