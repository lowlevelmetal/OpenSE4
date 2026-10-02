#pragma once

// The Weapons Report's Export button (docs/spec/06 §1.9 "Other Settings.txt
// keys", confirmed: binary): with Settings.txt `Allow Export of Weapon And
// Component Data` TRUE it writes four plain-text tables to the saves folder:
// weapons, components, weapon families and component families. The
// tables' layout is OpenSE4's own: tab-separated columns under one header
// line. Every component of the data set is listed, known or not (inferred,
// spec 06 §7 Q83: the spec does not say whether only the empire's
// researched parts go in).
// Headless, tested in tests/test_settings_keys.cpp.

#include "game/rules.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace opense4::client::classic {

struct ExportTable {
    std::string fileName;   // e.g. "weapons.txt"
    std::string text;       // header line, then one line per row, each ending in '\n'
};

// Settings.txt `Allow Export of Weapon And Component Data` (FALSE when missing).
bool exportAllowed(const ruleset::Settings& data);

// The four tables, in the order weapons, components, weapon families,
// component families.
std::vector<ExportTable> weaponAndComponentTables(const game::Rules& r);

// Writes each table to `dir` (created when missing), prefixed with
// `prefix` ("<prefix>weapons.txt", ...); the paths written, in table order.
std::expected<std::vector<std::filesystem::path>, std::string> writeExportTables(const std::filesystem::path& dir, std::string_view prefix,
                                                                                const std::vector<ExportTable>& tables);

} // namespace opense4::client::classic
