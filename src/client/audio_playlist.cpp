#include "client/audio.hpp"

#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <format>

// The parts of audio.hpp that need no audio device (tested directly).

namespace opense4::client {

namespace {

std::vector<std::string> readList(const ruleset::Settings& s, std::string_view kind) {
    std::vector<std::string> out;
    const int64_t n = s.integer(std::format("Num {} Songs", kind), 0);
    for (int64_t i = 1; i <= std::clamp<int64_t>(n, 0, 64); ++i)
        if (auto f = s.text(std::format("{} Song {} Filename", kind, i)); f && !f->empty()) out.push_back(*f);
    return out;
}

} // namespace

Playlists readPlaylists(const ruleset::Settings& settings) {
    return {readList(settings, "Intro"), readList(settings, "Background"), readList(settings, "Combat")};
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
