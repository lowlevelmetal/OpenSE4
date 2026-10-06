// The modder's guide and its example mods (docs/sdk/guide, mods/examples): every example
// checks and passes its own tests with opense4-sdk on our fixture data (those made for a
// classic data set only show that their patches read), and on the installed game when
// OPENSE4_CLASSIC_DATA is set; new --from-example copies one; the API reference and the
// examples' pictures and sounds are those their tools make; and the guide's links lead
// somewhere.

#include "bots_fixture.hpp"
#include "mod_fixture.hpp"

#include "mods/package.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

using namespace opense4;
using namespace opense4::test;
using namespace opense4::sdktest;
namespace fs = std::filesystem;

namespace {

const std::string kSdk = OPENSE4_SDK_EXE;

fs::path examplesDir() { return sourceRoot() / "mods" / "examples"; }

// The examples: every folder of mods/examples with a manifest.
std::vector<std::string> examples() {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(examplesDir()))
        if (e.is_directory() && fs::exists(e.path() / "mod.toml")) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// Examples written for the classic data set: they patch its records by name or by
// what they hold, so on our fixtures they can only show that their patches read.
const std::set<std::string> kClassicOnly{"balance"};

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

std::optional<fs::path> installedData() {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return std::nullopt;
    return ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
}

} // namespace

TEST_CASE("sdk guide: the example mods are those the guide lists, each with a README and tests") {
    const std::vector<std::string> names = examples();
    const std::vector<std::string> expected{"balance", "classic-ai-research", "new-ability", "new-hull", "scenario", "small-ai", "weapon-line"};
    CHECK(names == expected);
    const std::string index = slurp(sourceRoot() / "docs" / "sdk" / "README.md");
    const std::string list = slurp(examplesDir() / "README.md");
    for (const std::string& name : names) {
        CHECK_MESSAGE(fs::exists(examplesDir() / name / "README.md"), name);
        CHECK_MESSAGE(fs::is_directory(examplesDir() / name / "tests"), name);
        CHECK_MESSAGE(contains(index, "mods/examples/" + name), name << " is in docs/sdk/README.md");
        CHECK_MESSAGE(contains(list, "(" + name + "/"), name << " is in mods/examples/README.md");
        auto p = mods::openPackage(examplesDir() / name);
        REQUIRE_MESSAGE(p, (p ? std::string{} : p.error()));
        CHECK(p->id() == "example." + name);
        CHECK(p->warnings.empty());
    }
}

TEST_CASE("sdk guide: every example checks and passes its tests on the fixture data") {
    if (underEmulator()) {
        MESSAGE("under an emulator: skipped");
        return;
    }
    GameFolder g("guide_examples");
    TempDir dir("sdk_guide_examples");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    for (const std::string& name : examples()) {
        const std::string mod = (examplesDir() / name).string();
        const Ran checked = runProgram({kSdk, "check", mod, "--data=" + g.root.string()}, env);
        if (kClassicOnly.contains(name)) {
            // The patches read; what they name is not in our fixtures.
            CHECK_MESSAGE(checked.code == 1, name << "\n" << checked.out);
            int errors = 0;
            for (const std::string& line : lines(checked.out)) {
                if (!line.starts_with("  error: ")) continue;
                ++errors;
                CHECK_MESSAGE((contains(line, " has no record ") || contains(line, " file of those 'files' names")), line);
            }
            CHECK_MESSAGE(errors > 0, checked.out);
            const Ran tested = runProgram({kSdk, "test", mod, "--no-games", "--data=" + (dir.path() / "nothing").string()}, env);
            CHECK_MESSAGE(tested.code == 0, name << "\n" << tested.out);
            CHECK_MESSAGE(contains(tested.out, "0 passed, 0 failed, 5 skipped"), tested.out);
            continue;
        }
        CHECK_MESSAGE(checked.code == 0, name << "\n" << checked.out);
        CHECK_MESSAGE(contains(checked.out, "No problems found."), name << "\n" << checked.out);
        const Ran tested = runProgram({kSdk, "test", mod, "--data=" + g.root.string(), "--turns=3"}, env);
        CHECK_MESSAGE(tested.code == 0, name << "\n" << tested.out);
        CHECK_MESSAGE(contains(tested.out, " 0 failed,"), name << "\n" << tested.out);
    }
}

TEST_CASE("sdk guide: the examples on the installed data set (opt-in: OPENSE4_CLASSIC_DATA)") {
    const auto data = installedData();
    if (!data || underEmulator()) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to check the example mods on the installed game");
        return;
    }
    TempDir dir("sdk_guide_installed");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    const std::string game = "--data=" + data->parent_path().string();
    for (const std::string& name : examples()) {
        const std::string mod = (examplesDir() / name).string();
        const Ran checked = runProgram({kSdk, "check", mod, game}, env);
        CHECK_MESSAGE(checked.code == 0, name << "\n" << checked.out);
        const Ran tested = runProgram({kSdk, "test", mod, game, "--turns=5"}, env);
        CHECK_MESSAGE(tested.code == 0, name << "\n" << tested.out);
        CHECK_MESSAGE(contains(tested.out, " 0 failed, 0 skipped"), name << "\n" << tested.out);
    }
}

TEST_CASE("sdk guide: new --from-example copies an example as a mod of one's own; dump without mods") {
    TempDir dir("sdk_guide_from_example");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    const fs::path target = dir.path() / "mine";
    const Ran made = runProgram({kSdk, "new", "--from-example", "small-ai", target.string(), "--id=me.pioneer", "--name=My Pioneer",
                                 "--examples-dir=" + examplesDir().string()},
                                env);
    REQUIRE_MESSAGE(made.code == 0, made.out);
    auto p = mods::openPackage(target);
    REQUIRE_MESSAGE(p, (p ? std::string{} : p.error()));
    CHECK(p->id() == "me.pioneer");
    CHECK(p->manifest.name == "My Pioneer");
    REQUIRE(p->manifest.aiPlayers.size() == 1);
    CHECK(p->manifest.aiPlayers[0].name == "Pioneer");
    CHECK(fs::exists(target / "ai" / "pioneer.py"));
    CHECK(fs::exists(target / "tests" / "test_pioneer.py"));
    for (const auto& e : fs::recursive_directory_iterator(target)) CHECK_MESSAGE(e.path().filename() != "__pycache__", e.path().string());
    // Not into a folder in use; an unknown example names the others.
    CHECK(runProgram({kSdk, "new", "--from-example", "small-ai", target.string(), "--examples-dir=" + examplesDir().string()}, env).code == 2);
    const Ran unknown = runProgram({kSdk, "new", "--from-example", "no-such", (dir.path() / "x").string(), "--examples-dir=" + examplesDir().string()}, env);
    CHECK(unknown.code == 2);
    CHECK_MESSAGE(contains(unknown.out, "new-hull"), unknown.out);
    // dump with no mod writes the data set as it is, to compare a mod's with.
    GameFolder g("guide_dump");
    const Ran dumped = runProgram({kSdk, "dump", "--data=" + g.root.string(), "--out=" + (dir.path() / "dump").string()}, env);
    CHECK_MESSAGE(dumped.code == 0, dumped.out);
    CHECK(fs::exists(dir.path() / "dump" / "Data" / "Components.txt"));
    // A release finds them beside the program; this build, in the source tree.
    const Ran found = runProgram({kSdk, "new", "--from-example", "new-hull", (dir.path() / "hull").string()}, env);
    CHECK_MESSAGE(found.code == 0, found.out);
    CHECK(fs::exists(dir.path() / "hull" / "assets" / "Pictures" / "RaceGeneric" / "Generic_Mini_WrenCourier.png"));
}

TEST_CASE("sdk test: a mod's rules and scenarios are played, and a rules function that fails fails the test") {
    if (underEmulator()) {
        MESSAGE("under an emulator: skipped");
        return;
    }
    GameFolder g("guide_rules_test");
    TempDir dir("sdk_guide_rules_test");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    // A scenario with an objective every empire meets at once, and an action that records it.
    ModDir ok("guide_rules_ok", "test.guide-rules");
    ok.file("scripts/guide_rules.py", "from opense4 import rules\n\n\n@rules.objective(\"mark\")\n"
                                      "def mark(game, objective, fx):\n    objective.empire.mod_data[\"marked\"] = game.turn\n");
    ok.file("scenarios/start.toml", "title = \"Start\"\n\n[setup]\nseed = 5\nturn_style = \"simultaneous\"\nsystems = 6\n\n"
                                    "[[setup.empire]]\nname = \"One\"\nkind = \"human\"\n\n[[setup.empire]]\nname = \"Two\"\nkind = \"computer\"\n\n"
                                    "[[objective]]\nname = \"first\"\ntext = \"Hold a colony\"\nwhen = { colonies = 1 }\naction = \"mark\"\n");
    const Ran played = runProgram({kSdk, "test", ok.root.string(), "--data=" + g.root.string(), "--turns=2"}, env);
    CHECK_MESSAGE(played.code == 0, played.out);
    CHECK_MESSAGE(contains(played.out, "ok    test.guide-rules's rules in a game of the classic AI: 2 turns, no failures"), played.out);
    CHECK_MESSAGE(contains(played.out, "ok    scenario start: "), played.out);
    CHECK_MESSAGE(contains(played.out, "objectives met: first@0, first@1"), played.out);

    // A hook that raises: the game says which mod, function and why, and the test fails.
    ModDir bad("guide_rules_bad", "test.guide-broken");
    bad.file("scripts/guide_broken.py", "from opense4 import rules\n\n\n@rules.on(\"turn_end\")\ndef broken(game, fx):\n"
                                        "    raise ValueError(\"broken on purpose\")\n");
    const Ran failed = runProgram({kSdk, "test", bad.root.string(), "--data=" + g.root.string(), "--turns=2"}, env);
    CHECK(failed.code == 1);
    CHECK_MESSAGE(contains(failed.out, "FAIL  test.guide-broken's rules in a game of the classic AI: 2 failed calls"), failed.out);
    CHECK_MESSAGE(contains(failed.out, "broken on purpose"), failed.out);
}

TEST_CASE("sdk guide: the API reference and the examples' pictures and sounds are those their tools make") {
    const auto python = cpythonExe();
    if (!python) {
        MESSAGE("python3 (3.10 or newer) is not installed: skipped");
        return;
    }
    for (const char* tool : {"gen_sdk_reference.py", "make_example_assets.py"}) {
        const Ran r = runProgram({*python, "-B", (sourceRoot() / "tools" / tool).string(), "--check"});
        CHECK_MESSAGE(r.code == 0, tool << "\n" << r.out);
    }
}

namespace {

// GitHub's anchor for a heading: lower case, punctuation and symbols dropped, spaces to
// hyphens. Beyond ASCII, the Latin letters stay (é); arrows, dashes and signs go.
std::string anchorOf(std::string_view heading) {
    std::string out;
    for (size_t i = 0; i < heading.size();) {
        const unsigned char c = static_cast<unsigned char>(heading[i]);
        if (c < 0x80) {
            if (std::isalnum(c) || c == '-' || c == '_') out += static_cast<char>(std::tolower(c));
            else if (c == ' ') out += '-';
            ++i;
            continue;
        }
        const size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (len == 2 && i + 1 < heading.size()) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(heading[i + 1]) & 0x3Fu);
            if (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) out += heading.substr(i, 2);
        }
        i += len;
    }
    return out;
}

std::set<std::string> anchorsOf(const fs::path& file) {
    std::set<std::string> found;
    std::map<std::string, int> seen;   // a repeated heading gets -1, -2, ...
    bool code = false;
    for (const std::string& line : lines(slurp(file))) {
        if (line.starts_with("```")) code = !code;
        if (code || !line.starts_with("#")) continue;
        const size_t text = line.find_first_not_of('#');
        if (text == std::string::npos || line[text] != ' ') continue;
        const std::string a = anchorOf(line.substr(text + 1));
        const int times = seen[a]++;
        found.insert(times == 0 ? a : std::format("{}-{}", a, times));
    }
    return found;
}

} // namespace

TEST_CASE("sdk guide: the links of docs/sdk lead to files and headings that are there") {
    const fs::path docs = sourceRoot() / "docs" / "sdk";
    int checked = 0;
    for (const auto& e : fs::recursive_directory_iterator(docs)) {
        if (e.path().extension() != ".md") continue;
        bool code = false;
        int n = 0;
        for (const std::string& line : lines(slurp(e.path()))) {
            ++n;
            if (line.starts_with("```")) code = !code;
            if (code) continue;
            for (size_t at = line.find("]("); at != std::string::npos; at = line.find("](", at + 2)) {
                const size_t end = line.find(')', at + 2);
                if (end == std::string::npos) break;
                std::string target = line.substr(at + 2, end - at - 2);
                if (target.empty() || target.starts_with("http") || target.starts_with("mailto:") || target.find(' ') != std::string::npos) continue;
                std::string anchor;
                if (const size_t hash = target.find('#'); hash != std::string::npos) {
                    anchor = target.substr(hash + 1);
                    target = target.substr(0, hash);
                }
                const fs::path file = target.empty() ? e.path() : (e.path().parent_path() / target).lexically_normal();
                const std::string where = std::format("{}:{}: {}", fs::relative(e.path(), sourceRoot()).string(), n, line.substr(at, end - at + 1));
                ++checked;
                if (!fs::exists(file)) {
                    CHECK_MESSAGE(false, where << ": no such file");
                    continue;
                }
                if (!anchor.empty() && file.extension() == ".md") CHECK_MESSAGE(anchorsOf(file).contains(anchor), where << ": no such heading");
            }
        }
    }
    CHECK(checked > 100);
}
