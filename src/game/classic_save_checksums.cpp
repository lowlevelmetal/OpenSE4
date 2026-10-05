// The seven data-set checksums of the original's saved games (docs/spec/08
// §3.2.1, Appendix B): sums over the records of seven data files, read from
// the files as written, so that a simultaneous game exported for the original
// signs its players in.

#include "game/classic_save.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "ruleset/ruleset.hpp"

#include <array>
#include <format>

namespace opense4::game::classic {

using datafile::keysEqual;

namespace checksum {

namespace {

// A name's code in a list: its 1-based position (`first` for the first), 0 unknown.
template <size_t N>
int64_t codeOf(std::string_view name, const std::array<std::string_view, N>& list, int64_t first = 1) {
    for (size_t i = 0; i < N; ++i)
        if (keysEqual(list[i], name)) return first + static_cast<int64_t>(i);
    return 0;
}

constexpr std::array<std::string_view, 8> kHullTypes{"Ship", "Base", "Fighter", "Satellite", "Mine", "Troop", "Drone", "Weapon Platform"};
constexpr std::array<std::string_view, 15> kComponentTypes{
    "Ship",          "Base",    "Fighter",  "Satellite", "Mine", "Troop",
    "Drone",         "WeapPlatform",        "Ship\\Base", "Ftr\\Trp",
    "Ship\\Base\\Sat\\WeapPlat\\Drone",     "Ship\\Base\\Sat\\Drone",
    "Ship\\Base\\Sat",                      "Ship\\Base\\Drone",
    "All",
};
// Code: index + 1 (1 None, 2 one per vehicle .. 11 ten per vehicle).
constexpr std::array<std::string_view, 11> kComponentRestrictions{
    "None",                // 1
    "One Per Vehicle",     // 2
    "Two Per Vehicle",     // 3
    "Three Per Vehicle",   // 4
    "Four Per Vehicle",    // 5
    "Five Per Vehicle",    // 6
    "Six Per Vehicle",     // 7
    "Seven Per Vehicle",   // 8
    "Eight Per Vehicle",   // 9
    "Nine Per Vehicle",    // 10
    "Ten Per Vehicle",     // 11
};
constexpr std::array<std::string_view, 2> kFacilityRestrictions{"None", "One Per Planet"};
// Weapon Type, and a mount's Weapon Type Requirement: None is 0.
constexpr std::array<std::string_view, 6> kWeaponTypes{"None", "Direct Fire", "Seeking", "Point-Defense", "Warhead", "Any"};
constexpr std::array<std::string_view, 4> kDisplayTypes{"None", "Beam", "Torp", "Seeker"};
constexpr std::array<std::string_view, 7> kPhysicalTypes{"Planet", "Asteroids", "Storm", "Star", "Warp Point", "Destroyed Star", "Comet"};
constexpr std::array<std::string_view, 5> kStellarSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
constexpr std::array<std::string_view, 3> kGeneralTypes{"Advantage", "Disadvantage", "Neither"};
constexpr std::array<std::string_view, 5> kSightTypes{"EM Active", "EM Passive", "Psychic", "Gravitic", "Temporal"};
// Appendix B.
constexpr std::array<std::string_view, 32> kDamageTypes{
    "Normal",               "Shields Only",          "Skips Normal Shields",   "Only Engines",           "Only Weapons",
    "Plague Level 1",       "Plague Level 2",        "Plague Level 3",         "Plague Level 4",         "Plague Level 5",
    "Only Planet Population", "Only Planet Conditions", "Only Resupply Depots", "Only Spaceports",       "Pushes Target",
    "Pulls Target",         "Random Target Movement", "Only Shield Generators", "Only Boarding Parties",  "Only Security Stations",
    "Only Planet Destroyers", "Skips Armor",          "Skips Shields And Armor", "Quad Damage To Shields", "Increase Reload Time",
    "Disrupt Reload Time",  "Crew Conversion",       "Skips All Shields",      "Only Master Computers",  "Double Damage To Shields",
    "Half Damage To Shields", "Quarter Damage To Shields",
};
constexpr std::array<std::string_view, 33> kTraitTypes{
    "Reproduction",       "Mineral Production",  "Organics Production",  "Radioactives Production", "Research Production",
    "Intelligence Production", "SY Rate",        "Maintenance Cost",     "Supply Cost",          "No Plagues",
    "Luck",               "No Spaceports",       "Population Happiness", "Vehicle Speed",        "Galaxy Seen",
    "Planet Storage Space", "Planetary SY Rate", "Troops Bonus",         "Fighter Bonus",        "Ship Bonus",
    "Mineral Storage",    "Organics Storage",    "Radioactives Storage", "Production",           "Trade",
    "Space Combat",       "Ground Combat",       "Repair",               "Tech Area",            "Tollerance",
    "Ship Attack",        "Ship Defense",        "Population Emotionless",
};

const std::string& value(const datafile::Record& r, std::string_view key) {
    static const std::string kEmpty;
    const datafile::Field* f = r.find(key);
    return f ? f->value : kEmpty;
}

int64_t number(std::string_view text) { return datafile::parseInteger(text).value_or(0); }
int64_t num(const datafile::Record& r, std::string_view key) { return number(value(r, key)); }
int64_t text(const datafile::Record& r, std::string_view key) { return textLength(value(r, key)); }
int64_t flag(const datafile::Record& r, std::string_view key) { return datafile::parseBoolean(value(r, key)).value_or(false) ? 1 : 0; }

} // namespace

int64_t textLength(std::string_view utf8) {
    // Characters as the original reads the Latin-1 file: one per UTF-8 sequence.
    int64_t n = 0;
    for (char c : utf8)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

int64_t cost(const datafile::Record& r) { return 2 * num(r, "Cost Minerals") + 4 * num(r, "Cost Organics") + 6 * num(r, "Cost Radioactives"); }

int64_t requirements(const datafile::Record& r, const datafile::DataFile& techAreas) {
    int64_t sum = 0;
    const int64_t count = num(r, "Number of Tech Req");
    for (int64_t n = 1; n <= count; ++n) {
        const std::string& area = value(r, std::format("Tech Area Req {}", n));
        int64_t position = 0;
        for (size_t i = 0; i < techAreas.records.size(); ++i)
            if (keysEqual(value(techAreas.records[i], "Name"), area)) {
                position = static_cast<int64_t>(i) + 1;
                break;
            }
        sum += 4 * position + 7 * num(r, std::format("Tech Level Req {}", n));
    }
    return sum;
}

int64_t abilities(const datafile::Record& r) {
    int64_t sum = 0;
    const int64_t count = std::min<int64_t>(num(r, "Number of Abilities"), 20);
    for (int64_t n = 1; n <= count; ++n) {
        const std::string& type = value(r, std::format("Ability {} Type", n));
        sum += abilityId(type).value_or(0);
        sum += textLength(value(r, std::format("Ability {} Descr", n)));
        const std::string& v1 = value(r, std::format("Ability {} Val 1", n));
        // Cloak Level and Sensor Level name a sight type in value 1.
        if (keysEqual(type, "Cloak Level") || keysEqual(type, "Sensor Level")) sum += codeOf(v1, kSightTypes);
        else sum += number(v1);
        sum += number(value(r, std::format("Ability {} Val 2", n)));
    }
    return sum;
}

int64_t damages(std::string_view raw) {
    // Only the numbers followed by a space count, at most 20 of them.
    int64_t sum = 0;
    int counted = 0;
    size_t i = 0;
    while (i < raw.size() && counted < 20) {
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
        const size_t start = i;
        while (i < raw.size() && raw[i] != ' ' && raw[i] != '\t') ++i;
        if (i == start) break;
        if (i >= raw.size()) break;   // nothing after the last number
        sum += number(raw.substr(start, i - start));
        ++counted;
    }
    return sum;
}

int64_t weaponTypeCode(std::string_view name) { return codeOf(name, kWeaponTypes, 0); }
int64_t damageTypeCode(std::string_view name) { return codeOf(name, kDamageTypes); }
int64_t traitTypeCode(std::string_view name) { return codeOf(name, kTraitTypes); }

int32_t component(const datafile::Record& r, size_t position, const datafile::DataFile& techAreas) {
    int64_t sum = static_cast<int64_t>(position) + num(r, "Pic Num") + 3 * num(r, "Tonnage Space Taken") + 5 * num(r, "Tonnage Structure") +
                  cost(r) + codeOf(value(r, "Vehicle Type"), kComponentTypes) + num(r, "Supply Amount Used") +
                  codeOf(value(r, "Restrictions"), kComponentRestrictions) + num(r, "Family") + num(r, "Roman Numeral") +
                  num(r, "Custom Group") + requirements(r, techAreas) + abilities(r) + text(r, "Name") + text(r, "Description") +
                  text(r, "General Group");
    const int64_t weapon = weaponTypeCode(value(r, "Weapon Type"));
    sum += weapon;
    if (weapon != 0) {
        const datafile::Field* line = r.find("Weapon Damage At Rng");
        sum += line ? damages(line->raw) : 0;
        sum += damageTypeCode(value(r, "Weapon Damage Type")) + num(r, "Weapon Reload Rate") +
               codeOf(value(r, "Weapon Display Type"), kDisplayTypes) + num(r, "Weapon Display") + num(r, "Weapon Modifier") +
               text(r, "Weapon Sound") + num(r, "Weapon Family");
        if (weapon == 2) sum += num(r, "Weapon Seeker Speed") + num(r, "Weapon Seeker Dmg Res");
    }
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t facility(const datafile::Record& r, size_t position, const datafile::DataFile& techAreas) {
    const int64_t sum = static_cast<int64_t>(position) + text(r, "Name") + text(r, "Description") + text(r, "Facility Group") +
                        num(r, "Facility Family") + num(r, "Roman Numeral") + codeOf(value(r, "Restrictions"), kFacilityRestrictions) +
                        num(r, "Pic Num") + cost(r) + requirements(r, techAreas) + abilities(r);
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t vehicleSize(const datafile::Record& r, size_t position, const datafile::DataFile& techAreas) {
    const int64_t sum = static_cast<int64_t>(position) + text(r, "Name") + text(r, "Short Name") + text(r, "Description") + text(r, "Code") +
                        text(r, "Primary Bitmap Name") + text(r, "Alternate Bitmap Name") + codeOf(value(r, "Vehicle Type"), kHullTypes) +
                        cost(r) + num(r, "Tonnage") + num(r, "Engines Per Move") + requirements(r, techAreas) + abilities(r) +
                        flag(r, "Requirement Must Have Bridge") + flag(r, "Requirement Can Have Aux Con") + flag(r, "Requirement Uses Engines") +
                        num(r, "Requirement Min Life Support") + num(r, "Requirement Min Crew Quarters") + num(r, "Requirement Max Engines") +
                        num(r, "Requirement Pct Fighter Bays") + num(r, "Requirement Pct Colony Mods") + num(r, "Requirement Pct Cargo");
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t planetSize(const datafile::Record& r, size_t position) {
    // Max Population Domed is not counted.
    const int64_t sum = static_cast<int64_t>(position) + codeOf(value(r, "Physical Type"), kPhysicalTypes) +
                        codeOf(value(r, "Stellar Size"), kStellarSizes) + num(r, "Max Facilities") + num(r, "Max Population") +
                        num(r, "Max Cargo Spaces") + num(r, "Max Facilities Domed") + num(r, "Max Cargo Spaces Domed") +
                        num(r, "Special Ability ID");
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t techArea(const datafile::Record& r, size_t position, const datafile::DataFile& techAreas) {
    const int64_t sum = static_cast<int64_t>(position) + text(r, "Name") + text(r, "Group") + text(r, "Description") +
                        num(r, "Maximum Level") + num(r, "Level Cost") + num(r, "Start Level") + num(r, "Raise Level") +
                        num(r, "Racial Area") + num(r, "Unique Area") + requirements(r, techAreas);
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t mount(const datafile::Record& r, size_t position) {
    // Shield Percent and Vehicle Size Maximum are not counted.
    const int64_t sum = static_cast<int64_t>(position) + num(r, "Cost Percent") + num(r, "Tonnage Percent") + num(r, "Tonnage Structure Percent") +
                        num(r, "Damage Percent") + num(r, "Supply Percent") + num(r, "Range Modifier") + num(r, "Weapon To Hit Modifier") +
                        num(r, "Vehicle Size Minimum") + weaponTypeCode(value(r, "Weapon Type Requirement"));
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

int32_t racialTrait(const datafile::Record& r, size_t position, const datafile::DataFile& traits) {
    auto traitPosition = [&](std::string_view name) -> int64_t {
        for (size_t i = 0; i < traits.records.size(); ++i)
            if (keysEqual(value(traits.records[i], "Name"), name)) return static_cast<int64_t>(i) + 1;
        return 0;   // None, or a name the file lacks
    };
    int64_t sum = static_cast<int64_t>(position) + num(r, "Pic Num") + codeOf(value(r, "General Type"), kGeneralTypes) + num(r, "Cost") +
                  traitTypeCode(value(r, "Trait Type")) + num(r, "Value 1") + num(r, "Value 2");
    for (int n = 1; n <= 3; ++n) {
        sum += traitPosition(value(r, std::format("Required Trait {}", n)));
        sum += traitPosition(value(r, std::format("Restricted Trait {}", n)));
    }
    return static_cast<int32_t>(static_cast<uint32_t>(sum));
}

} // namespace checksum

namespace {

template <class Term>
int32_t sumOf(const datafile::DataFile& file, Term term) {
    uint32_t sum = 0;
    for (size_t i = 0; i < file.records.size(); ++i) sum += static_cast<uint32_t>(term(file.records[i], i + 1));
    return static_cast<int32_t>(sum);
}

} // namespace

DataSetChecksums dataSetChecksums(const ChecksumFiles& f) {
    using namespace checksum;
    return {
        sumOf(f.components, [&](const datafile::Record& r, size_t p) { return component(r, p, f.techAreas); }),
        sumOf(f.facilities, [&](const datafile::Record& r, size_t p) { return facility(r, p, f.techAreas); }),
        sumOf(f.vehicleSizes, [&](const datafile::Record& r, size_t p) { return vehicleSize(r, p, f.techAreas); }),
        sumOf(f.planetSizes, [](const datafile::Record& r, size_t p) { return planetSize(r, p); }),
        sumOf(f.techAreas, [&](const datafile::Record& r, size_t p) { return techArea(r, p, f.techAreas); }),
        sumOf(f.mounts, [](const datafile::Record& r, size_t p) { return mount(r, p); }),
        sumOf(f.racialTraits, [&](const datafile::Record& r, size_t p) { return racialTrait(r, p, f.racialTraits); }),
    };
}

std::expected<DataSetChecksums, std::string> dataSetChecksums(const std::filesystem::path& dataDir) {
    if (dataDir.empty()) return std::unexpected(std::string("the data set was not read from a folder"));
    ChecksumFiles files;
    const std::array<std::pair<datafile::DataFile*, std::string_view>, 7> wanted{{
        {&files.components, "Components.txt"},
        {&files.facilities, "Facility.txt"},
        {&files.vehicleSizes, "VehicleSize.txt"},
        {&files.planetSizes, "PlanetSize.txt"},
        {&files.techAreas, "TechArea.txt"},
        {&files.mounts, "CompEnhancement.txt"},
        {&files.racialTraits, "RacialTraits.txt"},
    }};
    for (const auto& [file, name] : wanted) {
        auto loaded = datafile::load(ruleset::childIgnoringCase(dataDir, name));
        if (!loaded) return std::unexpected(std::format("{}: {}", name, loaded.error()));
        *file = std::move(*loaded);
    }
    return dataSetChecksums(files);
}

} // namespace opense4::game::classic
