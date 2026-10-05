// Mod sets: load order, dependencies and the player's choice of mods
// (docs/sdk/packages-and-data.md "Load order").

#include "mod_fixture.hpp"
#include "ruleset/mods.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> ids(const ModSet& s) {
    std::vector<std::string> out;
    for (const Package& p : s.packages) out.push_back(p.id());
    return out;
}

std::string joined(const std::vector<std::string>& errors) {
    std::string out;
    for (const std::string& e : errors) out += e + "\n";
    return out;
}

} // namespace

TEST_CASE("sdk mod sets: requirements and hints come first, then the player's order") {
    // The player put the library last: it still loads before the mod that needs it.
    const ModSet set = modSet({openFixtureMod("needs-lib"), openFixtureMod("escort-hull"), openFixtureMod("common-lib")});
    CHECK(ids(set) == std::vector<std::string>{"test.escort-hull", "test.common-lib", "test.needs-lib"});

    ModDir a("order_a", "test.a");
    ModDir b("order_b", "test.b", "1.0.0", "\n[load]\nafter = [\"test.c\", \"test.not-enabled\"]\n");
    ModDir c("order_c", "test.c");
    CHECK(ids(modSet({a.open(), b.open(), c.open()})) == std::vector<std::string>{"test.a", "test.c", "test.b"});
    CHECK(ids(modSet({c.open(), b.open(), a.open()})) == std::vector<std::string>{"test.c", "test.b", "test.a"});
    CHECK(ids(modSet({b.open(), a.open()})) == std::vector<std::string>{"test.b", "test.a"});  // a hint to a mod not enabled is no requirement

    // The records and the identity of the set.
    const std::vector<ruleset::ModRecord> records = set.records();
    REQUIRE(records.size() == 3);
    CHECK(records[1].id == "test.common-lib");
    CHECK(records[1].version == "1.2.0");
    CHECK(set.identity() == ruleset::modSetIdentity(records));
    CHECK(set.identity().size() == 16);
    CHECK(ModSet{}.identity().empty());
}

TEST_CASE("sdk mod sets: what keeps mods from loading together") {
    // A required mod that is not enabled.
    auto missing = resolveModSet({openFixtureMod("needs-lib")});
    REQUIRE_FALSE(missing);
    CHECK(joined(missing.error()).find("mod test.needs-lib 0.3.0 needs mod test.common-lib (>=1.0, <2), which is not enabled") != std::string::npos);

    // A version outside the range.
    ModDir old("old_lib", "test.common-lib", "0.9.0");
    auto tooOld = resolveModSet({openFixtureMod("needs-lib"), old.open()});
    REQUIRE_FALSE(tooOld);
    CHECK(joined(tooOld.error()).find("needs mod test.common-lib >=1.0, <2, but the one enabled is version 0.9.0") != std::string::npos);

    // The same id twice.
    ModDir copy("copy_lib", "test.common-lib", "1.2.0");
    auto twice = resolveModSet({openFixtureMod("common-lib"), copy.open()});
    REQUIRE_FALSE(twice);
    CHECK(joined(twice.error()).find("mod test.common-lib is enabled twice") != std::string::npos);

    // A cycle.
    ModDir x("cycle_x", "test.x", "1.0.0", "\n[requires]\n\"test.y\" = \"*\"\n");
    ModDir y("cycle_y", "test.y", "1.0.0", "\n[load]\nafter = [\"test.x\"]\n");
    ModDir z("cycle_z", "test.z");
    auto cycle = resolveModSet({z.open(), x.open(), y.open()});
    REQUIRE_FALSE(cycle);
    CHECK(joined(cycle.error()).find("none can load first: test.x, test.y") != std::string::npos);
}

TEST_CASE("sdk mod sets: the player's choice, by path or by id in the mods folder") {
    TempDir dir("sdk_choice");
    const fs::path modsFolder = dir / "Mods";
    fs::create_directories(modsFolder);
    fs::copy(fixtureMod("common-lib"), modsFolder / "common-lib", fs::copy_options::recursive);
    fs::copy(fixtureMod("escort-hull"), modsFolder / "escort-hull", fs::copy_options::recursive);
    writeText(modsFolder / "broken" / "mod.toml", "[mod]\nid = \"BAD\"\n");

    const ModLibrary lib = scanModsFolder(modsFolder);
    CHECK(lib.packages.size() == 2);
    CHECK(lib.find("test.common-lib"));
    REQUIRE(lib.problems.size() == 1);
    CHECK(lib.problems[0].find("broken") != std::string::npos);

    ModChoice choice;
    choice.modsDir = modsFolder;
    choice.mods = {fixtureMod("needs-lib").string(), "test.common-lib"};
    auto set = selectMods(choice);
    REQUIRE_MESSAGE(set, (set ? std::string{} : joined(set.error())));
    CHECK(ids(*set) == std::vector<std::string>{"test.common-lib", "test.needs-lib"});

    choice.mods = {"test.nowhere"};
    auto none = selectMods(choice);
    REQUIRE_FALSE(none);
    CHECK(joined(none.error()).find("no mod 'test.nowhere' in the mods folder") != std::string::npos);
    choice.mods = {"no/such/folder"};
    CHECK_FALSE(selectMods(choice));
    // Relative paths from a base folder (a server's setup file).
    choice.mods = {"Mods/escort-hull"};
    choice.baseDir = dir.path();
    auto relative = selectMods(choice);
    REQUIRE(relative);
    CHECK(ids(*relative) == std::vector<std::string>{"test.escort-hull"});

    // The mods a game recorded, found again by id and identity.
    const std::vector<ruleset::ModRecord> recorded{openFixtureMod("common-lib").record(), openFixtureMod("escort-hull").record()};
    auto again = modsForGame(recorded, modsFolder);
    REQUIRE(again);
    CHECK(ids(*again) == std::vector<std::string>{"test.common-lib", "test.escort-hull"});
    std::vector<ruleset::ModRecord> other = recorded;
    other[0].hash = std::string(32, '0');
    auto differs = modsForGame(other, modsFolder);
    REQUIRE_FALSE(differs);
    CHECK(joined(differs.error()).find("the copy in") != std::string::npos);
    // An asset-only mod a game recorded may be missing.
    ruleset::ModRecord pictures{"test.pictures-only", "1.0.0", std::string(32, 'a'), false};
    auto withoutPictures = modsForGame(std::vector<ruleset::ModRecord>{pictures}, modsFolder);
    REQUIRE(withoutPictures);
    CHECK(withoutPictures->empty());
}

TEST_CASE("sdk mod sets: the mod manager's model") {
    TempDir dir("sdk_manager");
    fs::create_directories(dir / "Mods");
    for (std::string_view name : {"common-lib", "needs-lib", "escort-hull"})
        fs::copy(fixtureMod(name), dir / "Mods" / std::string(name), fs::copy_options::recursive);
    ModManager manager(scanModsFolder(dir / "Mods"), {"test.escort-hull", "test.unknown"});
    CHECK(manager.enabled() == std::vector<std::string>{"test.escort-hull"});  // a mod no longer there is dropped
    manager.enable("test.needs-lib");
    CHECK(manager.isEnabled("test.needs-lib"));
    auto broken = manager.resolve();
    REQUIRE_FALSE(broken);  // needs-lib needs the library
    manager.enable("test.common-lib");
    manager.move("test.common-lib", -5);
    CHECK(manager.enabled() == std::vector<std::string>{"test.common-lib", "test.escort-hull", "test.needs-lib"});
    auto set = manager.resolve();
    REQUIRE(set);
    CHECK(ids(*set) == std::vector<std::string>{"test.common-lib", "test.escort-hull", "test.needs-lib"});
    manager.disable("test.escort-hull");
    CHECK(manager.enabled() == std::vector<std::string>{"test.common-lib", "test.needs-lib"});
    CHECK(ModManager::summary(*manager.library().find("test.escort-hull")) == "assets, data; changes the game");
}

TEST_CASE("sdk mod sets: comparing a game's mods with a player's") {
    using ruleset::ModRecord;
    const ModRecord data{"test.data", "1.0.0", std::string(32, '1'), true};
    const ModRecord pictures{"test.pictures", "1.0.0", std::string(32, '2'), false};
    const ModRecord rules{"test.rules", "2.0.0", std::string(32, '3'), true};
    auto diff = [](std::vector<ModRecord> game, std::vector<ModRecord> mine) { return ruleset::compareModSets(game, mine, "the host"); };
    CHECK(diff({data, rules}, {data, rules}).empty());
    // Asset-only mods do not count either way.
    CHECK(diff({data, pictures}, {data}).empty());
    CHECK(diff({data}, {pictures, data}).empty());
    // Missing, another version, other files, one too many, another order.
    CHECK(joined(diff({data, rules}, {data})) == "the host uses mod test.rules 2.0.0, which you do not have enabled\n");
    ModRecord older = rules;
    older.version = "1.9.0";
    CHECK(joined(diff({rules}, {older})) == "mod test.rules: the host uses version 2.0.0, you have 1.9.0\n");
    ModRecord changed = rules;
    changed.hash = std::string(32, '9');
    CHECK(joined(diff({rules}, {changed})).find("your copy's files differ from the one the host uses") != std::string::npos);
    CHECK(joined(diff({data}, {data, rules})) == "mod test.rules 2.0.0 changes the game, and the host does not use it: turn it off\n");
    CHECK(joined(diff({data, rules}, {rules, data})).find("the mods load in another order") != std::string::npos);
    // The identity of a set counts only what changes the game.
    CHECK(ruleset::modSetIdentity(std::vector<ModRecord>{pictures}).empty());
    CHECK(ruleset::modSetIdentity(std::vector<ModRecord>{data, pictures}) == ruleset::modSetIdentity(std::vector<ModRecord>{data}));
    CHECK(ruleset::modSetIdentity(std::vector<ModRecord>{data, rules}) != ruleset::modSetIdentity(std::vector<ModRecord>{rules, data}));
}
