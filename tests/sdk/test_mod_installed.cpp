// Mods on the player's installed data set (opt-in: OPENSE4_CLASSIC_DATA).
// The patches name the install's records as the test finds them; nothing of
// the install is written down here.

#include "game/ai_data.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "mod_fixture.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> installedData() {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return std::nullopt;
    return ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
}

} // namespace

TEST_CASE("installed data set: mods load over it (opt-in)") {
    const auto dir = installedData();
    if (!dir) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to test against an installed data set");
        return;
    }
    const fs::path root = dir->parent_path();
    // Without mods: as the plain loader reads it, and the same identity.
    const LoadedDataSet base = loadDataSet(root, *dir, ModSet{});
    REQUIRE(base.ruleset);
    CHECK_MESSAGE(base.diagnostics.errors.empty(), allErrors(base.diagnostics));
    auto plain = ruleset::loadRuleset(*dir);
    REQUIRE(plain.ruleset);
    CHECK(base.ruleset->components.size() == plain.ruleset->components.size());
    CHECK(base.ruleset->techAreas.size() == plain.ruleset->techAreas.size());
    CHECK(base.diagnostics.unreadFields == plain.diagnostics.unreadFields);
    const game::Rules viaMods(*base.ruleset);
    const game::Rules viaInstall(std::move(*plain.ruleset), root);
    CHECK(game::dataSetIdentity(viaMods) == game::dataSetIdentity(viaInstall));
    CHECK(viaMods.racePresets().size() == viaInstall.racePresets().size());
    CHECK(game::ai::ministerStyles(viaMods) == game::ai::ministerStyles(viaInstall));
    const ruleset::Ruleset& rs = *base.ruleset;
    REQUIRE_FALSE(rs.components.empty());
    REQUIRE_FALSE(rs.techAreas.empty());

    // The fixture hull and a patch of the install's own records.
    const std::string first = rs.components.front().name;
    ModDir m("installed", "test.installed");
    m.file("data/patch.toml", std::format(R"([[abilities.declare]]
name = "Test Resonance"
combine = "sum"

[[components.change]]
name = "{}"
set = {{ "Supply Amount Used" = 77 }}
add = {{ abilities = [{{ "Ability Type" = "Test Resonance", "Ability Val 1" = 5 }}] }}

[[components.add]]
name = "Test Copy"
copy_from = "{}"
)",
                                          first, first));
    const LoadedDataSet d = loadDataSet(root, *dir, modSet({openFixtureMod("escort-hull"), m.open()}));
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    CHECK(d.ruleset->findVehicleSize("Escort Carrier"));
    CHECK(d.ruleset->findComponent(first)->supplyUsed == 77);
    CHECK(d.ruleset->components.size() == rs.components.size() + 1);
    const game::Rules rules(*d.ruleset);
    CHECK(rules.declaredAbilityOfComponent(0, "Test Resonance") == 5);
    CHECK_FALSE(game::sameDataSet(game::dataSetIdentity(rules), game::dataSetIdentity(viaMods)));

    // Removing a technology: every reference is reported, or cascade removes it all.
    const std::string area = rs.techAreas.front().name;
    ModDir rm("installed_rm", "test.remove");
    rm.file("data/patch.toml", std::format("[[tech_areas.remove]]\nname = \"{}\"\n", area));
    const LoadedDataSet refused = loadDataSet(root, *dir, modSet({rm.open()}));
    CHECK(hasError(refused.diagnostics, "leaves a reference to it"));
    ModDir cascade("installed_cascade", "test.cascade");
    cascade.file("data/patch.toml", std::format("[[tech_areas.remove]]\nname = \"{}\"\ncascade = true\n", area));
    const LoadedDataSet cascaded = loadDataSet(root, *dir, modSet({cascade.open()}));
    REQUIRE(cascaded.ruleset);
    CHECK_MESSAGE(cascaded.diagnostics.errors.empty(), allErrors(cascaded.diagnostics));
    CHECK(cascaded.ruleset->techAreas.size() < rs.techAreas.size());
    CHECK_FALSE(cascaded.ruleset->findTechArea(area));

    // The computer players' default tables, patched.
    if (base.data->file("Ai/Default_AI_Anger.txt")) {
        ModDir ai("installed_ai", "test.ai");
        ai.file("data/ai.toml", "[[ai.anger.change]]\nfiles = \"default\"\nset = { \"Regular Decrease\" = -77 }\n");
        const LoadedDataSet a = loadDataSet(root, *dir, modSet({ai.open()}));
        REQUIRE(a.ruleset);
        CHECK_MESSAGE(a.diagnostics.errors.empty(), allErrors(a.diagnostics));
        const game::Rules ar(*a.ruleset);
        CHECK(game::ai::profileFor(ar, "", "").anger.regularDecrease == -77);
    }

    // Written out, the patched data set reads back as it was.
    const LoadedDataSet hull = loadDataSet(root, *dir, modSet({openFixtureMod("escort-hull")}));
    REQUIRE(hull.ruleset);
    TempDir out("sdk_installed_dump");
    for (const datafile::DataFile* f : hull.data->dataFiles()) writeText(out / "Data" / f->name, datafile::write(*f, "Written by opense4 tests."));
    const auto back = ruleset::loadRuleset(out / "Data");
    REQUIRE(back.ruleset);
    CHECK_MESSAGE(back.diagnostics.errors.empty(), allErrors(back.diagnostics));
    CHECK(back.ruleset->components.size() == hull.ruleset->components.size());
    CHECK(back.ruleset->vehicleSizes.size() == hull.ruleset->vehicleSizes.size());
    CHECK(back.ruleset->findVehicleSize("Escort Carrier"));
}
