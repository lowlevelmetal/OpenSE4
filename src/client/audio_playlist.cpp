#include "client/audio.hpp"

#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

// The parts of audio.hpp that need no audio device (tested directly).

namespace opense4::client {

namespace {

std::vector<std::string> readList(const ruleset::Settings& s, std::string_view kind) {
    std::vector<std::string> out;
    const int64_t n = s.integer(std::format("Num {} Songs", kind), 0);
    for (int64_t i = 1; i <= std::clamp<int64_t>(n, 0, 64); ++i) {
        if (auto f = s.text(std::format("{} Song {} Filename", kind, i)); f && !f->empty()) {
            out.push_back(*f);
        } else if (const int64_t track = s.integer(std::format("{} Song {}", kind, i), 0); track > 1) {
            // The older key: a CD track number; the install's MP3s are numbered one lower.
            out.push_back(std::format("Space Empires IV - Track {:02}.mp3", track - 1));
        }
    }
    return out;
}

} // namespace

Playlists readPlaylists(const ruleset::Settings& settings) {
    if (!settings.boolean("Allow CD Music", true)) return {};
    return {readList(settings, "Intro"), readList(settings, "Background"), readList(settings, "Combat")};
}

std::string_view stellarSound(std::string_view title) {
    // The titles game/movement_stellar.cpp gives its reports.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> kSounds{{
        {"Planet Created: ", "crteplnt"},
        {"Planet Destroyed: ", "destplnt"},
        {"Star Created: ", "crtesun"},
        {"Star Destroyed: ", "destsun"},
        {"Warp Point Opened to ", "openwp"},
        {"Warp Point Closed: ", "closewp"},
        {"Storm created in ", "crtestrm"},
        {"Storm destroyed in ", "deststrm"},
        {"Nebula created in ", "destsun"},
        {"Black hole created in ", "destsun"},
    }};
    for (const auto& [prefix, sound] : kSounds)
        if (title.starts_with(prefix)) return sound;
    return {};
}

int musicStep(float volume) { return std::clamp(static_cast<int>(std::lround(volume * 5.0f)), 0, 5); }

float musicGain(int step) {
    static constexpr std::array<float, 6> kDecibels{0, -30, -20, -10, -5, 0};
    if (step <= 0) return 0.0f;
    return std::pow(10.0f, kDecibels[static_cast<size_t>(std::min(step, 5))] / 20.0f);
}

std::string MusicDirector::pick(const std::vector<std::string>& list) {
    if (list.empty()) return {};
    return list[static_cast<size_t>(rng_.below(list.size()))];
}

std::string MusicDirector::cue(MusicCue c, const Playlists& lists, uint32_t turn, bool inGame) {
    switch (c) {
        case MusicCue::IntroOpened: return pick(lists.intro);
        case MusicCue::GameLoaded:
        case MusicCue::ReplayClosed: return pick(lists.background);
        case MusicCue::TurnProcessed: return turn % 5 == 0 ? pick(lists.background) : std::string{};
        case MusicCue::CombatOpened: return pick(lists.combat);
        case MusicCue::NothingPlaying: return pick(inGame ? lists.background : lists.intro);
    }
    return {};
}

std::vector<std::string> soundCandidates(std::string_view name, bool remastered) {
    std::string file(name);
    if (file.find('.') == std::string::npos) file += ".wav";
    std::vector<std::string> out;
    if (remastered) out.push_back("Sounds/New/" + file);
    out.push_back("Sounds/" + file);
    return out;
}

} // namespace opense4::client
