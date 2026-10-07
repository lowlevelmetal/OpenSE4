// The modder's guide and its example mods (docs/sdk/guide, mods/examples): every example
// checks and passes its own tests with opense4-sdk on our fixture data (those made for a
// classic data set only show that their patches read), and on the installed game when
// OPENSE4_CLASSIC_DATA is set; new --from-example copies one; the API reference and the
// examples' pictures and sounds are those their tools make; the guide's links lead
// somewhere, and so do those of the release packages' copy (tools/stage_sdk_docs.py);
// and the docs' TOML and JSON examples read as the files they show (the Python examples
// are checked in test_sdk_python.cpp).

#include "bots_fixture.hpp"
#include "mod_fixture.hpp"

#include "mods/manifest.hpp"
#include "mods/mod_set.hpp"
#include "mods/package.hpp"
#include "mods/patch.hpp"
#include "net/types.hpp"
#include "ruleset/ruleset.hpp"
#include "script/json.hpp"
#include "sdk/scenario.hpp"
#include "sdk/ui.hpp"

#include <doctest/doctest.h>
#include <toml++/toml.hpp>

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
    CHECK_MESSAGE(slurp(dir.path() / "dump" / "Data" / "Components.txt").find("the data set with no mods.") != std::string::npos, dumped.out);
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

// Which links linksOf finds: those to paths (relative), or web addresses.
enum class Links { Relative, Web };

// The Markdown links of a file outside code (fenced blocks and `inline code`), with
// their lines: (line, target as written). tools/stage_sdk_docs.py finds them the same way.
std::vector<std::pair<int, std::string>> linksOf(const fs::path& file, Links kind = Links::Relative) {
    std::vector<std::pair<int, std::string>> out;
    bool code = false;
    int n = 0;
    for (const std::string& raw : lines(slurp(file))) {
        ++n;
        const size_t first = raw.find_first_not_of(' ');
        if (first != std::string::npos && raw.compare(first, 3, "```") == 0) code = !code;
        if (code) continue;
        std::string line;
        bool inline_code = false;
        for (char c : raw) {
            if (c == '`') inline_code = !inline_code;
            else if (!inline_code) line += c;
        }
        for (size_t at = line.find("]("); at != std::string::npos; at = line.find("](", at + 2)) {
            const size_t end = line.find(')', at + 2);
            if (end == std::string::npos) break;
            const std::string target = line.substr(at + 2, end - at - 2);
            if (target.empty() || target.starts_with("mailto:") || target.find(' ') != std::string::npos) continue;
            if (target.starts_with("http") != (kind == Links::Web)) continue;
            out.emplace_back(n, target);
        }
    }
    return out;
}

} // namespace

TEST_CASE("sdk guide: the docs' links lead to files and headings that are there, and the index reaches every page") {
    // docs/sdk, and the pages that point into it: the README, docs/*.md and the mods' READMEs.
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(sourceRoot() / "docs" / "sdk"))
        if (e.path().extension() == ".md") files.push_back(e.path());
    for (const auto& e : fs::directory_iterator(sourceRoot() / "docs"))
        if (e.path().extension() == ".md") files.push_back(e.path());
    for (const auto& e : fs::recursive_directory_iterator(sourceRoot() / "mods"))
        if (e.path().extension() == ".md") files.push_back(e.path());
    files.push_back(sourceRoot() / "README.md");
    int checked = 0;
    for (const fs::path& f : files) {
        for (const auto& [n, link] : linksOf(f)) {
            std::string target = link, anchor;
            if (const size_t hash = target.find('#'); hash != std::string::npos) {
                anchor = target.substr(hash + 1);
                target = target.substr(0, hash);
            }
            const fs::path file = target.empty() ? f : (f.parent_path() / target).lexically_normal();
            const std::string where = std::format("{}:{}: ({})", fs::relative(f, sourceRoot()).generic_string(), n, link);
            ++checked;
            if (!fs::exists(file)) {
                CHECK_MESSAGE(false, where << ": no such file");
                continue;
            }
            if (!anchor.empty() && file.extension() == ".md") CHECK_MESSAGE(anchorsOf(file).contains(anchor), where << ": no such heading");
        }
    }
    CHECK(checked > 300);

    // Every page of docs/sdk can be reached from its index, docs/sdk/README.md.
    const fs::path docs = (sourceRoot() / "docs" / "sdk").lexically_normal();
    std::set<fs::path> reached;
    std::vector<fs::path> todo{docs / "README.md"};
    while (!todo.empty()) {
        const fs::path page = todo.back();
        todo.pop_back();
        if (!reached.insert(page).second) continue;
        for (const auto& [n, link] : linksOf(page)) {
            const fs::path target = (page.parent_path() / link.substr(0, link.find('#'))).lexically_normal();
            if (target.extension() == ".md" && target.string().starts_with(docs.string()) && fs::exists(target)) todo.push_back(target);
        }
    }
    for (const auto& e : fs::recursive_directory_iterator(docs))
        if (e.path().extension() == ".md")
            CHECK_MESSAGE(reached.contains(e.path().lexically_normal()), fs::relative(e.path(), sourceRoot()).generic_string() << " is not linked from docs/sdk/README.md or a page it leads to");
}

TEST_CASE("sdk guide: a package's copy of the docs links to its own files and headings, and the rest to GitHub at the version's tag") {
    const auto python = cpythonExe();
    if (!python) {
        MESSAGE("python3 (3.10 or newer) is not installed: skipped");
        return;
    }
    // A package's folder with the README, the licence and the mods that come with
    // OpenSE4 in it, as tools/package_release.sh stages them before the SDK's files.
    TempDir dir("sdk_guide_package");
    const fs::path stage = dir.path() / "OpenSE4";
    fs::create_directories(stage / "mods");
    for (const char* file : {"README.md", "LICENSE"}) fs::copy_file(sourceRoot() / file, stage / file);
    const std::vector<std::string> bundled = mods::readBundledList(sourceRoot() / "mods" / std::string(mods::kBundledListFile));
    REQUIRE_FALSE(bundled.empty());
    for (const std::string& name : bundled) fs::copy(sourceRoot() / "mods" / name, stage / "mods" / name, fs::copy_options::recursive);
    const Ran staged = runProgram({*python, "-B", (sourceRoot() / "tools" / "stage_sdk_docs.py").string(), stage.string()});
    REQUIRE_MESSAGE(staged.code == 0, staged.out);

    // Every page of docs/sdk is there, with the SDK's design they cite, and every example.
    const fs::path docs = sourceRoot() / "docs" / "sdk";
    for (const auto& e : fs::recursive_directory_iterator(docs))
        if (e.is_regular_file()) CHECK_MESSAGE(fs::exists(stage / "sdk" / "docs" / fs::relative(e.path(), docs)), e.path().string());
    CHECK(fs::exists(stage / "sdk" / "docs" / "MODDING_SDK.md"));
    CHECK(fs::exists(stage / "sdk" / "examples" / "small-ai" / "mod.toml"));

    // The links of the staged pages: those to paths lead to files and headings of the
    // package, those to the repository to files (raw/ for pictures), folders and headings
    // of this source tree at its version's tag. None is lost: the sources have as many.
    const std::string repository = "https://github.com/lowlevelmetal/OpenSE4/";
    const std::string tag = "v" + std::string(net::appVersion().substr(std::string_view("OpenSE4 ").size())) + "/";
    size_t inPackage = 0, onGitHub = 0, links = 0;
    for (const auto& e : fs::recursive_directory_iterator(stage)) {
        if (e.path().extension() != ".md") continue;
        const fs::path& f = e.path();
        for (const auto& [n, link] : linksOf(f)) {
            std::string target = link, anchor;
            if (const size_t hash = target.find('#'); hash != std::string::npos) {
                anchor = target.substr(hash + 1);
                target = target.substr(0, hash);
            }
            const fs::path file = target.empty() ? f : (f.parent_path() / target).lexically_normal();
            const std::string where = std::format("{}:{}: ({})", fs::relative(f, stage).generic_string(), n, link);
            ++inPackage;
            const fs::path inside = file.lexically_relative(stage.lexically_normal());
            if (inside.empty() || *inside.begin() == ".." || !fs::exists(file)) {
                CHECK_MESSAGE(false, where << ": not in the package");
                continue;
            }
            if (!anchor.empty() && file.extension() == ".md") CHECK_MESSAGE(anchorsOf(file).contains(anchor), where << ": no such heading");
        }
        for (const auto& [n, link] : linksOf(f, Links::Web)) {
            ++links;
            if (!link.starts_with(repository)) continue;
            std::string rest = link.substr(repository.size()), anchor;
            if (const size_t hash = rest.find('#'); hash != std::string::npos) {
                anchor = rest.substr(hash + 1);
                rest = rest.substr(0, hash);
            }
            // A file (blob/, raw/ for pictures) or a folder (tree/); not another page of
            // the repository's, such as its releases or the README's CI badge.
            const size_t slash = rest.find('/');
            const std::string kind = rest.substr(0, slash);
            if (slash == std::string::npos || (kind != "blob" && kind != "tree" && kind != "raw")) continue;
            ++onGitHub;
            const std::string where = std::format("{}:{}: ({})", fs::relative(f, stage).generic_string(), n, link);
            rest = rest.substr(slash + 1);
            if (!rest.starts_with(tag)) {
                CHECK_MESSAGE(false, where << ": not at " << tag);
                continue;
            }
            const fs::path file = sourceRoot() / rest.substr(tag.size());
            if (!fs::exists(file)) {
                CHECK_MESSAGE(false, where << ": no such file in the source tree");
                continue;
            }
            const bool folder = kind == "tree";
            CHECK_MESSAGE(fs::is_directory(file) == folder, where << std::string(folder ? ": a file, not a folder" : ": a folder, not a file"));
            const bool picture = std::set<std::string>{".png", ".webp", ".jpg", ".jpeg", ".gif", ".svg"}.contains(file.extension().string());
            CHECK_MESSAGE(picture == (kind == "raw"), where << std::string(picture ? ": a picture shows from raw/" : ": raw/ is for pictures"));
            if (!anchor.empty() && file.extension() == ".md") CHECK_MESSAGE(anchorsOf(file).contains(anchor), where << ": no such heading");
        }
    }
    links += inPackage;
    CHECK(inPackage > 300);
    CHECK(onGitHub > 50);

    std::vector<fs::path> sources{sourceRoot() / "README.md", sourceRoot() / "docs" / "MODDING_SDK.md"};
    std::vector<fs::path> folders{docs, sourceRoot() / "mods" / "examples"};
    for (const std::string& name : bundled) folders.push_back(sourceRoot() / "mods" / name);
    for (const fs::path& folder : folders)
        for (const auto& e : fs::recursive_directory_iterator(folder))
            if (e.path().extension() == ".md") sources.push_back(e.path());
    size_t sourceLinks = 0;
    for (const fs::path& f : sources) sourceLinks += linksOf(f).size() + linksOf(f, Links::Web).size();
    CHECK(links == sourceLinks);
}

namespace {

// A fenced block of a Markdown file: its first line, language, the words after the
// language (`fragment`: an excerpt, not checked) and its text.
struct CodeBlock {
    fs::path file;
    int line = 0;
    std::string language;
    std::vector<std::string> words;
    std::string text;
};

std::vector<CodeBlock> codeBlocks(const fs::path& file) {
    std::vector<CodeBlock> out;
    const std::vector<std::string> all = lines(slurp(file));
    for (size_t i = 0; i < all.size(); ++i) {
        const size_t indent = all[i].find_first_not_of(' ');
        if (indent == std::string::npos || all[i].compare(indent, 3, "```") != 0) continue;
        CodeBlock b{file, static_cast<int>(i + 1), {}, {}, {}};
        std::istringstream info(all[i].substr(indent + 3));
        info >> b.language;
        for (std::string w; info >> w;) b.words.push_back(w);
        for (++i; i < all.size(); ++i) {
            const std::string& l = all[i];
            const size_t at = l.find_first_not_of(' ');
            if (at != std::string::npos && l.compare(at, 3, "```") == 0) break;
            b.text += (l.size() >= indent && l.find_first_not_of(' ') >= indent ? l.substr(indent) : l) + "\n";
        }
        out.push_back(std::move(b));
    }
    return out;
}

// The SDK's documentation: docs/sdk, docs/MODDING_SDK.md and the mods' READMEs.
std::vector<fs::path> sdkMarkdown() {
    std::vector<fs::path> files{sourceRoot() / "docs" / "MODDING_SDK.md"};
    for (const fs::path& top : {sourceRoot() / "docs" / "sdk", sourceRoot() / "mods"})
        for (const auto& e : fs::recursive_directory_iterator(top))
            if (e.path().extension() == ".md") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

// What a TOML example is, from its top-level keys: an interface file (ui/*.toml: arrays
// of [[order]], [[panel]]...), a text file (text/<language>.toml: dotted keys), a manifest
// (mod.toml), a scenario or a data patch.
enum class TomlKind { Ui, Text, Manifest, Scenario, Patch };

TomlKind tomlKind(const toml::table& t) {
    for (const char* key : {"order", "panel", "column", "empire_page", "button"})
        if (t.get_as<toml::array>(key)) return TomlKind::Ui;
    for (const auto& [key, value] : t) {
        if (value.is_string() && key.str().find('.') != std::string_view::npos) return TomlKind::Text;
        if (value.is_table() && (key == "order" || key == "option" || key == "panel" || key == "column" || key == "page" || key == "scenario"))
            return TomlKind::Text;
    }
    const auto* ai = t.get_as<toml::table>("ai");
    if (t.contains("mod") || t.contains("requires") || t.contains("load") || t.contains("rules") || (ai && ai->contains("players")))
        return TomlKind::Manifest;
    if (t.contains("title") || t.contains("setup") || t.contains("objective")) return TomlKind::Scenario;
    if (t.contains("order") || t.contains("panel") || t.contains("column") || t.contains("empire_page")) return TomlKind::Ui;
    return TomlKind::Patch;
}

// The problems of a TOML example, read as the file it shows by the parser of that file.
std::vector<std::string> tomlProblems(const std::string& text, const std::string& where) {
    toml::table t;
    try {
        t = toml::parse(text, std::string_view(where));
    } catch (const toml::parse_error& e) {
        return {std::format("{}: not TOML: {}", where, e.description())};
    }
    switch (tomlKind(t)) {
    case TomlKind::Manifest: {
        // Parts of a manifest get a [mod] table of their own.
        const std::string whole = t.contains("mod") ? text : "[mod]\nid = \"doc.example\"\nname = \"Example\"\nversion = \"1.0.0\"\napi = 1\n\n" + text;
        auto m = mods::parseManifest(whole, where);
        return m ? std::vector<std::string>{} : m.error();
    }
    case TomlKind::Scenario: {
        const std::string whole = t.contains("title") ? text : "title = \"Example\"\n\n" + text;
        auto s = sdk::parseScenario(whole, where, "doc.example", "example");
        return s ? std::vector<std::string>{} : s.error();
    }
    case TomlKind::Ui: {
        auto u = sdk::parseUiFile(text, where, "doc.example");
        return u ? std::vector<std::string>{} : u.error();
    }
    case TomlKind::Text: {
        auto u = sdk::parseUiTexts(text, where);
        return u ? std::vector<std::string>{} : u.error();
    }
    case TomlKind::Patch: {
        std::vector<std::string> errors;
        const mods::Origin origin{"doc.example", where, false};
        if (auto root = mods::parsePatchToml(text, origin, errors)) {
            mods::PatchSet set;
            mods::parsePatch(*root, origin, set, errors);
        }
        return errors;
    }
    }
    return {};
}

} // namespace

TEST_CASE("sdk guide: the TOML and JSON examples of the docs read as the files they show") {
    int toml = 0, json = 0;
    for (const fs::path& file : sdkMarkdown()) {
        for (const CodeBlock& b : codeBlocks(file)) {
            const std::string where = std::format("{}:{}", fs::relative(file, sourceRoot()).generic_string(), b.line);
            if (b.language != "toml" && b.language != "json") continue;
            const bool fragment = b.words.size() == 1 && b.words[0] == "fragment";
            CHECK_MESSAGE((b.words.empty() || fragment), where << ": unknown words after the language (none, or fragment)");
            if (fragment) continue;
            if (b.language == "toml") {
                ++toml;
                for (const std::string& p : tomlProblems(b.text, where)) CHECK_MESSAGE(false, where << ": " << p);
                continue;
            }
            // JSON: one value, or one per line (a conversation, a list of commands).
            ++json;
            if (script::parseJson(b.text)) continue;
            int n = 0;
            for (const std::string& l : lines(b.text)) {
                ++n;
                if (l.find_first_not_of(" \t") == std::string::npos) continue;
                auto v = script::parseJson(l);
                CHECK_MESSAGE(v.has_value(), where << ": line " << n << " of the block is not JSON: " << (v ? std::string{} : v.error().describe()));
            }
        }
    }
    CHECK(toml > 80);
    CHECK(json > 40);
}
