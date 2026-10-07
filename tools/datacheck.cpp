// opense4-datacheck: loads a classic-format data set and reports what was found,
// every problem, and any fields the loader does not read yet.
//
//   opense4-datacheck                 auto-detect an installed copy of the classic game
//   opense4-datacheck path/to/Data    check a specific data set (e.g. a classic mod's)
//   opense4-datacheck -v              also list every ability and damage type in use

#include "core/environment.hpp"
#include "ruleset/ruleset.hpp"

#include <cstdio>
#include <map>
#include <set>

using namespace opense4;

namespace {

constexpr const char* kUsage = R"(opense4-datacheck: load a data set of the classic game and report every problem.

Usage:
  opense4-datacheck [DIR] [-v]

DIR is the game folder or its Data folder: an installed game, or a classic mod's
complete data set (default: the installed game, found as the game finds it). It
prints what the data set holds, then every error and warning with its file, line
and record, and the fields OpenSE4 does not read. Mods made with the SDK are
checked over the installed game with opense4-sdk check (docs/sdk/README.md).

  -v, --verbose   Also list every ability type and weapon damage type in use
  -h, --help      Show this help

Exit status: 0 when the data set has no errors, 1 when it has some, 2 when there is
no data set at DIR (or none installed) or for usage errors.
)";

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path dir;
    bool verbose = false;
    const std::vector<std::string> args = core::utf8Arguments(argc, argv);  // UTF-8 on Windows too
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "-h" || arg == "--help") {
            std::printf("%s", kUsage);
            return 0;
        }
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg.starts_with("-")) {
            std::fprintf(stderr, "opense4-datacheck: unknown option %s\n\n%s", args[i].c_str(), kUsage);
            return 2;
        } else if (!dir.empty()) {
            std::fprintf(stderr, "opense4-datacheck: one data set at a time (%s and %s)\n", dir.string().c_str(), args[i].c_str());
            return 2;
        } else {
            dir = arg;
        }
    }
    const auto found = ruleset::findInstalledDataDir(dir);
    if (!found) {
        std::fprintf(stderr, "No data set found%s%s.\n", dir.empty() ? "" : " at ", dir.string().c_str());
        return 2;
    }
    std::printf("Data set: %s\n\n", found->string().c_str());

    const ruleset::LoadResult result = ruleset::loadRuleset(*found);
    const auto& d = result.diagnostics;
    if (result.ruleset) {
        const ruleset::Ruleset& rs = *result.ruleset;
        std::printf("%-22s %5zu\n", "Tech areas", rs.techAreas.size());
        std::printf("%-22s %5zu\n", "Vehicle sizes", rs.vehicleSizes.size());
        std::printf("%-22s %5zu\n", "Components", rs.components.size());
        std::printf("%-22s %5zu\n", "Facilities", rs.facilities.size());
        std::printf("%-22s %5zu\n", "Planet sizes", rs.planetSizes.size());
        std::printf("%-22s %5zu\n", "Racial traits", rs.racialTraits.size());
        std::printf("%-22s %5zu\n", "Cultures", rs.cultures.size());
        std::printf("%-22s %5zu\n", "Sector object types", rs.sectorObjectTypes.size());
        std::printf("%-22s %5zu\n", "System types", rs.systemTypes.size());
        std::printf("%-22s %5zu\n", "Quadrant types", rs.quadrantTypes.size());
        std::printf("%-22s %5zu\n", "Stellar ability types", rs.stellarAbilityTypes.size());
        std::printf("%-22s %5zu\n", "Weapon mounts", rs.weaponMounts.size());
        std::printf("%-22s %5zu\n", "Formations", rs.formations.size());
        std::printf("%-22s %5zu\n", "Happiness models", rs.happinessModels.size());
        std::printf("%-22s %5zu\n", "Intel projects", rs.intelProjects.size());
        std::printf("%-22s %5zu\n", "Event types", rs.eventTypes.size());
        std::printf("%-22s %5zu\n", "Combat strategies", rs.combatStrategies.size());
        std::printf("%-22s %5zu\n", "Settings", rs.settings.size());
        std::printf("%-22s %5zu / %zu / %zu\n", "Names (empire/sys/emp)", rs.names.empireNames.size(), rs.names.systemNames.size(),
                    rs.names.emperorNames.size());

        std::map<std::string, int> abilityUse;
        auto count = [&](const std::vector<ruleset::Ability>& list) {
            for (const auto& a : list) ++abilityUse[a.type];
        };
        for (const auto& c : rs.components) count(c.abilities);
        for (const auto& f : rs.facilities) count(f.abilities);
        for (const auto& v : rs.vehicleSizes) count(v.abilities);
        for (const auto& s : rs.systemTypes) count(s.abilities);
        for (const auto& s : rs.stellarAbilityTypes)
            for (const auto& [chance, a] : s.possibleAbilities) ++abilityUse[a.type];
        std::set<std::string> damageTypes;
        for (const auto& c : rs.components)
            if (c.isWeapon()) damageTypes.insert(c.weapon.damageType);
        std::printf("\n%zu distinct ability types in use, %zu weapon damage types.\n", abilityUse.size(), damageTypes.size());
        if (verbose) {
            for (const auto& [type, n] : abilityUse) std::printf("  ability  %4d  %s\n", n, type.c_str());
            for (const auto& t : damageTypes) std::printf("  damage   %s\n", t.c_str());
        }
    }

    std::printf("\n%zu errors, %zu warnings, %zu distinct unread fields\n", d.errors.size(), d.warnings.size(), d.unreadFields.size());
    for (const auto& e : d.errors) std::printf("  error: %s\n", e.c_str());
    for (const auto& w : d.warnings) std::printf("  warning: %s\n", w.c_str());
    for (const auto& [field, n] : d.unreadFields) std::printf("  unread: %s (x%d)\n", field.c_str(), n);
    return d.errors.empty() ? 0 : 1;
}
