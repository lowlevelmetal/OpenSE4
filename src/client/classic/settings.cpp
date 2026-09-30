#include "client/classic/settings.hpp"

#include "game/setup.hpp"

#include "client/classic/session.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>

namespace opense4::client::classic {

namespace {

using S = ClassicSettings;

constexpr std::array<BoolOption, 22> kBoolOptions{{
    {"General", "show_log_at_turn_start", "Open the log when a turn starts", &S::showLogAtTurnStart},
    {"General", "confirm_end_turn", "Ask before ending the turn", &S::confirmEndTurn},
    {"General", "confirm_scrap", "Ask before scrapping", &S::confirmScrap},
    {"General", "confirm_stellar_manipulation", "Ask before stellar manipulation", &S::confirmStellarManipulation},
    {"General", "confirm_delete_projects", "Ask before removing research or intelligence projects", &S::confirmDeleteProjects},
    {"General", "pick_colony_type_on_colonize", "Choose a colony type when a colony is founded", &S::pickColonyTypeOnColonize},
    {"Next \\ Previous", "cycle_skips_under_construction", "Skip ships that are still being built", &S::cycleSkipsUnderConstruction},
    {"Next \\ Previous", "cycle_skips_damaged", "Skip damaged ships", &S::cycleSkipsDamaged},
    {"Next \\ Previous", "cycle_once_per_location", "Stop only once per location", &S::cycleOncePerLocation},
    {"Ship Movement", "avoid_minefields", "Route around known minefields", &S::avoidMinefields},
    {"Ship Movement", "avoid_restricted_systems", "Route around systems marked to avoid", &S::avoidRestrictedSystems},
    {"Ship Orders", "clear_orders_on_enemy_contact", "Clear orders on entering a system with enemies", &S::clearOrdersOnEnemyContact},
    {"Ship Orders", "clear_orders_on_any_contact", "Clear orders on entering a system with any other empire", &S::clearOrdersOnAnyContact},
    {"System Display", "show_warp_point_names", "Show where known warp points lead", &S::showWarpPointNames},
    {"System Display", "show_planet_names", "Show planet names", &S::showPlanetNames},
    {"System Display", "show_facility_markers", "Show facility markers on colonies", &S::showFacilityMarkers},
    {"System Display", "show_movement_lines", "Show movement lines", &S::showMovementLines},
    {"System Display", "show_waypoint_markers", "Show waypoint markers", &S::showWaypointMarkers},
    {"System Display", "show_colonization_markers", "Show colonization markers on planets", &S::showColonizationMarkers},
    {"Sound", "sound_on", "Play sound effects", &S::soundOn},
    {"Sound", "music_on", "Play music", &S::musicOn},
    {"Sound", "remastered_sounds", "Use the remastered sound set when the game has it", &S::remasteredSounds},
}};

std::filesystem::path settingsFile() { return userDataDir() / "classic_settings.toml"; }

std::unique_ptr<ClassicSettings>& instance() {
    static std::unique_ptr<ClassicSettings> s;
    return s;
}

} // namespace

std::span<const BoolOption> boolOptions() { return kBoolOptions; }

std::string settingsToToml(const ClassicSettings& s) {
    toml::table options;
    for (const BoolOption& o : kBoolOptions) options.insert(o.key, s.*o.member);
    toml::table replay;
    replay.insert("speed", double(s.replaySpeed));
    toml::table sound;
    sound.insert("effects_volume", double(s.soundVolume));
    sound.insert("music_volume", double(s.musicVolume));
    toml::table root;
    root.insert("options", std::move(options));
    root.insert("replay", std::move(replay));
    root.insert("sound", std::move(sound));
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
    if (auto speed = root["replay"]["speed"].value<double>()) s.replaySpeed = std::clamp(float(*speed), 0.25f, 8.0f);
    if (auto v = root["sound"]["effects_volume"].value<double>()) s.soundVolume = std::clamp(float(*v), 0.0f, 1.0f);
    if (auto v = root["sound"]["music_volume"].value<double>()) s.musicVolume = std::clamp(float(*v), 0.0f, 1.0f);
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

std::string hashPassword(std::string_view password) { return game::hashPassword(password); }

} // namespace opense4::client::classic
