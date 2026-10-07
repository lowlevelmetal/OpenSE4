#include "client/classic/settings.hpp"

#include "game/setup.hpp"

#include "client/classic/movement_pace.hpp"
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

constexpr std::array<BoolOption, 13> kBoolOptions{{
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
    {"replay_events", &S::replayEvents},
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
    options.insert("system_movement_speed", s.systemMovementSpeed);
    toml::table sound;
    sound.insert("effects_volume", double(s.soundVolume));
    sound.insert("music_percent", s.musicVolume);
    sound.insert("mute_in_background", s.muteInBackground);
    toml::table games;
    games.insert("last_saved", s.lastSavedGame);
    toml::array done;
    for (const std::string& d : s.learnDone) done.push_back(d);
    toml::table learn;
    learn.insert("done", std::move(done));
    learn.insert("free_play", s.learnFreePlay);
    learn.insert("started", s.learnStarted);
    // Step numbers from 1, as the panel shows them.
    toml::array resume;
    for (const ClassicSettings::ResumeRecord& r : s.learnResume) {
        toml::table t;
        t.insert("lesson", r.lesson);
        t.insert("left_at", int64_t{r.leftAt} + 1);
        t.insert("resume_at", int64_t{r.resumeAt} + 1);
        t.insert("fingerprint", r.fingerprint);
        resume.push_back(std::move(t));
    }
    learn.insert("resume", std::move(resume));
    toml::array enabled;
    for (const std::string& id : s.enabledMods) enabled.push_back(id);
    toml::table mods;
    mods.insert("enabled", std::move(enabled));
    mods.insert("show_ai_notes", s.showAiNotes);
    mods.insert("language", s.modLanguage);
    toml::table root;
    root.insert("options", std::move(options));
    root.insert("sound", std::move(sound));
    root.insert("learn", std::move(learn));
    root.insert("games", std::move(games));
    root.insert("mods", std::move(mods));
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
    // One of the speeds the Options window offers (a whole number reads too).
    if (auto v = root["options"]["system_movement_speed"].value<double>()) s.systemMovementSpeed = kMovementSpeeds[movementSpeedStep(*v)];
    if (auto v = root["sound"]["effects_volume"].value<double>()) s.soundVolume = std::clamp(float(*v), 0.0f, 1.0f);
    if (auto v = root["sound"]["music_percent"].value<int64_t>()) {
        // The nearest of the five steps.
        int best = kMusicVolumes.back();
        for (int step : kMusicVolumes)
            if (std::abs(int(*v) - step) < std::abs(int(*v) - best)) best = step;
        s.musicVolume = best;
    }
    if (auto v = root["sound"]["mute_in_background"].value<bool>()) s.muteInBackground = *v;
    if (auto v = root["games"]["last_saved"].value<std::string>()) s.lastSavedGame = *v;
    if (const toml::array* done = root["learn"]["done"].as_array())
        for (const toml::node& d : *done)
            if (auto v = d.value<std::string>()) s.learnDone.push_back(*v);
    if (const toml::array* enabled = root["mods"]["enabled"].as_array())
        for (const toml::node& n : *enabled)
            if (auto v = n.value<std::string>(); v && !v->empty()) s.enabledMods.push_back(*v);
    if (auto v = root["mods"]["show_ai_notes"].value<bool>()) s.showAiNotes = *v;
    if (auto v = root["mods"]["language"].value<std::string>(); v && !v->empty()) s.modLanguage = *v;
    if (auto v = root["learn"]["free_play"].value<bool>()) s.learnFreePlay = *v;
    if (auto v = root["learn"]["started"].value<bool>()) s.learnStarted = *v;
    if (const toml::array* resume = root["learn"]["resume"].as_array())
        for (const toml::node& n : *resume) {
            const toml::table* t = n.as_table();
            if (!t) continue;
            ClassicSettings::ResumeRecord r;
            r.lesson = (*t)["lesson"].value_or(std::string{});
            const int64_t left = (*t)["left_at"].value_or(int64_t{0}), at = (*t)["resume_at"].value_or(int64_t{0});
            r.fingerprint = (*t)["fingerprint"].value_or(std::string{});
            if (r.lesson.empty() || left < 1 || at < 1 || at > left || left > 10000) continue;   // not one of ours
            r.leftAt = uint32_t(left - 1);
            r.resumeAt = uint32_t(at - 1);
            s.learnResume.push_back(std::move(r));
        }
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
    std::ofstream out(file, std::ios::binary | std::ios::trunc);  // LF line ends on every platform
    if (out) out << settingsToToml(settings());
    if (!out) {
        log::warn("Could not write {}", file.string());
        return false;
    }
    return true;
}

void newGameStarted(bool simultaneous) {
    if (!simultaneous || settings().showMovementLines) return;
    settings().showMovementLines = true;
    saveSettings();
}

void rememberSavedGame(const std::string& file) {
    if (settings().lastSavedGame == file) return;
    settings().lastSavedGame = file;
    saveSettings();
}

std::string hashPassword(std::string_view password) { return game::hashPassword(password); }

} // namespace opense4::client::classic
