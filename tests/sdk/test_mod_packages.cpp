// Mod packages: folders, .zip files and classic mods; what they hold and
// their identity (docs/sdk/packages-and-data.md).

#include "mod_fixture.hpp"
#include "mods/package.hpp"
#include "mods/zip.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

// Copies a folder (a fixture mod) somewhere a test may change it.
void copyTree(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

std::vector<ZipInput> zipOf(const fs::path& root, std::string_view prefix = {}) {
    std::vector<ZipInput> out;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        const std::string text = readText(f);
        out.push_back({std::string(prefix) + f.lexically_relative(root).generic_string(), std::vector<uint8_t>(text.begin(), text.end())});
    }
    return out;
}

} // namespace

TEST_CASE("sdk packages: a folder with a manifest") {
    const Package p = openFixtureMod("escort-hull");
    CHECK_FALSE(p.classic);
    CHECK_FALSE(p.zipped);
    CHECK(p.id() == "test.escort-hull");
    CHECK(p.label() == "test.escort-hull 1.0.0");
    CHECK(p.tiers == (kTierAssets | kTierData));
    CHECK(p.affectsGame());
    CHECK(p.hash.size() == 32);
    CHECK(p.warnings.empty());
    CHECK(p.assetRoot() == p.root / "assets");
    REQUIRE(p.dataScripts().size() == 1);
    CHECK(p.dataScripts().front()->path == "data/hulls.toml");
    CHECK(p.gameFiles().empty());
    const ruleset::ModRecord r = p.record();
    CHECK(r.id == "test.escort-hull");
    CHECK(r.version == "1.0.0");
    CHECK(r.hash == p.hash);
    CHECK(r.affectsGame);

    const Package lib = openFixtureMod("common-lib");
    CHECK(lib.tiers == kTierData);
    CHECK(tierNames(lib.tiers) == "data");
    CHECK(tierNames(kTierAssets | kTierAi | kTierInterface) == "assets, AI, interface");
}

TEST_CASE("sdk packages: tiers from the folders, and what data/ may hold") {
    ModDir m("tiers", "test.tiers");
    m.file("assets/Sounds/Ping.wav", "RIFF");
    m.file("ai/player.py", "pass\n");
    m.file("scripts/hooks.py", "pass\n");
    m.file("ui/panel.toml", "x = 1\n");
    m.file("text/strings.toml", "x = 1\n");
    m.file("data/Pictures/Hull.bmp", "BM");          // not a game file
    m.file("assets/Ai/Default_AI_Anger.txt", "x");  // a game file in assets/
    const Package p = m.open();
    CHECK(p.tiers == (kTierAssets | kTierAi | kTierScripts | kTierInterface | kTierText));
    CHECK(p.affectsGame());
    REQUIRE(p.warnings.size() == 2);
    CHECK(p.warnings[0].find("assets/Ai/Default_AI_Anger.txt: game files are read from data/") != std::string::npos);
    CHECK(p.warnings[1].find("data/Pictures/Hull.bmp: not something data/ holds") != std::string::npos);

    ModDir assetsOnly("assets", "test.assets-only");
    assetsOnly.file("assets/Pictures/RaceGeneric/Generic_Mini_X.bmp", "BM");
    CHECK_FALSE(assetsOnly.open().affectsGame());
}

TEST_CASE("sdk packages: game files in the game folder's layout") {
    const Package p = openFixtureMod("ai-tweaks");
    const std::vector<GameMount> files = p.gameFiles();
    REQUIRE(files.size() == 1);
    CHECK(files[0].installPath == "Ai/Testers/Testers_AI_Anger.txt");
    CHECK(files[0].packagePath == "data/Ai/Testers/Testers_AI_Anger.txt");

    ModDir m("mounts", "test.mounts");
    m.file("data/Components.txt", dataText("Name := X"));
    m.file("data/Pictures/Races/Zorg/Zorg_AI_General.txt", dataText("Name := Zorg"));
    m.file("data/Dsgnname/Zorg.txt", "Alpha\n");
    m.file("data/notes.md", "notes\n");
    const Package q = m.open();
    std::vector<std::string> paths;
    for (const GameMount& g : q.gameFiles()) paths.push_back(g.installPath);
    CHECK(paths == std::vector<std::string>{"Data/Components.txt", "Dsgnname/Zorg.txt", "Pictures/Races/Zorg/Zorg_AI_General.txt"});
}

TEST_CASE("sdk packages: a classic mod has no manifest") {
    const Package p = openFixtureMod("classic-names");
    CHECK(p.classic);
    CHECK(p.id() == "classic.classic-names");
    CHECK(p.manifest.version.text == "0");
    CHECK(p.tiers == (kTierAssets | kTierData));
    const std::vector<GameMount> files = p.gameFiles();
    REQUIRE(files.size() == 1);
    CHECK(files[0].installPath == "Data/SystemNames.txt");  // a data file at its top; readme.txt is not one
    CHECK(p.assetRoot() == p.root);

    // With a Data folder, the folder is the game folder's layout.
    TempDir dir("sdk_classic");
    writeText(dir / "Old Mod" / "Data" / "Components.txt", dataText("Name := X"));
    writeText(dir / "Old Mod" / "Ai" / "Default_AI_Anger.txt", dataText("Regular Decrease := -1"));
    writeText(dir / "Old Mod" / "Pictures" / "Races" / "Zorg" / "Zorg_AI_General.txt", dataText("Name := Zorg"));
    writeText(dir / "Old Mod" / "Pictures" / "Races" / "Zorg" / "Zorg_Main.bmp", "BM");
    writeText(dir / "Old Mod" / "Components.txt", "not read: outside Data\n");
    auto q = openPackage(dir / "Old Mod");
    REQUIRE(q);
    CHECK(q->id() == "classic.old-mod");
    std::vector<std::string> paths;
    for (const GameMount& g : q->gameFiles()) paths.push_back(g.installPath);
    CHECK(paths == std::vector<std::string>{"Ai/Default_AI_Anger.txt", "Data/Components.txt", "Pictures/Races/Zorg/Zorg_AI_General.txt"});
    CHECK(q->tiers == (kTierAssets | kTierData));
}

TEST_CASE("sdk packages: the identity covers the manifest and every file but assets/ and ui/") {
    TempDir dir("sdk_identity");
    copyTree(fixtureMod("escort-hull"), dir / "a");
    copyTree(fixtureMod("escort-hull"), dir / "b");
    auto hashOf = [&](std::string_view which) {
        auto p = openPackage(dir / which);
        REQUIRE(p);
        return p->hash;
    };
    const std::string original = openFixtureMod("escort-hull").hash;
    // The same files: the same identity, wherever they are.
    CHECK(hashOf("a") == original);
    CHECK(hashOf("b") == original);
    // Pictures and interface files do not change it.
    writeText(dir / "a" / "assets" / "Pictures" / "RaceGeneric" / "Generic_Mini_EscortCarrier.bmp", "BM other");
    writeText(dir / "a" / "ui" / "panel.toml", "x = 2\n");
    CHECK(hashOf("a") == original);
    // Text files count with their line ends as LF.
    const std::string patch = readText(dir / "b" / "data" / "hulls.toml");
    std::string crlf;
    for (char c : patch) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    writeText(dir / "b" / "data" / "hulls.toml", crlf);
    CHECK(hashOf("b") == original);
    // Data, the manifest, any other file: a new identity.
    writeText(dir / "b" / "data" / "hulls.toml", patch + "\n# a comment\n");
    CHECK(hashOf("b") != original);
    writeText(dir / "b" / "data" / "hulls.toml", patch);
    CHECK(hashOf("b") == original);
    writeText(dir / "b" / "mod.toml", readText(fixtureMod("escort-hull") / "mod.toml") + "\n");
    CHECK(hashOf("b") != original);
    copyTree(fixtureMod("escort-hull"), dir / "c");
    writeText(dir / "c" / "README.md", "Hello\n");
    auto c = openPackage(dir / "c");
    REQUIRE(c);
    CHECK(c->hash != original);
}

TEST_CASE("sdk packages: a .zip reads like its folder") {
    TempDir dir("sdk_zip");
    const Package folder = openFixtureMod("escort-hull");
    OpenOptions options;
    options.cacheDir = dir / "cache";
    // Files at the archive's top.
    REQUIRE(writeZip(dir / "flat.zip", zipOf(fixtureMod("escort-hull"))));
    auto flat = openPackage(dir / "flat.zip", options);
    REQUIRE_MESSAGE(flat, (flat ? std::string{} : flat.error()));
    CHECK(flat->zipped);
    CHECK(flat->id() == folder.id());
    CHECK(flat->hash == folder.hash);
    CHECK(flat->tiers == folder.tiers);
    CHECK(fs::exists(flat->assetRoot() / "Pictures" / "RaceGeneric" / "Generic_Mini_EscortCarrier.bmp"));
    // Or in one top folder, as when a folder is zipped.
    REQUIRE(writeZip(dir / "nested.zip", zipOf(fixtureMod("escort-hull"), "escort-hull/")));
    auto nested = openPackage(dir / "nested.zip", options);
    REQUIRE(nested);
    CHECK(nested->hash == folder.hash);
    CHECK(nested->root.filename() == "escort-hull");
    // Unpacked once: opening it again uses the same copy.
    auto again = openPackage(dir / "flat.zip", options);
    REQUIRE(again);
    CHECK(again->root == flat->root);
    // A classic mod as a .zip.
    REQUIRE(writeZip(dir / "classic-names.zip", zipOf(fixtureMod("classic-names"))));
    auto classic = openPackage(dir / "classic-names.zip", options);
    REQUIRE(classic);
    CHECK(classic->classic);
    CHECK(classic->hash == openFixtureMod("classic-names").hash);

    // Not a zip, and names a stranger's archive must not hold.
    writeText(dir / "broken.zip", "not an archive");
    CHECK_FALSE(openPackage(dir / "broken.zip", options));
    CHECK_FALSE(openPackage(dir / "missing", options));
    CHECK(safeEntryName("data/x.toml"));
    CHECK_FALSE(safeEntryName("../x"));
    CHECK_FALSE(safeEntryName("a/../../x"));
    CHECK_FALSE(safeEntryName("/etc/x"));
    CHECK_FALSE(safeEntryName("C:/x"));
    CHECK_FALSE(safeEntryName("a\\b"));
    CHECK_FALSE(safeEntryName("a//b"));
    CHECK_FALSE(writeZip(dir / "bad.zip", std::vector<ZipInput>{{"../escape.txt", {}}}));
}

TEST_CASE("sdk packages: a packed mod records its identity") {
    TempDir dir("sdk_packed");
    copyTree(fixtureMod("common-lib"), dir / "lib");
    auto p = openPackage(dir / "lib");
    REQUIRE(p);
    writeText(dir / "lib" / std::string(kIdentityFile), identityFileText(*p));
    auto withId = openPackage(dir / "lib");
    REQUIRE(withId);
    CHECK(withId->hash == p->hash);  // the record is not part of what it records
    CHECK(withId->warnings.empty());
    writeText(dir / "lib" / "data" / "more.toml", "[[abilities.declare]]\nname = \"Other\"\n");
    auto changed = openPackage(dir / "lib");
    REQUIRE(changed);
    REQUIRE(changed->warnings.size() == 1);
    CHECK(changed->warnings[0].find("differ from those it was packed with") != std::string::npos);
}
