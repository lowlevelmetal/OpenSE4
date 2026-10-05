// opense4-sdk's commands, run as a player runs them, on our own fixtures
// (docs/sdk/packages-and-data.md "opense4-sdk").

#include "image_files.hpp"
#include "mod_fixture.hpp"
#include "mods/zip.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

using namespace opense4;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

struct Run {
    int code = -1;
    std::string out;
};

Run sdk(const std::string& args) {
    static TempDir outputs("sdk_tool_output");
    static int n = 0;
    const fs::path file = outputs / std::format("run{}.txt", ++n);
    std::string command = std::format("\"{}\" {} > \"{}\" 2>&1", OPENSE4_SDK_EXE, args, file.string());
#if defined(_WIN32)
    command = "\"" + command + "\"";  // cmd.exe drops the outer quotes
#endif
    int code = std::system(command.c_str());
#if !defined(_WIN32)
    if (WIFEXITED(code)) code = WEXITSTATUS(code);
#endif
    return Run{code, readText(file)};
}

std::string q(const fs::path& p) { return "\"" + p.string() + "\""; }

} // namespace

TEST_CASE("sdk tool: new, check, info") {
    GameFolder g("tool_new");
    TempDir dir("sdk_tool_new");
    for (std::string_view kind : {"assets", "data", "ai", "rules"}) {
        const fs::path mod = dir / std::string(kind);
        const Run made = sdk(std::format("new {} {} --id=test.new-{}", kind, q(mod), kind));
        CHECK_MESSAGE(made.code == 0, made.out);
        CHECK(fs::exists(mod / "mod.toml"));
        auto p = mods::openPackage(mod);
        REQUIRE_MESSAGE(p, (p ? std::string{} : p.error()));
        CHECK(p->id() == std::format("test.new-{}", kind));
        CHECK(p->manifest.api == mods::kApiVersion);
        const Run checked = sdk(std::format("check {} --data={}", q(mod), q(g.root)));
        CHECK_MESSAGE(checked.code == 0, checked.out);
        CHECK_MESSAGE(checked.out.find("No problems found.") != std::string::npos, checked.out);
    }
    CHECK(fs::exists(dir / "ai" / "ai" / "player.py"));
    CHECK(readText(dir / "rules" / "scripts" / "rules.py").find("later step") != std::string::npos);
    CHECK(sdk(std::format("new data {}", q(dir / "data"))).code == 2);  // not into a folder in use
    CHECK(sdk(std::format("new campaign {}", q(dir / "x"))).code == 2);

    const Run info = sdk(std::format("info {}", q(fixtureMod("needs-lib"))));
    CHECK(info.code == 0);
    CHECK(info.out.find("test.needs-lib 0.3.0") != std::string::npos);
    CHECK(info.out.find("requires:    test.common-lib >=1.0, <2") != std::string::npos);
    CHECK(info.out.find(openFixtureMod("needs-lib").hash) != std::string::npos);
    CHECK(info.out.find("patch:       data/anchor.toml") != std::string::npos);
}

TEST_CASE("sdk tool: check finds the dependencies and reports problems") {
    GameFolder g("tool_check");
    TempDir dir("sdk_tool_check");
    fs::create_directories(dir / "Mods");
    fs::copy(fixtureMod("common-lib"), dir / "Mods" / "common-lib", fs::copy_options::recursive);
    // Its requirement from the mods folder.
    const Run ok = sdk(std::format("check {} --data={} --mods-dir={}", q(fixtureMod("needs-lib")), q(g.root), q(dir / "Mods")));
    CHECK_MESSAGE(ok.code == 0, ok.out);
    CHECK(ok.out.find("Load order: test.common-lib 1.2.0, test.needs-lib 0.3.0") != std::string::npos);
    CHECK(ok.out.find("1 declared abilities") != std::string::npos);
    // Without it.
    const Run missing = sdk(std::format("check {} --data={} --mods-dir={}", q(fixtureMod("needs-lib")), q(g.root), q(dir / "Nothing")));
    CHECK(missing.code == 1);
    CHECK(missing.out.find("needs mod test.common-lib") != std::string::npos);
    // A typo and a reference left behind.
    ModDir bad("tool_bad", "test.bad");
    bad.file("data/patch.toml", "[[components.change]]\nname = \"Spark Drive\"\nset = { \"Tonage Space Taken\" = 5 }\n\n"
                                "[[tech_areas.remove]]\nname = \"Drive Systems\"\n");
    const Run problems = sdk(std::format("check {} --data={}", q(bad.root), q(g.root)));
    CHECK(problems.code == 1);
    CHECK(problems.out.find("error: mod test.bad, data/patch.toml:3: Components.txt [Spark Drive]: components records have no field") != std::string::npos);
    CHECK(problems.out.find("removing tech_areas 'Drive Systems' leaves a reference") != std::string::npos);
    // Pictures: the hull's own are there; a picture in a format the game does not read is an error.
    const Run hull = sdk(std::format("check {} --data={}", q(fixtureMod("escort-hull")), q(g.root)));
    CHECK_MESSAGE(hull.code == 0, hull.out);
    CHECK_MESSAGE(hull.out.find("No problems found.") != std::string::npos, hull.out);
    ModDir pictures("tool_pictures", "test.pictures");
    pictures.file("data/hull.toml", "[[vehicle_sizes.add]]\nname = \"Test Lighter\"\ncopy_from = \"Test Cutter\"\n"
                                   "set = { \"Primary Bitmap Name\" = \"Lighter\" }\n");
    pictures.file("assets/Music/Theme.ogg", "OggS");
    pictures.file("assets/Pictures/RaceGeneric/Generic_Mini_Nobody.bmp", "BM");
    pictures.file("assets/Docs/notes.pdf", "%PDF");
    const Run p = sdk(std::format("check {} --data={}", q(pictures.root), q(g.root)));
    CHECK(p.code == 1);
    CHECK(p.out.find("the hull [Test Lighter] has no Mini picture 'Lighter'") != std::string::npos);
    CHECK(p.out.find("assets/Music/Theme.ogg: the OGG Vorbis file cannot be read") != std::string::npos);
    CHECK(p.out.find("assets/Pictures/RaceGeneric/Generic_Mini_Nobody.bmp: the picture cannot be read") != std::string::npos);
    CHECK(p.out.find("no hull's Primary or Alternate Bitmap Name is 'nobody'") != std::string::npos);
    CHECK(p.out.find("assets/Docs/notes.pdf: the game reads pictures, sounds, music and fonts only") != std::string::npos);

    // Beyond the original's formats: PNG pictures at twice their classic size and an OGG Vorbis sound.
    const Run pack = sdk(std::format("check {} --data={}", q(fixtureMod("picture-pack")), q(g.root)));
    CHECK_MESSAGE(pack.code == 0, pack.out);
    CHECK_MESSAGE(pack.out.find("no hull's Primary or Alternate Bitmap Name is 'lancer'") != std::string::npos, pack.out);   // a picture for designs
    CHECK_MESSAGE(pack.out.find("cannot be read") == std::string::npos, pack.out);
    ModDir sizes("tool_sizes", "test.sizes");
    writePng(sizes.root / "assets/Pictures/RaceGeneric/Generic_Portrait_Odd.png", 200, 150, solid(1, 2, 3));
    writePng(sizes.root / "assets/Pictures/RaceGeneric/Generic_Mini_Small.png", 20, 20, solid(1, 2, 3));
    writeBmp(sizes.root / "assets/Pictures/Events/Fine.png", 256, 256, solid(1, 2, 3));   // BMP data under a PNG's name
    writeBytes(sizes.root / "assets/Sounds/ping.wav", {'R', 'I', 'F', 'F'});
    const Run z = sdk(std::format("check {} --data={}", q(sizes.root), q(g.root)));
    CHECK(z.out.find("Generic_Portrait_Odd.png: it is 200x150, not a whole multiple of its kind's 128x128") != std::string::npos);
    CHECK(z.out.find("Generic_Mini_Small.png: it is 20x20, smaller than its kind's 36x36") != std::string::npos);
    CHECK(z.out.find("Fine.png: it is named .png but holds BMP data") != std::string::npos);
    CHECK(z.out.find("Events/Fine.png: it is 256x256") == std::string::npos);   // twice the classic size: fine
    CHECK(z.out.find("assets/Sounds/ping.wav: it is not a WAV file") != std::string::npos);
}

TEST_CASE("sdk tool: dump writes the patched data set elsewhere") {
    GameFolder g("tool_dump");
    TempDir dir("sdk_tool_dump");
    const Run d = sdk(std::format("dump {} {} --data={} --out={}", q(fixtureMod("common-lib")), q(fixtureMod("escort-hull")), q(g.root), q(dir / "out")));
    CHECK_MESSAGE(d.code == 0, d.out);
    const std::string hulls = readText(dir / "out" / "Data" / "VehicleSize.txt");
    CHECK(hulls.find("Name := Escort Carrier\r\n") != std::string::npos);
    CHECK(hulls.find("test.common-lib 1.2.0, test.escort-hull 1.0.0") != std::string::npos);
    // What it wrote is a data set the loader reads.
    const auto dumped = ruleset::loadRuleset(dir / "out" / "Data");
    REQUIRE(dumped.ruleset);
    CHECK(dumped.diagnostics.errors.empty());
    CHECK(dumped.ruleset->findVehicleSize("Escort Carrier"));
    CHECK(dumped.ruleset->components.size() == ruleset::loadRuleset(g.data()).ruleset->components.size());
    // Never into the game folder.
    const Run inside = sdk(std::format("dump {} --data={} --out={}", q(fixtureMod("common-lib")), q(g.root), q(g.root / "dump")));
    CHECK(inside.code == 2);
    CHECK_FALSE(fs::exists(g.root / "dump"));
}

TEST_CASE("sdk tool: pack makes a .zip that opens as the same mod") {
    TempDir dir("sdk_tool_pack");
    fs::copy(fixtureMod("escort-hull"), dir / "escort-hull", fs::copy_options::recursive);
    writeText(dir / "escort-hull" / ".hidden", "left out");
    const Run packed = sdk(std::format("pack {} --out={}", q(dir / "escort-hull"), q(dir / "out" / "escort.zip")));
    CHECK_MESSAGE(packed.code == 0, packed.out);
    const mods::Package folder = openFixtureMod("escort-hull");
    CHECK(packed.out.find(folder.hash) != std::string::npos);
    auto entries = mods::listZip(dir / "out" / "escort.zip");
    REQUIRE(entries);
    std::vector<std::string> names;
    for (const auto& e : *entries) names.push_back(e.name);
    CHECK(std::find(names.begin(), names.end(), "mod.identity") != names.end());
    CHECK(std::find(names.begin(), names.end(), ".hidden") == names.end());
    mods::OpenOptions options;
    options.cacheDir = dir / "cache";
    auto zipped = mods::openPackage(dir / "out" / "escort.zip", options);
    REQUIRE(zipped);
    CHECK(zipped->hash == folder.hash);
    CHECK(zipped->warnings.empty());
    // The same files pack to the same bytes.
    const Run again = sdk(std::format("pack {} --out={}", q(dir / "escort-hull"), q(dir / "out" / "again.zip")));
    CHECK(again.code == 0);
    CHECK(readText(dir / "out" / "escort.zip") == readText(dir / "out" / "again.zip"));
}

TEST_CASE("sdk tool: what is not there yet says so") {
    for (std::string_view command : {"run", "test", "arena", "publish"}) {
        const Run r = sdk(std::string(command));
        CHECK(r.code == 2);
        CHECK(r.out.find("not there yet") != std::string::npos);
    }
    CHECK(sdk("frobnicate").code == 2);
    CHECK(sdk("--help").code == 0);
}
