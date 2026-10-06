// Mod sets: load order, dependencies and the player's choice of mods
// (docs/sdk/packages-and-data.md "Load order").

#include "mod_fixture.hpp"
#include "ruleset/mods.hpp"

#include <doctest/doctest.h>

#include <algorithm>

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
    choice.folders.user = modsFolder;
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
    auto again = modsForGame(recorded, ModFolders{modsFolder, {}});
    REQUIRE(again);
    CHECK(ids(*again) == std::vector<std::string>{"test.common-lib", "test.escort-hull"});
    std::vector<ruleset::ModRecord> other = recorded;
    other[0].hash = std::string(32, '0');
    auto differs = modsForGame(other, ModFolders{modsFolder, {}});
    REQUIRE_FALSE(differs);
    CHECK(joined(differs.error()).find("the copy in") != std::string::npos);
    // An asset-only mod a game recorded may be missing.
    ruleset::ModRecord pictures{"test.pictures-only", "1.0.0", std::string(32, 'a'), false};
    auto withoutPictures = modsForGame(std::vector<ruleset::ModRecord>{pictures}, ModFolders{modsFolder, {}});
    REQUIRE(withoutPictures);
    CHECK(withoutPictures->empty());
}

TEST_CASE("sdk mod sets: the mods that come with OpenSE4, after the player's own") {
    // A release's layout: mods/ beside the programs holds the bundled mods.
    TempDir dir("sdk_bundled");
    const fs::path program = dir / "OpenSE4";
    const fs::path bundled = program / "mods";
    fs::create_directories(bundled);
    fs::copy(fixtureMod("escort-hull"), bundled / "escort-hull", fs::copy_options::recursive);
    fs::copy(fixtureMod("common-lib"), bundled / "common-lib", fs::copy_options::recursive);
    // The player's own copy of the library: another version, which replaces the bundled one.
    const fs::path user = dir / "Mods";
    fs::create_directories(user);
    fs::copy(fixtureMod("common-lib"), user / "common-lib", fs::copy_options::recursive);
    std::string manifest = readText(user / "common-lib" / "mod.toml");
    manifest.replace(manifest.find("1.2.0"), 5, "1.4.0");
    writeText(user / "common-lib" / "mod.toml", manifest);
    CHECK(bundledModsFolderFor(program, {}) == bundled);
    const ModFolders folders{user, bundledModsFolderFor(program, {})};

    const ModLibrary lib = scanMods(folders);
    CHECK(lib.problems.empty());
    REQUIRE(lib.find("test.common-lib"));
    CHECK_FALSE(lib.find("test.common-lib")->bundled);
    CHECK(lib.find("test.common-lib")->manifest.version.text == "1.4.0");   // the player's copy wins
    REQUIRE(lib.find("test.escort-hull"));
    CHECK(lib.find("test.escort-hull")->bundled);                             // found by id among the bundled mods
    REQUIRE(lib.replaced.size() == 1);
    CHECK(lib.replaced[0].id() == "test.common-lib");
    CHECK(lib.replaced[0].bundled);
    CHECK(lib.packages.size() == 2);

    // Chosen by id: the bundled mod is found, the player's library used.
    ModChoice choice;
    choice.folders = folders;
    choice.mods = {"test.escort-hull", "test.common-lib"};
    auto set = selectMods(choice);
    REQUIRE_MESSAGE(set, (set ? std::string{} : joined(set.error())));
    REQUIRE(set->packages.size() == 2);
    CHECK(set->packages[0].bundled);
    CHECK(set->packages[1].manifest.version.text == "1.4.0");
    // In neither folder: the message names both.
    choice.mods = {"test.nowhere"};
    auto none = selectMods(choice);
    REQUIRE_FALSE(none);
    CHECK(joined(none.error()).find("no mod 'test.nowhere' in the mods folder") != std::string::npos);
    CHECK(joined(none.error()).find("or the mods that come with OpenSE4") != std::string::npos);
    // Without the bundled mods (--no-bundled-mods), the escort hull is nowhere.
    choice.folders.bundled.clear();
    choice.mods = {"test.escort-hull"};
    auto without = selectMods(choice);
    REQUIRE_FALSE(without);
    CHECK(joined(without.error()).find("come with OpenSE4") == std::string::npos);

    // A game's mods: the bundled hull by id and identity; the library the game played
    // with was the bundled one, but the player's copy replaces it, and says so.
    const std::vector<ruleset::ModRecord> hull{openFixtureMod("escort-hull").record()};
    auto again = modsForGame(hull, folders);
    REQUIRE_MESSAGE(again, (again ? std::string{} : joined(again.error())));
    REQUIRE(again->packages.size() == 1);
    CHECK(again->packages[0].bundled);
    const std::vector<ruleset::ModRecord> library{openFixtureMod("common-lib").record()};
    auto replaced = modsForGame(library, folders);
    REQUIRE_FALSE(replaced);
    CHECK(joined(replaced.error()).find("it replaces the one that comes with OpenSE4") != std::string::npos);
    auto missing = modsForGame(hull, ModFolders{user, {}});
    REQUIRE_FALSE(missing);
    CHECK(joined(missing.error()).find("the game uses mod test.escort-hull 1.0.0, which is not in the mods folder") != std::string::npos);
}

TEST_CASE("sdk mod sets: a developer build's bundled mods from the source tree") {
    // The source tree's layout: mods/ with bundled.txt naming the mods that ship,
    // beside the SDK's examples and mods that do not.
    TempDir dir("sdk_bundled_tree");
    const fs::path tree = dir / "mods";
    fs::create_directories(tree / "examples");
    fs::copy(fixtureMod("escort-hull"), tree / "escort-hull", fs::copy_options::recursive);
    fs::copy(fixtureMod("picture-pack"), tree / "picture-pack", fs::copy_options::recursive);
    fs::copy(fixtureMod("common-lib"), tree / "examples" / "common-lib", fs::copy_options::recursive);
    writeText(tree / "bundled.txt", "# The mods that ship.\n\nescort-hull   # the hull\n");
    CHECK(readBundledList(tree / "bundled.txt") == std::vector<std::string>{"escort-hull"});

    // No mods/ beside the program: the source tree's, when it has the list.
    const fs::path program = dir / "build";
    fs::create_directories(program);
    CHECK(bundledModsFolderFor(program, tree) == tree);
    CHECK(bundledModsFolderFor(program, dir / "elsewhere").empty());
    CHECK(bundledModsFolderFor(program, {}).empty());
    fs::create_directories(program / "mods");
    CHECK(bundledModsFolderFor(program, tree) == program / "mods");   // a mods/ beside the program wins

    // Only the listed folders: not the examples, nor a mod the list leaves out.
    ModLibrary lib = scanBundledMods(tree);
    CHECK(lib.problems.empty());
    REQUIRE(lib.packages.size() == 1);
    CHECK(lib.packages[0].id() == "test.escort-hull");
    CHECK(lib.packages[0].bundled);
    // A listed folder that is not there is a problem; a name with a slash is no folder of it.
    writeText(tree / "bundled.txt", "escort-hull\ngone\nexamples/common-lib\n");
    lib = scanBundledMods(tree);
    CHECK(lib.packages.size() == 1);
    CHECK(lib.problems.size() == 2);
    // Without the list (a release's mods/), every mod there.
    fs::remove(tree / "bundled.txt");
    fs::remove_all(tree / "examples");
    lib = scanBundledMods(tree);
    CHECK(lib.packages.size() == 2);
    CHECK(std::all_of(lib.packages.begin(), lib.packages.end(), [](const Package& p) { return p.bundled; }));
}

TEST_CASE("sdk mod sets: the mods that ship with OpenSE4 (mods/bundled.txt)") {
    // Our own mods/ folder: each folder its list names is a mod that opens, Hegemon among them.
    const fs::path tree(OPENSE4_MODS_DIR);
    REQUIRE(fs::is_regular_file(tree / std::string(kBundledListFile)));
    const ModLibrary lib = scanBundledMods(tree);
    CHECK_MESSAGE(lib.problems.empty(), joined(lib.problems));
    CHECK(lib.packages.size() == readBundledList(tree / std::string(kBundledListFile)).size());
    const Package* hegemon = lib.find("opense4.hegemon");
    REQUIRE(hegemon);
    CHECK(hegemon->bundled);
    CHECK(hegemon->affectsGame());
    REQUIRE(hegemon->manifest.aiPlayers.size() == 1);
    CHECK(hegemon->manifest.aiPlayers[0].name == "Hegemon");
    for (const Package& p : lib.packages) CHECK_MESSAGE(p.source.parent_path() == tree, p.source.string());   // never mods/examples
#ifdef OPENSE4_TEST_DEV_PATHS
    // A developer build without mods/ beside its programs finds them there.
    TempDir program("sdk_bundled_program");
    CHECK(bundledModsFolder(program.path()) == tree);
#endif
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
