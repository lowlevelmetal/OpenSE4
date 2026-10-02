#include "ruleset/ruleset.hpp"

#include "ruleset/ability_names.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace opense4::ruleset {

namespace {

template <class Vec, class NameOf>
void buildIndex(std::unordered_map<std::string, uint32_t>& index, const Vec& items, NameOf&& nameOf) {
    index.clear();
    for (size_t i = 0; i < items.size(); ++i) index.emplace(datafile::normalizeKey(nameOf(items[i])), static_cast<uint32_t>(i));
}

std::optional<uint32_t> lookup(const std::unordered_map<std::string, uint32_t>& index, std::string_view name) {
    const auto it = index.find(datafile::normalizeKey(name));
    if (it == index.end()) return std::nullopt;
    return it->second;
}

} // namespace

void Ruleset::reindex() {
    buildIndex(techIndex_, techAreas, [](const TechArea& t) -> const std::string& { return t.name; });
    buildIndex(systemIndex_, systemTypes, [](const SystemType& t) -> const std::string& { return t.name; });
    buildIndex(stellarIndex_, stellarAbilityTypes, [](const StellarAbilityType& t) -> const std::string& { return t.name; });
    buildIndex(componentIndex_, components, [](const Component& c) -> const std::string& { return c.name; });
    buildIndex(facilityIndex_, facilities, [](const Facility& f) -> const std::string& { return f.name; });
    buildIndex(vehicleIndex_, vehicleSizes, [](const VehicleSize& v) -> const std::string& { return v.name; });
}

std::optional<TechAreaId> Ruleset::findTechArea(std::string_view name) const {
    if (auto i = lookup(techIndex_, name)) return TechAreaId{*i};
    return std::nullopt;
}
std::optional<SystemTypeId> Ruleset::findSystemType(std::string_view name) const {
    if (auto i = lookup(systemIndex_, name)) return SystemTypeId{*i};
    return std::nullopt;
}
std::optional<StellarAbilityTypeId> Ruleset::findStellarAbilityType(std::string_view name) const {
    if (auto i = lookup(stellarIndex_, name)) return StellarAbilityTypeId{*i};
    return std::nullopt;
}
const Component* Ruleset::findComponent(std::string_view name) const {
    auto i = lookup(componentIndex_, name);
    return i ? &components[*i] : nullptr;
}
const Facility* Ruleset::findFacility(std::string_view name) const {
    auto i = lookup(facilityIndex_, name);
    return i ? &facilities[*i] : nullptr;
}
const VehicleSize* Ruleset::findVehicleSize(std::string_view name) const {
    auto i = lookup(vehicleIndex_, name);
    return i ? &vehicleSizes[*i] : nullptr;
}

AbilityNameStatus abilityNameStatus(std::string_view type) {
    static const std::unordered_map<std::string, AbilityNameStatus> kNames = [] {
        std::unordered_map<std::string, AbilityNameStatus> m;
#define OPENSE4_ABILITY_NAME(name, text) m.emplace(datafile::normalizeKey(text), AbilityNameStatus::Known);
        OPENSE4_ABILITIES(OPENSE4_ABILITY_NAME)
#undef OPENSE4_ABILITY_NAME
        // Listed in a file header, but not a type the original knows (spec 01 §4.4).
        for (const char* ignored : {"System - Sensor Interference", "System - Damage", "System - Ability Required"})
            m[datafile::normalizeKey(ignored)] = AbilityNameStatus::Ignored;
        return m;
    }();
    const std::string key = datafile::normalizeKey(type);
    if (key.starts_with("ai tag")) return AbilityNameStatus::Known;
    const auto it = kNames.find(key);
    return it == kNames.end() ? AbilityNameStatus::Unknown : it->second;
}

std::string Settings::normalize(std::string_view key) { return datafile::normalizeKey(key); }
void Settings::set(std::string key, std::string value) { values_[normalize(key)] = std::move(value); }
bool Settings::has(std::string_view key) const { return values_.contains(normalize(key)); }

std::optional<std::string> Settings::text(std::string_view key) const {
    const auto it = values_.find(normalize(key));
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

// integer() and boolean() run in the engine's inner loops: they parse the
// stored text in place instead of copying it.
int64_t Settings::integer(std::string_view key, int64_t fallback) const {
    const auto it = values_.find(normalize(key));
    return it == values_.end() ? fallback : datafile::parseInteger(it->second).value_or(fallback);
}

bool Settings::boolean(std::string_view key, bool fallback) const {
    const auto it = values_.find(normalize(key));
    return it == values_.end() ? fallback : datafile::parseBoolean(it->second).value_or(fallback);
}

#if defined(_WIN32)
// Where Steam is installed, as it records it (any drive, any folder).
std::optional<std::filesystem::path> steamFolderFromRegistry() {
    for (const auto& [root, key, value] : {std::tuple{HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"},
                                           std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"},
                                           std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"}}) {
        wchar_t buffer[1024];
        DWORD size = sizeof buffer;
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS && buffer[0] != 0)
            return std::filesystem::path(buffer);
    }
    return std::nullopt;
}
#endif

std::filesystem::path childIgnoringCase(const std::filesystem::path& dir, std::string_view name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exact = dir / name;
    if (fs::exists(exact, ec)) return exact;
    auto lowered = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    const std::string want = lowered(std::string(name));
    std::vector<fs::path> found;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (lowered(e.path().filename().string()) == want) found.push_back(e.path());
    if (found.empty()) return exact;
    return *std::min_element(found.begin(), found.end());  // the directory's order is unspecified
}

std::optional<std::filesystem::path> findInstalledDataDir(const std::filesystem::path& hint) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto isDataDir = [&](const fs::path& p) {
        return fs::exists(childIgnoringCase(p, "Components.txt"), ec) && fs::exists(childIgnoringCase(p, "TechArea.txt"), ec);
    };
    // Accept the Data dir itself, the game dir, or the Steam app dir.
    auto resolve = [&](const fs::path& p) -> std::optional<fs::path> {
        for (const fs::path& candidate : {p, childIgnoringCase(p, "Data"), childIgnoringCase(childIgnoringCase(p, "se4"), "Data")})
            if (isDataDir(candidate)) return fs::weakly_canonical(candidate, ec);
        return std::nullopt;
    };
    if (!hint.empty()) return resolve(hint);

    std::vector<fs::path> steamRoots;
    if (const char* home = std::getenv("HOME")) {
        steamRoots.push_back(fs::path(home) / ".local/share/Steam");
        steamRoots.push_back(fs::path(home) / ".steam/steam");
        steamRoots.push_back(fs::path(home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
        steamRoots.push_back(fs::path(home) / "Library/Application Support/Steam");  // macOS
    }
#if defined(_WIN32)
    if (auto steam = steamFolderFromRegistry()) steamRoots.push_back(*steam);
#endif
    if (const char* programFiles = std::getenv("ProgramFiles(x86)")) steamRoots.push_back(fs::path(programFiles) / "Steam");
    steamRoots.push_back("C:/Program Files (x86)/Steam");
    steamRoots.push_back("C:/Program Files/Steam");

    // Every Steam library listed in libraryfolders.vdf, plus the roots themselves.
    std::vector<fs::path> libraries = steamRoots;
    const std::regex pathLine(R"re("path"\s*"([^"]+)")re");
    for (const fs::path& root : steamRoots) {
        std::ifstream in(root / "steamapps" / "libraryfolders.vdf");
        std::string line;
        std::smatch m;
        while (std::getline(in, line))
            if (std::regex_search(line, m, pathLine)) {
                std::string p = m[1].str();
                for (size_t pos; (pos = p.find("\\\\")) != std::string::npos;) p.replace(pos, 2, "/");
                libraries.emplace_back(p);
            }
    }
    for (const fs::path& lib : libraries)
        if (auto found = resolve(lib / "steamapps" / "common" / "Space Empires IV Deluxe")) return found;
    return std::nullopt;
}

} // namespace opense4::ruleset
