#pragma once

// Internal to the manifest's reader (manifest.cpp, manifest_rules.cpp).

#include "mods/manifest.hpp"

#include <toml++/toml.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {

// "mods/x/mod.toml:12": where a node is written.
std::string manifestAt(std::string_view source, const toml::node& n);
// Python names separated by dots ("fleet.admiral").
bool dottedPythonName(std::string_view name);
// Reads the [rules] table into `out`, each problem with its line in `errors`.
void parseRulesTable(const toml::node& node, std::string_view source, RulesDecl& out, std::vector<std::string>& errors);

} // namespace opense4::mods
