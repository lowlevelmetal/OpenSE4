#include "ruleset/ruleset.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

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

std::string Settings::normalize(std::string_view key) { return datafile::normalizeKey(key); }
void Settings::set(std::string key, std::string value) { values_[normalize(key)] = std::move(value); }
bool Settings::has(std::string_view key) const { return values_.contains(normalize(key)); }

std::optional<std::string> Settings::text(std::string_view key) const {
    const auto it = values_.find(normalize(key));
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

int64_t Settings::integer(std::string_view key, int64_t fallback) const {
    if (auto v = text(key)) return datafile::parseInteger(*v).value_or(fallback);
    return fallback;
}

bool Settings::boolean(std::string_view key, bool fallback) const {
    if (auto v = text(key)) return datafile::parseBoolean(*v).value_or(fallback);
    return fallback;
}

std::optional<std::filesystem::path> findInstalledDataDir(const std::filesystem::path& hint) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto isDataDir = [&](const fs::path& p) { return fs::exists(p / "Components.txt", ec) && fs::exists(p / "TechArea.txt", ec); };
    // Accept the Data dir itself, the game dir, or the Steam app dir.
    auto resolve = [&](const fs::path& p) -> std::optional<fs::path> {
        for (const fs::path& candidate : {p, p / "Data", p / "se4" / "Data"})
            if (isDataDir(candidate)) return fs::weakly_canonical(candidate, ec);
        return std::nullopt;
    };
    if (!hint.empty()) return resolve(hint);

    std::vector<fs::path> steamRoots;
    if (const char* home = std::getenv("HOME")) {
        steamRoots.push_back(fs::path(home) / ".local/share/Steam");
        steamRoots.push_back(fs::path(home) / ".steam/steam");
        steamRoots.push_back(fs::path(home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
    }
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
