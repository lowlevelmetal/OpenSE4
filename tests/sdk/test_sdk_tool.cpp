// opense4-sdk's commands, run as a player runs them, on our own fixtures
// (docs/sdk/packages-and-data.md "opense4-sdk").

#include "image_files.hpp"
#include "mod_fixture.hpp"
#include "mods/zip.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <sstream>

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

Run program(const char* exe, const std::string& args) {
    static TempDir outputs("sdk_tool_output");
    static int n = 0;
    const fs::path file = outputs / std::format("run{}.txt", ++n);
    // Cross-compiled tests run the tool through the same emulator as themselves
    // (OPENSE4_TEST_RUNNER, e.g. "qemu-arm -L /usr/arm-linux-gnueabihf").
    const char* runner = std::getenv("OPENSE4_TEST_RUNNER");
    std::string command = std::format("{}{}\"{}\" {} > \"{}\" 2>&1", runner ? runner : "", runner ? " " : "", exe, args,
                                      file.string());
#if defined(_WIN32)
    command = "\"" + command + "\"";  // cmd.exe drops the outer quotes
#endif
    int code = std::system(command.c_str());
#if !defined(_WIN32)
    if (WIFEXITED(code)) code = WEXITSTATUS(code);
#endif
    return Run{code, readText(file)};
}

Run sdk(const std::string& args) { return program(OPENSE4_SDK_EXE, args); }

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
    // The rules template's module is named after the mod, so that two mods' do not conflict.
    CHECK(readText(dir / "rules" / "scripts" / "test_new_rules.py").find("@rules.on(\"after_galaxy\")") != std::string::npos);
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

TEST_CASE("sdk tool: a command without what it needs says so; publish waits for the Steam release") {
    for (std::string_view command : {"run", "test", "arena"}) {
        const Run r = sdk(std::string(command));
        CHECK(r.code == 2);
        CHECK_FALSE(r.out.empty());
    }
    const Run publish = sdk("publish");
    CHECK(publish.code == 2);
    CHECK(publish.out.find("waits for the Steam release") != std::string::npos);
    CHECK(sdk("arena --help").code == 0);
    CHECK(sdk("frobnicate").code == 2);
    CHECK(sdk("--help").code == 0);
}

namespace {

const std::vector<std::string> kCommands{"new", "check", "dump", "pack", "info", "test", "run", "arena", "env-host", "bot", "python", "publish"};

// The options a help text names: every --word in it.
std::set<std::string> optionsIn(const std::string& text) {
    std::set<std::string> out;
    static const std::regex option(R"((^|[^\w-])(--[a-z][a-z0-9-]*))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), option); it != std::sregex_iterator(); ++it) out.insert((*it)[2].str());
    return out;
}

// The documentation that shows opense4-sdk's command lines: README.md, CLAUDE.md, docs/
// (but its specs), the mods' READMEs and the in-game manual.
std::vector<fs::path> commandLineDocs() {
    const fs::path root = fs::path(OPENSE4_DOCS_DIR).parent_path();
    std::vector<fs::path> files{root / "README.md", root / "CLAUDE.md"};
    for (const fs::path& top : {root / "docs", root / "mods", root / "assets" / "learn" / "manual"})
        for (const auto& e : fs::recursive_directory_iterator(top))
            if (e.path().extension() == ".md" && e.path().string().find("spec") == std::string::npos) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

TEST_CASE("sdk tool: every command answers --help, -h and help <command>; the usage lists them all") {
    const Run usage = sdk("--help");
    REQUIRE(usage.code == 0);
    CHECK(sdk("help").out == usage.out);
    CHECK(sdk("-h").out == usage.out);
    for (const std::string& command : kCommands) {
        INFO(command);
        const size_t listed = usage.out.find("  opense4-sdk " + command);
        CHECK_MESSAGE((listed != std::string::npos && std::isspace(static_cast<unsigned char>(usage.out[listed + 14 + command.size()]))),
                      "the usage lists " << command);
        const Run help = sdk(command + " --help");
        CHECK_MESSAGE(help.code == 0, help.out);
        CHECK_MESSAGE(help.out.find("opense4-sdk " + command) != std::string::npos, help.out);
        CHECK(sdk(command + " -h").out == help.out);
        CHECK(sdk("help " + command).out == help.out);
    }
    // The commands' own options, as the usage names them, are those their help names.
    const Run new_ = sdk("new --help");
    for (const char* option : {"--id", "--name", "--from-example", "--examples-dir"}) CHECK(optionsIn(new_.out).contains(option));
    CHECK(optionsIn(sdk("dump --help").out).contains("--mod"));
    CHECK(optionsIn(sdk("info --help").out).contains("--mods-dir"));
    CHECK(sdk("pack . --mods-dir=x").code == 2);   // pack takes a folder, never an id
}

TEST_CASE("sdk tool: the command lines of the docs use opense4-sdk's commands and options") {
    // In code (a fenced block, or `...`), "opense4-sdk <command>" names a command, and the
    // options after it (up to "--", the end of the code, a "|", "#", ";" or "&&") are that
    // command's, or --no-bundled-mods, which every command takes.
    std::map<std::string, std::set<std::string>> options;
    for (const std::string& command : kCommands) {
        options[command] = optionsIn(sdk(command + " --help").out);
        options[command].insert({"--no-bundled-mods", "--help"});
    }
    options["help"] = {};   // opense4-sdk help <command>
    static const std::regex call(R"(opense4-sdk(\.exe)?\s+([a-z][a-z-]*)(?=[\s`]|$)([^`]*))");
    int checked = 0;
    for (const fs::path& file : commandLineDocs()) {
        std::istringstream in(readText(file));
        bool code = false;
        int n = 0;
        for (std::string line; std::getline(in, line);) {
            ++n;
            const size_t first = line.find_first_not_of(' ');
            if (first != std::string::npos && line.compare(first, 3, "```") == 0) {
                code = !code;
                continue;
            }
            std::vector<std::string> parts;
            if (code) parts.push_back(line);
            else
                for (size_t a = line.find('`'); a != std::string::npos;) {
                    const size_t b = line.find('`', a + 1);
                    if (b == std::string::npos) break;
                    parts.push_back(line.substr(a + 1, b - a - 1));
                    a = line.find('`', b + 1);
                }
            for (const std::string& part : parts) {
                for (auto it = std::sregex_iterator(part.begin(), part.end(), call); it != std::sregex_iterator(); ++it) {
                    const std::string command = (*it)[2].str();
                    std::string rest = (*it)[3].str();
                    for (const char* end : {" -- ", "|", " #", ";", "&&", "opense4"})
                        if (const size_t at = rest.find(end); at != std::string::npos) rest.resize(at);
                    const std::string where = std::format("{}:{}: {}", fs::relative(file, fs::path(OPENSE4_DOCS_DIR).parent_path()).generic_string(), n, part);
                    ++checked;
                    if (!options.contains(command)) {
                        CHECK_MESSAGE(false, where << ": opense4-sdk has no command " << command);
                        continue;
                    }
                    for (const std::string& option : optionsIn(rest))
                        CHECK_MESSAGE(options[command].contains(option), where << ": opense4-sdk " << command << " has no option " << option);
                }
            }
        }
    }
    CHECK(checked > 100);
}

TEST_CASE("sdk tool: opense4-server's commands answer --help and -h") {
    const Run usage = program(OPENSE4_SERVER_EXE, "--help");
    REQUIRE(usage.code == 0);
    CHECK(usage.out.find("opense4-server pbem process") != std::string::npos);
    for (const char* args : {"-h", "pbem --help", "pbem -h", "pbem new --help", "pbem process --help", "pbem turn-files --help", "pbem orders --help",
                             "pbem info --help", "bot --help", "password-verifier --help"}) {
        const Run r = program(OPENSE4_SERVER_EXE, args);
        CHECK_MESSAGE(r.code == 0, args << "\n" << r.out);
        CHECK_MESSAGE(r.out == usage.out, args);
    }
    // Every option the help names is one of the server's, and the other way round for these.
    const std::set<std::string> named = optionsIn(usage.out);
    for (const char* option : {"--version", "--allow-data-mismatch", "--no-bundled-mods", "--bot-token", "--trust-new-host-key", "--port"})
        CHECK_MESSAGE(named.contains(option), option);
    const Run version = program(OPENSE4_SERVER_EXE, "--version");
    CHECK(version.code == 0);
    CHECK(version.out.find("network protocol") != std::string::npos);
}
