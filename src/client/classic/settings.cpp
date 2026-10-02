#include "client/classic/settings.hpp"

#include "game/setup.hpp"

#include "client/classic/session.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "ruleset/ruleset.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>

namespace opense4::client::classic {

namespace {

using S = ClassicSettings;

constexpr std::array<BoolOption, 12> kBoolOptions{{
    {"animate_system_movement", &S::animateSystemMovement},
    {"animate_combat_movement", &S::animateCombatMovement},
    {"sound_on", &S::soundOn},
    {"classic_sound_effects", &S::classicSoundEffects},
    {"music_on", &S::musicOn},
    {"fast_tactical_combat", &S::fastTacticalCombat},
    {"show_movement_lines", &S::showMovementLines},
    {"show_group_identifiers", &S::showGroupIdentifiers},
    {"show_viewing_rectangle", &S::showViewingRectangle},
    {"center_on_current_ship", &S::centerOnCurrentShip},
    {"show_to_hit_chances", &S::showToHitChances},
    {"tactical_grid", &S::tacticalGrid},
}};

std::filesystem::path settingsFile() { return userDataDir() / "classic_settings.toml"; }

std::unique_ptr<ClassicSettings>& instance() {
    static std::unique_ptr<ClassicSettings> s;
    return s;
}

} // namespace

std::span<const BoolOption> boolOptions() { return kBoolOptions; }

bool musicAllowed(const ruleset::Settings& data) { return data.boolean("Allow CD Music", true); }

bool openMusicRows(ClassicSettings& s, bool allowed) {
    if (allowed && s.musicOn) return false;
    const bool changed = s.musicOn;
    s.musicOn = false;
    return changed;
}

bool musicLampLit(const ClassicSettings& s, bool allowed) { return s.musicOn && allowed; }

std::string settingsToToml(const ClassicSettings& s) {
    toml::table options;
    for (const BoolOption& o : kBoolOptions) options.insert(o.key, s.*o.member);
    toml::table sound;
    sound.insert("effects_volume", double(s.soundVolume));
    sound.insert("music_percent", s.musicVolume);
    toml::table games;
    games.insert("last_saved", s.lastSavedGame);
    toml::array done;
    for (const std::string& d : s.learnDone) done.push_back(d);
    toml::table learn;
    learn.insert("done", std::move(done));
    toml::table root;
    root.insert("options", std::move(options));
    root.insert("sound", std::move(sound));
    root.insert("learn", std::move(learn));
    root.insert("games", std::move(games));
    std::ostringstream out;
    out << "# OpenSE4 classic client preferences\n" << root << "\n";
    return out.str();
}

ClassicSettings settingsFromToml(std::string_view text, std::string* error) {
    ClassicSettings s;
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (error) *error = std::string(e.description());
        return s;
    }
    if (const toml::table* options = root["options"].as_table())
        for (const BoolOption& o : kBoolOptions)
            if (auto v = (*options)[o.key].value<bool>()) s.*o.member = *v;
    if (auto v = root["sound"]["effects_volume"].value<double>()) s.soundVolume = std::clamp(float(*v), 0.0f, 1.0f);
    if (auto v = root["sound"]["music_percent"].value<int64_t>()) {
        // The nearest of the five steps.
        int best = kMusicVolumes.back();
        for (int step : kMusicVolumes)
            if (std::abs(int(*v) - step) < std::abs(int(*v) - best)) best = step;
        s.musicVolume = best;
    }
    if (auto v = root["games"]["last_saved"].value<std::string>()) s.lastSavedGame = *v;
    if (const toml::array* done = root["learn"]["done"].as_array())
        for (const toml::node& d : *done)
            if (auto v = d.value<std::string>()) s.learnDone.push_back(*v);
    return s;
}

ClassicSettings& settings() {
    auto& s = instance();
    if (!s) {
        s = std::make_unique<ClassicSettings>();
        const std::filesystem::path file = settingsFile();
        std::ifstream in(file);
        if (in) {
            std::stringstream text;
            text << in.rdbuf();
            std::string error;
            *s = settingsFromToml(text.str(), &error);
            if (!error.empty()) log::warn("{}: {} (using defaults)", file.string(), error);
        }
    }
    return *s;
}

bool saveSettings() {
    const std::filesystem::path file = settingsFile();
    std::ofstream out(file, std::ios::trunc);
    if (out) out << settingsToToml(settings());
    if (!out) {
        log::warn("Could not write {}", file.string());
        return false;
    }
    return true;
}

void rememberSavedGame(const std::string& file) {
    if (settings().lastSavedGame == file) return;
    settings().lastSavedGame = file;
    saveSettings();
}

std::string hashPassword(std::string_view password) { return game::hashPassword(password); }

} // namespace opense4::client::classic
