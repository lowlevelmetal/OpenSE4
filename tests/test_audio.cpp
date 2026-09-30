// Sound and music lookup (client/audio.hpp; no audio device needed).

#include "client/audio.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

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
