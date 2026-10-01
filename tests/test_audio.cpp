// Sound and music lookup (client/audio.hpp; no audio device needed).

#include "client/audio.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;

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
