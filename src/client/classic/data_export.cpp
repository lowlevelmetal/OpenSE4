#include "client/classic/data_export.hpp"

#include "game/design.hpp"

#include <format>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>

namespace opense4::client::classic {

namespace {

// Ranges the damage columns cover: the Weapons Report's two pages.
constexpr int kExportRanges = 20;

// One fixed-width column: text left-aligned, numbers right-aligned (the
// alignment is OpenSE4's, inferred); a longer value is written whole.
struct Column {
    std::string_view heading;
    size_t width;
    bool number = true;
};

std::string padded(std::string_view text, size_t width, bool right) {
    std::string out;
    if (text.size() < width && right) out.append(width - text.size(), ' ');
    for (char c : text) out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    if (text.size() < width && !right) out.append(width - text.size(), ' ');
    return out;
}

// One line of the columns, then `tail` (a last column without a width).
std::string row(std::span<const Column> cols, std::span<const std::string> cells, std::string_view tail = {}) {
    std::string out;
    for (size_t i = 0; i < cols.size(); ++i) out += padded(i < cells.size() ? std::string_view(cells[i]) : std::string_view{}, cols[i].width, cols[i].number);
    out += tail;
    // No trailing blanks.
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + '\n';
}

// The header line, then dashes as long as it.
std::string header(std::span<const Column> cols, std::string_view tail = {}) {
    std::vector<std::string> names;
    for (const Column& c : cols) names.emplace_back(c.heading);
    std::string out = row(cols, names, tail);
    out += std::string(out.size() - 1, '-') + '\n';
    return out;
}

std::string num(int64_t v) { return std::to_string(v); }

// Weapons: name (40), the damage at ranges 1 to 20 (4 each), reload rate,
// tonnage and the three costs (6 each), the damage type's name (20).
ExportTable weaponsTable(const game::Rules& r) {
    std::vector<Column> cols{{"Name", 40, false}};
    std::vector<std::string> rangeNames;
    for (int range = 1; range <= kExportRanges; ++range) rangeNames.push_back(std::to_string(range));
    for (const std::string& n : rangeNames) cols.push_back({n, 4});
    for (std::string_view h : {"Reload", "Tons", "Min", "Org", "Rad"}) cols.push_back({h, 6});
    cols.push_back({" Damage Type", 20, false});
    ExportTable t{"Weapons.txt", header(cols)};
    const auto& comps = r.data().components;
    for (uint32_t i = 0; i < comps.size(); ++i) {
        const ruleset::Component& c = comps[i];
        if (!c.isWeapon()) continue;
        std::vector<std::string> cells{c.name};
        const game::DesignEntry unmounted{i, -1};
        for (int range = 1; range <= kExportRanges; ++range) cells.push_back(num(game::weaponDamageAtRange(r, unmounted, range)));
        for (int64_t v : {int64_t(c.weapon.reloadRate), int64_t(c.tonnage), c.cost.minerals, c.cost.organics, c.cost.radioactives}) cells.push_back(num(v));
        cells.push_back(" " + c.weapon.damageType);
        t.text += row(cols, cells);
    }
    return t;
}

// Comps: name (40), tonnage and structure (6 each), the three costs (8 each),
// family, level and custom group (6 each), then the abilities' names joined by ", ".
ExportTable componentsTable(const game::Rules& r) {
    const std::vector<Column> cols{{"Name", 40, false}, {"Tons", 6},   {"Struct", 6}, {"Min", 8},   {"Org", 8},
                                   {"Rad", 8},          {"Family", 6}, {"Level", 6},  {"Group", 6}};
    ExportTable t{"Comps.txt", header(cols, " Abilities")};
    for (const ruleset::Component& c : r.data().components) {
        std::string abilities;
        for (const ruleset::Ability& a : c.abilities) abilities += (abilities.empty() ? "" : ", ") + a.type;
        const std::vector<std::string> cells{c.name,          num(c.tonnage),      num(c.structure),   num(c.cost.minerals),  num(c.cost.organics),
                                             num(c.cost.radioactives), num(c.family), num(c.romanNumeral), num(c.customGroup)};
        t.text += row(cols, cells, abilities.empty() ? std::string{} : " " + abilities);
    }
    return t;
}

// WeaponFamilies: name (40), weapon family number (15), vehicle type text (6).
ExportTable weaponFamiliesTable(const game::Rules& r) {
    const std::vector<Column> cols{{"Name", 40, false}, {"Weapon Family", 15}, {" Vehicle Type", 6, false}};
    ExportTable t{"WeaponFamilies.txt", header(cols)};
    for (const ruleset::Component& c : r.data().components)
        if (c.isWeapon()) t.text += row(cols, std::vector<std::string>{c.name, num(c.weapon.family), " " + c.vehicleText});
    return t;
}

// CompFamilies: name (40), family number (15), vehicle type text (6).
ExportTable componentFamiliesTable(const game::Rules& r) {
    const std::vector<Column> cols{{"Name", 40, false}, {"Family", 15}, {" Vehicle Type", 6, false}};
    ExportTable t{"CompFamilies.txt", header(cols)};
    for (const ruleset::Component& c : r.data().components) t.text += row(cols, std::vector<std::string>{c.name, num(c.family), " " + c.vehicleText});
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
