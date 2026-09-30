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

constexpr std::array<BoolOption, 19> kBoolOptions{{
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
}};

constexpr std::array<MinisterCategory, 24> kMinisters{{
    {"Design and Ship Construction", true},
    {"Expenses", true},
    {"Production Output", true},
    {"Research", true},
    {"Intelligence", true},
    {"Politics", true},
    {"Repair", true},
    {"Resupply", true},
    {"Scrap", true},
    {"Retrofit", true},
    {"Facility Construction", false},
    {"Transports", false},
    {"Carriers", false},
    {"Colonization", false},
    {"Attack", false},
    {"Defense", false},
    {"Exploration", false},
    {"Patrol", false},
    {"Mines \\ Satellites", false},
    {"Fleets", false},
    {"Stellar Manipulation", false},
    {"Ship Cloaking", false},
    {"Space Yard Ships", false},
    {"Troops", false},
}};

std::filesystem::path settingsFile() { return userDataDir() / "classic_settings.toml"; }

std::unique_ptr<ClassicSettings>& instance() {
    static std::unique_ptr<ClassicSettings> s;
    return s;
}

} // namespace

bool ClassicSettings::ministerOn(std::string_view category) const {
    return std::find(ministers.begin(), ministers.end(), category) != ministers.end();
}

void ClassicSettings::setMinister(std::string_view category, bool on) {
    std::erase(ministers, category);
    if (on) ministers.emplace_back(category);
}

std::span<const BoolOption> boolOptions() { return kBoolOptions; }
std::span<const MinisterCategory> ministerCategories() { return kMinisters; }

std::string settingsToToml(const ClassicSettings& s) {
    toml::table options;
    for (const BoolOption& o : kBoolOptions) options.insert(o.key, s.*o.member);
    toml::array ministers;
    for (const std::string& m : s.ministers) ministers.push_back(m);
    toml::table minister;
    minister.insert("categories", std::move(ministers));
    minister.insert("new_vehicles", s.ministersForNewVehicles);
    minister.insert("race_style", s.raceMinisterStyle);
    toml::table replay;
    replay.insert("speed", double(s.replaySpeed));
    toml::table root;
    root.insert("options", std::move(options));
    root.insert("ministers", std::move(minister));
    root.insert("replay", std::move(replay));
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
    if (const toml::table* m = root["ministers"].as_table()) {
        if (const toml::array* list = (*m)["categories"].as_array())
            for (const toml::node& n : *list)
                if (auto name = n.value<std::string>()) s.setMinister(*name, true);
        s.ministersForNewVehicles = (*m)["new_vehicles"].value_or(s.ministersForNewVehicles);
        s.raceMinisterStyle = (*m)["race_style"].value_or(s.raceMinisterStyle);
    }
    if (auto speed = root["replay"]["speed"].value<double>()) s.replaySpeed = std::clamp(float(*speed), 0.25f, 8.0f);
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
