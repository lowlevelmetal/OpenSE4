#pragma once

// The Weapons Report's Export button (docs/spec/06 §1.9 "Other Settings.txt
// keys", §7 Q83, confirmed: binary): with Settings.txt `Allow Export of
// Weapon And Component Data` TRUE it writes four plain-text files into the
// save folder, in this order: Weapons.txt, Comps.txt, WeaponFamilies.txt and
// CompFamilies.txt. They list every component of the data set in data order,
// researched or not, with base values (no empire, no mount): the two weapon
// files every weapon, the two component files every component. Each is in
// fixed-width columns padded with spaces, under a header line and a line of
// dashes. Headless, tested in tests/test_settings_keys.cpp.

#include "game/rules.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace opense4::client::classic {

struct ExportTable {
    std::string fileName;   // "Weapons.txt", ...
    std::string text;       // header line, dashes, then one line per row, each ending in '\n'
};

// Settings.txt `Allow Export of Weapon And Component Data` (FALSE when missing).
bool exportAllowed(const ruleset::Settings& data);

// The four files, in the order weapons, components, weapon families,
// component families.
std::vector<ExportTable> weaponAndComponentTables(const game::Rules& r);

// Writes each table to `dir` (created when missing), prefixed with `prefix`
// (empty for the original's names); the paths written, in table order.
std::expected<std::vector<std::filesystem::path>, std::string> writeExportTables(const std::filesystem::path& dir, std::string_view prefix,
                                                                                const std::vector<ExportTable>& tables);

} // namespace opense4::client::classic
