#include "client/classic/data_export.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>

namespace opense4::client::classic {

namespace {

using ruleset::WeaponKind;

// Ranges the damage columns cover: the Weapons Report's two pages.
constexpr int kExportRanges = 20;

std::string_view kindName(WeaponKind k) {
    switch (k) {
        case WeaponKind::None: return "None";
        case WeaponKind::DirectFire: return "Direct Fire";
        case WeaponKind::Seeking: return "Seeking";
        case WeaponKind::Warhead: return "Warhead";
        case WeaponKind::PointDefense: return "Point-Defense";
    }
    return "None";
}

std::string vehicleTypes(ruleset::VehicleTypeMask mask) {
    static constexpr std::array<std::string_view, static_cast<size_t>(ruleset::VehicleType::Count)> kNames{
        "Ship", "Base", "Fighter", "Satellite", "Mine", "Troop", "Drone", "Weapon Platform"};
    std::string out;
    for (size_t i = 0; i < kNames.size(); ++i)
        if (mask & ruleset::maskOf(static_cast<ruleset::VehicleType>(i))) out += (out.empty() ? "" : ", ") + std::string(kNames[i]);
    return out.empty() ? std::string("-") : out;
}

std::string requirements(const game::Rules& r, const std::vector<ruleset::TechRequirement>& reqs) {
    std::string out;
    for (const ruleset::TechRequirement& q : reqs) {
        const std::string area = q.area.valid() && q.area.index() < r.data().techAreas.size() ? r.tech(q.area).name : std::string("?");
        out += std::format("{}{} {}", out.empty() ? "" : "; ", area, q.level);
    }
    return out.empty() ? std::string("-") : out;
}

std::string abilities(const std::vector<ruleset::Ability>& list) {
    std::string out;
    for (const ruleset::Ability& a : list) out += std::format("{}{} ({}/{})", out.empty() ? "" : "; ", a.type, a.value1, a.value2);
    return out.empty() ? std::string("-") : out;
}

// A cell never holds a tab or a line break.
std::string cell(std::string_view text) {
    std::string out(text);
    std::replace(out.begin(), out.end(), '\t', ' ');
    std::replace(out.begin(), out.end(), '\n', ' ');
    std::replace(out.begin(), out.end(), '\r', ' ');
    return out;
}

void line(std::string& out, std::initializer_list<std::string> cells) {
    bool first = true;
    for (const std::string& c : cells) {
        if (!first) out += '\t';
        out += cell(c);
        first = false;
    }
    out += '\n';
}

// Members of a family, lowest level first (ties in data order). Family 0 is
// no family: such parts are left out of the family tables.
std::string memberNames(const game::Rules& r, std::vector<uint32_t> members) {
    std::stable_sort(members.begin(), members.end(),
                     [&](uint32_t a, uint32_t b) { return r.component(a).romanNumeral < r.component(b).romanNumeral; });
    std::string out;
    for (uint32_t c : members) out += (out.empty() ? "" : ", ") + r.component(c).name;
    return out;
}

ExportTable weaponsTable(const game::Rules& r) {
    ExportTable t{"weapons.txt", {}};
    std::string header = "Name\tWeapon Type\tWeapon Family\tLevel\tDamage Type\tTargets\tReload\tTonnage\tStructure\tMinerals\tOrganics\tRadioactives";
    for (int range = 1; range <= kExportRanges; ++range) header += std::format("\tRange {}", range);
    t.text = header + '\n';
    const auto& comps = r.data().components;
    for (uint32_t i = 0; i < comps.size(); ++i) {
        const ruleset::Component& c = comps[i];
        if (!c.isWeapon()) continue;
        std::string targets;
        for (const std::string& target : c.weapon.targets) targets += (targets.empty() ? "" : ", ") + target;
        std::string row = cell(c.name);
        for (const std::string& v :
             {std::string(kindName(c.weapon.kind)), std::to_string(c.weapon.family), std::to_string(c.romanNumeral), c.weapon.damageType,
              targets.empty() ? std::string("-") : targets, std::to_string(c.weapon.reloadRate), std::to_string(c.tonnage),
              std::to_string(c.structure), std::to_string(c.cost.minerals), std::to_string(c.cost.organics), std::to_string(c.cost.radioactives)})
            row += '\t' + cell(v);
        const game::DesignEntry unmounted{i, -1};
        for (int range = 1; range <= kExportRanges; ++range) row += std::format("\t{}", game::weaponDamageAtRange(r, unmounted, range));
        t.text += row + '\n';
    }
    return t;
}

ExportTable componentsTable(const game::Rules& r) {
    ExportTable t{"components.txt", {}};
    line(t.text, {"Name", "Group", "Family", "Level", "Weapon Type", "Tonnage", "Structure", "Minerals", "Organics", "Radioactives",
                  "Supply Used", "Most Per Vehicle", "Vehicle Types", "Requirements", "Abilities"});
    for (const ruleset::Component& c : r.data().components)
        line(t.text, {c.name, c.generalGroup.empty() ? std::string("-") : c.generalGroup, std::to_string(c.family), std::to_string(c.romanNumeral),
                      std::string(kindName(c.weapon.kind)), std::to_string(c.tonnage), std::to_string(c.structure), std::to_string(c.cost.minerals),
                      std::to_string(c.cost.organics), std::to_string(c.cost.radioactives), std::to_string(c.supplyUsed),
                      c.maxPerVehicle > 0 ? std::to_string(c.maxPerVehicle) : std::string("-"), vehicleTypes(c.vehicles),
                      requirements(r, c.requirements), abilities(c.abilities)});
    return t;
}

ExportTable weaponFamiliesTable(const game::Rules& r) {
    ExportTable t{"weapon_families.txt", {}};
    line(t.text, {"Weapon Family", "Weapon Type", "Members", "Weapons"});
    std::map<int, std::vector<uint32_t>> families;
    const auto& comps = r.data().components;
    for (uint32_t i = 0; i < comps.size(); ++i)
        if (comps[i].isWeapon() && comps[i].weapon.family > 0) families[comps[i].weapon.family].push_back(i);
    for (const auto& [family, members] : families)
        line(t.text, {std::to_string(family), std::string(kindName(r.component(members.front()).weapon.kind)), std::to_string(members.size()),
                      memberNames(r, members)});
    return t;
}

ExportTable componentFamiliesTable(const game::Rules& r) {
    ExportTable t{"component_families.txt", {}};
    line(t.text, {"Family", "Group", "Members", "Components"});
    std::map<int, std::vector<uint32_t>> families;
    const auto& comps = r.data().components;
    for (uint32_t i = 0; i < comps.size(); ++i)
        if (comps[i].family > 0) families[comps[i].family].push_back(i);
    for (const auto& [family, members] : families) {
        const std::string& group = r.component(members.front()).generalGroup;
        line(t.text, {std::to_string(family), group.empty() ? std::string("-") : group, std::to_string(members.size()), memberNames(r, members)});
    }
    return t;
}

} // namespace

bool exportAllowed(const ruleset::Settings& data) { return data.boolean("Allow Export of Weapon And Component Data", false); }

std::vector<ExportTable> weaponAndComponentTables(const game::Rules& r) {
    std::vector<ExportTable> out;
    out.push_back(weaponsTable(r));
    out.push_back(componentsTable(r));
    out.push_back(weaponFamiliesTable(r));
    out.push_back(componentFamiliesTable(r));
    return out;
}

std::expected<std::vector<std::filesystem::path>, std::string> writeExportTables(const std::filesystem::path& dir, std::string_view prefix,
                                                                                const std::vector<ExportTable>& tables) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return std::unexpected(std::format("Cannot create {}: {}", dir.string(), ec.message()));
    std::vector<std::filesystem::path> written;
    for (const ExportTable& t : tables) {
        const std::filesystem::path file = dir / (std::string(prefix) + t.fileName);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << t.text;
        if (!out) return std::unexpected(std::format("Cannot write {}", file.string()));
        written.push_back(file);
    }
    return written;
}

} // namespace opense4::client::classic
