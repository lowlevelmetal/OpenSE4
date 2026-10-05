// Data patches on the parsed records, before the typed loaders
// (docs/sdk/packages-and-data.md "Data patches").

#include "game/ai_data.hpp"
#include "game/rules.hpp"
#include "mod_fixture.hpp"
#include "mods/generator.hpp"
#include "script/value.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

LoadedDataSet loadWith(const GameFolder& g, std::vector<Package> packages, const mods::LoadOptions& options = {}) {
    return loadDataSet(g.root, g.data(), modSet(std::move(packages)), options);
}

// One mod with one patch file.
struct PatchMod : ModDir {
    PatchMod(std::string_view tag, std::string_view patch, std::string_view id = "test.patch") : ModDir(tag, id) { file("data/patch.toml", patch); }
};

const ruleset::Component* component(const LoadedDataSet& d, std::string_view name) { return d.ruleset->findComponent(name); }

std::vector<std::string> abilityTypes(const std::vector<ruleset::Ability>& list) {
    std::vector<std::string> out;
    for (const auto& a : list) out.push_back(a.type);
    return out;
}

const datafile::DataFile* aiFile(const LoadedDataSet& d, std::string_view path) {
    for (const auto& [p, f] : d.data->aiFiles())
        if (p == path) return f;
    return nullptr;
}

} // namespace

TEST_CASE("sdk patches: no mods load the data set as the plain loader does") {
    GameFolder g("plain");
    const LoadedDataSet d = loadWith(g, {});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    const ruleset::LoadResult plain = ruleset::loadRuleset(g.data());
    REQUIRE(plain.ruleset);
    CHECK(d.ruleset->components.size() == plain.ruleset->components.size());
    CHECK(d.ruleset->techAreas.size() == plain.ruleset->techAreas.size());
    CHECK(d.ruleset->names.systemNames == plain.ruleset->names.systemNames);
    CHECK(d.diagnostics.unreadFields == plain.diagnostics.unreadFields);
    CHECK(d.diagnostics.warnings == plain.diagnostics.warnings);
    CHECK(d.ruleset->mods.empty());
    CHECK(d.ruleset->files);
}

TEST_CASE("sdk patches: add, change and remove records of several tables") {
    GameFolder g("ops");
    PatchMod m("ops", R"(
[[components.add]]
name = "Twin Needle Beam"
copy_from = "Needle Beam"
before = "Needle Beam"
set = { "Tonnage Space Taken" = 35, "Restrictions" = "One Per Vehicle" }

[[components.change]]
name = "Spark Drive"
set = { "Supply Amount Used" = 3, "Description" = "Moves ships, now thirstier." }

[[vehicle_sizes.add]]
name = "Test Lighter"
copy_from = "Test Cutter"
set = { "Tonnage" = 120, "Primary Bitmap Name" = "Lighter" }

[[facilities.change]]
name = "Ore Digger"
set = { "Cost Minerals" = 777 }

[[cultures.add]]
name = "Restless"
copy_from = "Steady"
set = { "Research" = 15 }

[[sector_types.remove]]
index = 1

[[settings.change]]
set = { "Number Of Space Combat Turns" = 12 }

[[design_types.add]]
name = "Picket Ship"
)");
    const LoadedDataSet d = loadWith(g, {m.open()});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    const ruleset::Ruleset& rs = *d.ruleset;
    // Placed before its original, with the copy's fields and the patch's.
    REQUIRE(rs.components.size() == 3);
    CHECK(rs.components[1].name == "Twin Needle Beam");
    CHECK(rs.components[2].name == "Needle Beam");
    CHECK(rs.components[1].tonnage == 35);
    CHECK(rs.components[1].maxPerVehicle == 1);
    CHECK(rs.components[1].weapon.damageAtRange == rs.components[2].weapon.damageAtRange);
    CHECK(component(d, "Spark Drive")->supplyUsed == 3);
    CHECK(component(d, "Spark Drive")->description == "Moves ships, now thirstier.");
    REQUIRE(rs.findVehicleSize("Test Lighter"));
    CHECK(rs.findVehicleSize("Test Lighter")->tonnage == 120);
    CHECK(rs.findVehicleSize("Test Lighter")->primaryBitmap == "Lighter");
    CHECK(rs.findVehicleSize("Test Lighter")->requirements.size() == 1);
    CHECK(rs.findFacility("Ore Digger")->cost.minerals == 777);
    REQUIRE(rs.cultures.size() == 2);
    CHECK(rs.cultures[1].research == 15);
    const ruleset::LoadResult plain = ruleset::loadRuleset(g.data());
    CHECK(rs.sectorObjectTypes.size() + 1 == plain.ruleset->sectorObjectTypes.size());
    CHECK(rs.settings.integer("Number Of Space Combat Turns") == 12);
    CHECK(rs.names.designTypes.back() == "Picket Ship");
    CHECK(rs.mods.size() == 1);
    CHECK(rs.mods[0].id == "test.patch");
}

TEST_CASE("sdk patches: list entries added and removed, the list renumbered") {
    GameFolder g("lists");
    PatchMod m("lists", R"(
[[components.change]]
name = "Spark Drive"
remove = { requirements = ["Drive Systems"], abilities = [1] }
add = { abilities = [{ "Ability Type" = "Supply Storage", "Ability Val 1" = 50 }, { "Ability Type" = "Standard Ship Movement", "Ability Val 1" = 2 }] }

[[tech_areas.change]]
name = "Applied Physics"
add = { requirements = [{ "Tech Area Req" = "Drive Systems", "Tech Level Req" = 1 }] }

[[racial_traits.change]]
name = "Night Eyes"
remove = { restricted_traits = ["Day Eyes"] }
add = { values = [{ "Value" = 9 }] }

[[quadrant_types.change]]
name = "Test Quadrant"
remove = { system_types = [{ "Type Name" = "Test Single Star" }] }

[system_names]
add = ["Kestrel", "Lantern"]
)");
    const LoadedDataSet d = loadWith(g, {m.open()});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    const ruleset::Component* spark = component(d, "Spark Drive");
    CHECK(spark->requirements.empty());
    CHECK(abilityTypes(spark->abilities) == std::vector<std::string>{"Supply Storage", "Standard Ship Movement"});
    CHECK(spark->abilities[0].number1() == 50);
    CHECK(spark->abilities[1].number1() == 2);
    const auto physics = d.ruleset->findTechArea("Applied Physics");
    REQUIRE(physics);
    REQUIRE(d.ruleset->techArea(*physics).requirements.size() == 1);
    const ruleset::RacialTrait& night = d.ruleset->racialTraits[0];
    CHECK(night.restrictedTraits.empty());
    CHECK(night.values == std::vector<std::string>{"5", "9"});
    REQUIRE(d.ruleset->quadrantTypes[0].systemTypeChances.size() == 1);
    CHECK(d.ruleset->quadrantTypes[0].systemTypeChances[0].second == 400);
    const auto& names = d.ruleset->names.systemNames;
    REQUIRE(names.size() >= 2);
    CHECK(names[names.size() - 2] == "Kestrel");
    CHECK(names.back() == "Lantern");

    // The renumbered list in the file: the count, then entries from 1.
    const datafile::DataFile* components = nullptr;
    for (const datafile::DataFile* f : d.data->dataFiles())
        if (f->name == "Components.txt") components = f;
    REQUIRE(components);
    const datafile::Record& r = components->records[0];
    CHECK(r.find("Number of Abilities")->value == "2");
    CHECK(r.find("Ability 1 Type")->value == "Supply Storage");
    CHECK(r.find("Ability 2 Type")->value == "Standard Ship Movement");
    CHECK_FALSE(r.find("Ability 3 Type"));
    CHECK(r.find("Number of Tech Req")->value == "0");
    CHECK_FALSE(r.find("Tech Area Req 1"));
}

TEST_CASE("sdk patches: the name lists and a mod's replacement data file") {
    GameFolder g("classic");
    const LoadedDataSet d = loadWith(g, {openFixtureMod("classic-names")});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    CHECK(d.ruleset->names.systemNames == std::vector<std::string>{"Aster", "Brightwater", "Cinder", "Duskfall"});
    PatchMod m("namelist", "[system_names]\nremove = [\"Cinder\", \"Nowhere\"]\nadd = [\"Embers\"]\n");
    const LoadedDataSet both = loadWith(g, {openFixtureMod("classic-names"), m.open()});
    CHECK(both.ruleset->names.systemNames == std::vector<std::string>{"Aster", "Brightwater", "Duskfall", "Embers"});
    CHECK(hasError(both.diagnostics, "mod test.patch, data/patch.toml:2: SystemNames.txt has no 'Nowhere' to remove"));
    // A later mod's whole file replaces an earlier mod's patches.
    ModDir later("replace", "test.replace");
    later.file("data/SystemNames.txt", "Final\n");
    const LoadedDataSet replaced = loadWith(g, {m.open(), later.open()});
    CHECK(replaced.ruleset->names.systemNames == std::vector<std::string>{"Final"});
}

TEST_CASE("sdk patches: typos and mistakes are errors naming the mod, file, line and record") {
    GameFolder g("typos");
    PatchMod m("typos", R"([[components.change]]
name = "Spark Drive"
set = { "Tonage Space Taken" = 5 }

[[components.chnage]]
name = "Spark Drive"

[[componnets.add]]
name = "X"

[[components.change]]
name = "Nothing Of That Name"
set = { "Tonnage Space Taken" = 5 }

[[components.add]]
name = "Spark Drive"
set = { "Tonnage Space Taken" = 5 }

[[components.change]]
nmae = "Spark Drive"

[[settings.change]]
set = { "Maximum Number Of Sytems" = 10 }

[[components.change]]
name = "Spark Drive"
set = { "Ability 3 Type" = "Supply Storage" }
add = { weapons = [{ "Type" = "x" }] }

[[components.change]]
name = "Needle Beam"
add = { abilities = [{ "Ability Typ" = "Supply Storage" }] }

[[components.change]]
name = "Needle Beam"
set = { "Tonnage Space Taken" = 1.5 }
)");
    const LoadedDataSet d = loadWith(g, {m.open()});
    const std::string e = allErrors(d.diagnostics);
    CHECK_MESSAGE(hasError(d.diagnostics, "mod test.patch, data/patch.toml:3: Components.txt [Spark Drive]: components records have no field 'Tonage Space Taken'"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:5: unknown operation 'components.chnage' (add, change, remove)"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:8: unknown table 'componnets'"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:11: Components.txt has no record 'Nothing Of That Name' to change"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:15: Components.txt already has 'Spark Drive'"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:20: unknown key 'nmae' in components.change"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:23: Settings.txt has no field 'Maximum Number Of Sytems'"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:27: Components.txt [Spark Drive]: 'Ability 3 Type' is past the end of its list (Number of Abilities)"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "components has no list 'weapons' (its lists: requirements, abilities)"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "'Ability Typ' is not a field of an entry of abilities"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "data/patch.toml:36: 'Tonnage Space Taken' should be text, a whole number or true/false, not a number with a fraction"), e);
    // The loaders' own checks name the patch too.
    PatchMod bad("loaderror", "[[components.change]]\nname = \"Spark Drive\"\nset = { \"Restrictions\" = \"Seven Hundred Per Vehicle\" }\n");
    const LoadedDataSet l = loadWith(g, {bad.open()});
    CHECK_MESSAGE(hasError(l.diagnostics, "[Spark Drive] (changed by mod test.patch, data/patch.toml:3): unknown restriction 'Seven Hundred Per Vehicle'"),
                  allErrors(l.diagnostics));
    PatchMod notNumber("number", "[[components.change]]\nname = \"Spark Drive\"\nset = { \"Tonnage Space Taken\" = \"heavy\" }\n");
    const LoadedDataSet n = loadWith(g, {notNumber.open()});
    CHECK_MESSAGE(hasError(n.diagnostics, "Components.txt [Spark Drive] (mod test.patch, data/patch.toml:3): 'Tonnage Space Taken' should be a whole number"),
                  allErrors(n.diagnostics));
}

TEST_CASE("sdk patches: removing a record checks what refers to it") {
    GameFolder g("refs");
    g.write("Ai/Default_AI_Research.txt",
            "AI State := Exploration\nTech Area Name := Drive Systems\nTech Area Level := 1\nTech Area Min Percent := 10\n\n"
            "AI State := Exploration\nTech Area Name := Applied Physics\nTech Area Level := 1\nTech Area Min Percent := 10");
    // Without cascade: each reference is an error naming where it is.
    PatchMod m("refs", "[[tech_areas.remove]]\nname = \"Drive Systems\"\n");
    const LoadedDataSet d = loadWith(g, {m.open()});
    const std::string e = allErrors(d.diagnostics);
    CHECK_MESSAGE(hasError(d.diagnostics,
                           "mod test.patch, data/patch.toml:1: removing tech_areas 'Drive Systems' leaves a reference to it in Components.txt:"),
                  e);
    CHECK_MESSAGE(hasError(d.diagnostics, "leaves a reference to it in Default_AI_Research.txt:"), e);
    CHECK_MESSAGE(hasError(d.diagnostics, "('Tech Area Req 1')"), e);
    // With cascade: what needs it goes too, and what needs that.
    PatchMod c("cascade", "[[tech_areas.remove]]\nname = \"Applied Physics\"\ncascade = true\n");
    const LoadedDataSet cd = loadWith(g, {c.open()});
    REQUIRE(cd.ruleset);
    CHECK_MESSAGE(cd.diagnostics.errors.empty(), allErrors(cd.diagnostics));
    CHECK(cd.ruleset->techAreas.empty());                 // Drive Systems needed Applied Physics
    CHECK(cd.ruleset->vehicleSizes.empty());              // the cutter needed Applied Physics
    REQUIRE(cd.ruleset->components.size() == 1);          // the drive needed Drive Systems
    CHECK(cd.ruleset->components[0].name == "Needle Beam");
    const datafile::DataFile* research = aiFile(cd, "Ai/Default_AI_Research.txt");
    REQUIRE(research);
    CHECK(research->records.empty());
    // A list entry goes, not its record: a quadrant's system type.
    PatchMod s("entry", "[[system_types.remove]]\nname = \"Test Busy System\"\ncascade = true\n");
    const LoadedDataSet sd = loadWith(g, {s.open()});
    REQUIRE(sd.ruleset);
    CHECK_MESSAGE(sd.diagnostics.errors.empty(), allErrors(sd.diagnostics));
    CHECK(sd.ruleset->quadrantTypes.size() == 2);
    CHECK(sd.ruleset->quadrantTypes[0].systemTypeChances.size() == 1);
    // A racial trait named by another's restrictions.
    PatchMod t("trait", "[[racial_traits.remove]]\nname = \"Day Eyes\"\n");
    CHECK(hasError(loadWith(g, {t.open()}).diagnostics, "RacialTraits.txt:"));
    // Renaming is removing the old name.
    PatchMod rename("rename", "[[tech_areas.change]]\nname = \"Applied Physics\"\nset = { \"Name\" = \"Physics\" }\n");
    CHECK(hasError(loadWith(g, {rename.open()}).diagnostics, "removing tech_areas 'Applied Physics' leaves a reference"));
    // A later mod that adds the name again leaves nothing dangling.
    PatchMod back("back", "[[tech_areas.add]]\nname = \"Drive Systems\"\ncopy_from = \"Applied Physics\"\n", "test.back");
    const LoadedDataSet again = loadWith(g, {m.open(), back.open()});
    CHECK_MESSAGE(again.diagnostics.errors.empty(), allErrors(again.diagnostics));
}

TEST_CASE("sdk patches: the computer players' tables") {
    GameFolder g("ai");
    g.write("Ai/Default_AI_Anger.txt", "Per Attack Location := 7\nRegular Decrease := -9");
    g.write("Ai/Default_AI_Research.txt", "AI State := Exploration\nTech Area Name := Drive Systems\nTech Area Level := 1\nTech Area Min Percent := 10");
    g.write("Ai/Aggressive/Aggressive_AI_Anger.txt", "Regular Decrease := -1");
    g.write("Pictures/Races/Testian/Testian_AI_General.txt", "Name := Testian\nRace Opt 1 Num Characteristics := 0");
    g.write("Pictures/Races/Testian/Testian_AI_Anger.txt", "Regular Decrease := -4");
    const LoadedDataSet d = loadWith(g, {openFixtureMod("ai-tweaks")});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    const game::Rules rules(*d.ruleset);
    // The default table is patched; the style's and race's are not (files = "default").
    CHECK(game::ai::profileFor(rules, "Nobody", "").anger.regularDecrease == -6);
    CHECK(game::ai::profileFor(rules, "Testian", "").anger.regularDecrease == -4);
    CHECK(game::ai::profileFor(rules, "", "Aggressive").anger.regularDecrease == -1);
    const auto& research = game::ai::profileFor(rules, "Nobody", "").research;
    REQUIRE(research.size() == 2);
    CHECK(research[1].area == "Applied Physics");
    CHECK(research[1].minPercent == 40);
    // The mod's own minister style folder is there.
    CHECK(game::ai::ministerStyles(rules) == std::vector<std::string>{"Aggressive", "Testers"});
    CHECK(game::ai::profileFor(rules, "", "Testers").anger.perAttackLocation == 9);

    // Every file of a table, and one race's.
    PatchMod all("all", R"([[ai.anger.change]]
set = { "Regular Decrease" = -2 }

[[ai.anger.change]]
files = "race:Testian"
set = { "Per Attack Location" = 3 }

[[ai.research.change]]
match = { "Tech Area Name" = "Drive Systems" }
set = { "Tech Area Level" = 2 }

[[ai.general.change]]
files = ["race:Testian"]
set = { "Name" = "Testians Renamed" }
)");
    const LoadedDataSet a = loadWith(g, {all.open()});
    REQUIRE(a.ruleset);
    CHECK_MESSAGE(a.diagnostics.errors.empty(), allErrors(a.diagnostics));
    const game::Rules ar(*a.ruleset);
    CHECK(game::ai::profileFor(ar, "Nobody", "").anger.regularDecrease == -2);
    CHECK(game::ai::profileFor(ar, "", "Aggressive").anger.regularDecrease == -2);
    CHECK(game::ai::profileFor(ar, "Testian", "").anger.perAttackLocation == 3);
    CHECK(game::ai::profileFor(ar, "Nobody", "").research[0].level == 2);
    REQUIRE(ar.racePresets().size() == 1);
    CHECK(ar.racePresets()[0].name == "Testians Renamed");

    // Typos in the AI tables, and files that are not there.
    PatchMod typos("aitypos", R"([[ai.anger.change]]
files = "default"
set = { "Regular Decrese" = -2 }

[[ai.research.remove]]
match = { "Tech Area Name" = "Nothing" }

[[ai.fleets.change]]
set = { "Percentage of Fleets to use for defense" = 10 }

[[ai.anger.change]]
files = "style:Nobody"
set = { "Regular Decrease" = 1 }

[[ai.anger.change]]
files = "elsewhere"
set = { "Regular Decrease" = 1 }
)");
    const LoadedDataSet t = loadWith(g, {typos.open()});
    const std::string e = allErrors(t.diagnostics);
    CHECK_MESSAGE(hasError(t.diagnostics, "mod test.patch, data/patch.toml:3: Ai/Default_AI_Anger.txt [7]: ai.anger records have no field 'Regular Decrese'"), e);
    CHECK_MESSAGE(hasError(t.diagnostics, "data/patch.toml:5: no ai.research file has a record match {Tech Area Name = Nothing} to remove"), e);
    CHECK_MESSAGE(hasError(t.diagnostics, "data/patch.toml:8: no ai.fleets file (<prefix>_AI_Fleets.txt) to patch"), e);
    CHECK_MESSAGE(hasError(t.diagnostics, "data/patch.toml:11: no ai.anger file of those 'files' names"), e);
    CHECK_MESSAGE(hasError(t.diagnostics, "files = 'elsewhere'"), e);
}

TEST_CASE("sdk patches: declared abilities") {
    GameFolder g("abilities");
    // Undeclared, a new name is an error, as before.
    PatchMod undeclared("undeclared", R"([[components.change]]
name = "Spark Drive"
add = { abilities = [{ "Ability Type" = "Hyperspace Anchor", "Ability Val 1" = 4 }] }
)");
    CHECK(hasError(loadWith(g, {undeclared.open()}).diagnostics, "unknown ability type 'Hyperspace Anchor'"));

    // Declared by one mod, used by another.
    const LoadedDataSet d = loadWith(g, {openFixtureMod("common-lib"), openFixtureMod("needs-lib")});
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    REQUIRE(d.ruleset->declaredAbilities.size() == 1);
    CHECK(d.ruleset->declaredAbilities[0].name == "Hyperspace Anchor");
    CHECK(d.ruleset->declaredAbilities[0].combine == ruleset::Combine::Max);
    CHECK(d.ruleset->declaredAbilities[0].mod == "test.common-lib");
    const game::Rules rules(*d.ruleset);
    const uint32_t spark = 0;
    REQUIRE(rules.component(spark).name == "Spark Drive");
    CHECK(rules.declaredAbilityOfComponent(spark, "Hyperspace Anchor") == 4);
    CHECK(rules.declaredAbilityOfComponent(spark, "hyperspace  anchor") == 4);  // any case, as data names are
    CHECK(rules.declaredAbilityOfComponent(1, "Hyperspace Anchor") == 0);
    CHECK_FALSE(rules.declaredAbilityOfComponent(spark, "Not Declared"));
    CHECK(rules.declaredAbilityOfHull(0, "Hyperspace Anchor") == 0);
    // On a design: combined over the hull and every component, as declared (largest).
    game::Design design;
    design.hull = 0;
    design.entries = {{spark, -1}, {spark, -1}, {1, -1}};
    CHECK(rules.declaredAbilityOfDesign(design, "Hyperspace Anchor") == 4);
    // The engine's own abilities read as before.
    const auto movement = game::parseAbilityKind("Standard Ship Movement");
    REQUIRE(movement);
    CHECK(game::abilitySum(rules.componentAbilities(spark), *movement) == 1);

    // Sum and min, on a colony's facilities.
    PatchMod sums("sums", R"([[abilities.declare]]
name = "Field Strength"
combine = "sum"

[[abilities.declare]]
name = "Weakest Link"
combine = "min"

[[facilities.change]]
name = "Ore Digger"
add = { abilities = [{ "Ability Type" = "Field Strength", "Ability Val 1" = 5 }, { "Ability Type" = "Weakest Link", "Ability Val 1" = 7 }] }
)");
    const LoadedDataSet s = loadWith(g, {sums.open()});
    REQUIRE(s.ruleset);
    CHECK_MESSAGE(s.diagnostics.errors.empty(), allErrors(s.diagnostics));
    const game::Rules sr(*s.ruleset);
    game::Colony colony;
    colony.facilities = {0, 0, 0};
    CHECK(sr.declaredAbilityOfColony(colony, "Field Strength") == 15);
    CHECK(sr.declaredAbilityOfColony(colony, "Weakest Link") == 7);
    CHECK(sr.declaredAbilityOfFacility(0, "Field Strength") == 5);
    colony.facilities.clear();
    CHECK(sr.declaredAbilityOfColony(colony, "Weakest Link") == 0);

    // The engine's names need no declaring; two declarations must agree.
    PatchMod engine("engine", "[[abilities.declare]]\nname = \"Supply Storage\"\n");
    CHECK(hasError(loadWith(g, {engine.open()}).diagnostics, "'Supply Storage' is one of the game's own abilities"));
    PatchMod other("other", "[[abilities.declare]]\nname = \"Hyperspace Anchor\"\ncombine = \"sum\"\n", "test.other");
    CHECK(hasError(loadWith(g, {openFixtureMod("common-lib"), other.open()}).diagnostics,
                   "mod test.common-lib declared 'Hyperspace Anchor' with combine = \"max\"; this declaration says \"sum\""));
    PatchMod same("same", "[[abilities.declare]]\nname = \"Hyperspace Anchor\"\ncombine = \"max\"\n", "test.same");
    CHECK(loadWith(g, {openFixtureMod("common-lib"), same.open()}).diagnostics.errors.empty());
    PatchMod badCombine("combine", "[[abilities.declare]]\nname = \"X\"\ncombine = \"average\"\n");
    CHECK(hasError(loadWith(g, {badCombine.open()}).diagnostics, "combine = 'average'"));
}

TEST_CASE("sdk patches: data generators run through the script runtime's interface") {
    GameFolder g("generators");
    ModDir m("gen", "test.gen");
    m.file("data/weapons.py", "def generate(data):\n    return {}\n");
    // Without the runtime: an error that says so.
    const LoadedDataSet none = loadWith(g, {m.open()});
    CHECK(hasError(none.diagnostics, "data/weapons.py: data generators need the script runtime"));

    // A runner that returns a patch: applied like a .toml patch.
    struct Fake final : GeneratorRunner {
        std::vector<GeneratorRequest> seen;
        std::expected<script::Value, std::string> run(const GeneratorRequest& request) override {
            seen.push_back(request);
            script::ValueList adds;
            for (int level = 2; level <= 3; ++level) {
                script::ValueMap set{{"Tonnage Space Taken", script::Value(10 * level)}};
                adds.push_back(script::Value(script::ValueMap{{"name", script::Value(std::format("Needle Beam {}", level))},
                                                              {"copy_from", script::Value("Needle Beam")},
                                                              {"set", script::Value(std::move(set))}}));
            }
            return script::Value(script::ValueMap{{"components", script::Value(script::ValueMap{{"add", script::Value(std::move(adds))}})}});
        }
    };
    auto fake = std::make_shared<Fake>();
    mods::LoadOptions options;
    options.generators = fake;
    const LoadedDataSet d = loadWith(g, {m.open()}, options);
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    REQUIRE(fake->seen.size() == 1);
    CHECK(fake->seen[0].mod == "test.gen");
    CHECK(fake->seen[0].file == "data/weapons.py");
    CHECK(fake->seen[0].source.find("def generate") != std::string::npos);
    REQUIRE(d.ruleset->components.size() == 4);
    CHECK(d.ruleset->components[3].name == "Needle Beam 3");
    CHECK(d.ruleset->components[3].tonnage == 30);
}
