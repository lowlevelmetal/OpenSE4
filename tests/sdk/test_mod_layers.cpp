// The layered files: mods' pictures, sounds, music, fonts and pointers over
// the install (assets::InstallFiles), and their game files over the
// install's (mods::GameData). Nothing is written into the install.

#include "assets/assets.hpp"
#include "game/ai_data.hpp"
#include "game/rules.hpp"
#include "mod_fixture.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

// Every file under a folder with its content: what "untouched" compares.
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) out[e.path().lexically_relative(root).generic_string()] = readText(e.path());
    return out;
}

} // namespace

TEST_CASE("sdk layers: mods' files over the install's, the later mod first") {
    const fs::path install = fixtureDir() / "install";
    const auto before = snapshot(install);
    TempDir dir("sdk_layers");
    writeText(dir / "a" / "Pictures" / "Game" / "Normal.cur", "mod a");
    writeText(dir / "a" / "Fonts" / "TestFace.fon", "mod a font");
    writeText(dir / "a" / "Music" / "Theme.mp3", "mod a music");
    writeText(dir / "b" / "pictures" / "game" / "normal.cur", "mod b");  // any case
    writeText(dir / "b" / "Sounds" / "Ping.wav", "mod b sound");

    assets::InstallFiles plain(install);
    assets::InstallFiles files(install);
    files.addLayer(dir / "a", "a");
    files.addLayer(dir / "b", "b");
    CHECK(files.layerCount() == 2);
    // The later mod wins, then the earlier, then the install.
    CHECK(readText(*files.find("Pictures/Game/Normal.cur")) == "mod b");
    CHECK(readText(*files.find("Music/Theme.mp3")) == "mod a music");
    CHECK(readText(*files.find("SOUNDS/ping.WAV")) == "mod b sound");
    CHECK(*files.find("Pictures/Game/Target.cur") == *plain.find("Pictures/Game/Target.cur"));
    CHECK_FALSE(files.find("Pictures/Game/Nothing.bmp"));
    // Fonts and pointers: the mods, then Path.txt's mod folder, then the install (as before without mods).
    CHECK(readText(*files.findModFirst("Fonts/TestFace.fon")) == "mod a font");
    CHECK(plain.findModFirst("Fonts/TestFace.fon")->generic_string().find("TestMod") != std::string::npos);
    CHECK(files.findModFirst("Fonts/OnlyBase.fon") == plain.findModFirst("Fonts/OnlyBase.fon"));
    // Path.txt's folder still applies only where it did.
    CHECK(plain.find("Fonts/TestFace.fon")->generic_string().find("TestMod") == std::string::npos);
    // The install is as it was.
    CHECK(snapshot(install) == before);
}

TEST_CASE("sdk layers: a fixture mod's hull pictures are found and read") {
    const Package hull = openFixtureMod("escort-hull");
    assets::InstallFiles files(fixtureDir() / "install");
    files.addLayer(hull.assetRoot(), hull.label());
    const auto mini = files.find("Pictures/RaceGeneric/Generic_Mini_EscortCarrier.bmp");
    REQUIRE(mini);
    const auto image = assets::loadImage(*mini, true);
    REQUIRE(image);
    CHECK(image->width == 36);
    CHECK(image->height == 36);
    const auto portrait = files.find("pictures/racegeneric/generic_portrait_escortcarrier.bmp");
    REQUIRE(portrait);
    CHECK(assets::loadImage(*portrait, false)->width == 128);
    // A classic mod's folder is a layer as it is.
    const Package classic = openFixtureMod("classic-names");
    files.addLayer(classic.assetRoot(), classic.label());
    CHECK(files.find("Pictures/RaceGeneric/Generic_Mini_Cutter.bmp")->generic_string().find("classic-names") != std::string::npos);
}

TEST_CASE("sdk layers: game files, races and design names from mods") {
    GameFolder g("gamefiles");
    g.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9");
    g.write("Pictures/Races/Testian/Testian_AI_General.txt", "Name := Testian\nDesign Name File := Testian.txt");
    g.writePlain("Dsgnname/Testian.txt", "Arrow\nBolt\n");
    const auto before = snapshot(g.root);

    ModDir a("gf_a", "test.a");
    a.file("data/Ai/Default_AI_Anger.txt", dataText("Regular Decrease := -5"));
    a.file("data/Pictures/RaceNeutral/Drifter/Drifter_AI_General.txt", dataText("Name := Drifters"));
    a.file("data/Dsgnname/Testian.txt", "Comet\nDart\n");
    ModDir b("gf_b", "test.b");
    b.file("data/Ai/Default_AI_Anger.txt", dataText("Regular Decrease := -3"));
    b.file("data/Pictures/Races/Zorg/Zorg_AI_General.txt", dataText("Name := Zorgians"));

    const LoadedDataSet d = loadDataSet(g.root, g.data(), modSet({a.open(), b.open()}));
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    const ruleset::GameFiles& files = *d.data;
    // The later mod's table.
    auto anger = files.file("ai/default_ai_anger.txt");
    REQUIRE(anger);
    CHECK(anger->records[0].find("Regular Decrease")->value == "-3");
    CHECK(anger->origin == "mod test.b: data/Ai/Default_AI_Anger.txt");
    // Folders merged over the layers.
    std::vector<std::string> races;
    for (const ruleset::FileEntry& e : files.list("Pictures/Races")) races.push_back(e.name);
    CHECK(races == std::vector<std::string>{"Testian", "Zorg"});
    // The rules see the mods' races, tables and design names.
    const game::Rules rules(*d.ruleset);
    std::vector<std::string> presets;
    for (const auto& p : rules.racePresets()) presets.push_back(p.name);
    CHECK(presets == std::vector<std::string>{"Testian", "Zorgians", "Drifters"});
    CHECK(game::ai::profileFor(rules, "Testian", "").anger.regularDecrease == -3);
    CHECK(game::ai::designNameList(rules, "Testian.txt") == std::vector<std::string>{"Comet", "Dart"});
    CHECK(game::ai::designNameFiles(rules) == std::vector<std::string>{"Testian.txt"});
    CHECK(files.path("Dsgnname/Testian.txt")->generic_string().find("gf_a") != std::string::npos);
    // The install is as it was.
    CHECK(snapshot(g.root) == before);

    // An asset mod's game files are not game files: they are left out.
    ModDir sneaky("gf_sneaky", "test.sneaky");
    sneaky.file("assets/Ai/Default_AI_Anger.txt", dataText("Regular Decrease := 50"));
    const LoadedDataSet s = loadDataSet(g.root, g.data(), modSet({sneaky.open()}));
    CHECK(s.data->file("Ai/Default_AI_Anger.txt")->records[0].find("Regular Decrease")->value == "-9");
}

TEST_CASE("sdk layers: the install's own files read as before") {
    GameFolder g("asbefore");
    g.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9");
    g.write("Ai/Aggressive/Aggressive_AI_Anger.txt", "Regular Decrease := -1");
    g.write("Pictures/Races/Testian/Testian_AI_General.txt", "Name := Testian");
    g.write("Pictures/Races/Testian/Testian_AI_Anger.txt", "Regular Decrease := -4");
    // Without mods, through the mods' loader and the plain one alike.
    const LoadedDataSet d = loadDataSet(g.root, g.data(), ModSet{});
    REQUIRE(d.ruleset);
    const game::Rules viaMods(*d.ruleset);
    auto plain = ruleset::loadRuleset(g.data());
    REQUIRE(plain.ruleset);
    const game::Rules viaInstall(*plain.ruleset, g.root);
    for (const game::Rules* r : {&viaMods, &viaInstall}) {
        CHECK(game::ai::profileFor(*r, "Testian", "").anger.regularDecrease == -4);
        CHECK(game::ai::profileFor(*r, "Nobody", "").anger.regularDecrease == -9);
        CHECK(game::ai::profileFor(*r, "", "Aggressive").anger.regularDecrease == -1);
        CHECK(game::ai::ministerStyles(*r) == std::vector<std::string>{"Aggressive"});
        REQUIRE(r->racePresets().size() == 1);
    }
    CHECK(viaMods.files()->fingerprint() == viaInstall.files()->fingerprint());
}
