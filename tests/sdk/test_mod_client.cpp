// Mods in the client's windows, without drawing them (client/classic/mods_model.hpp):
// the Mods window's choice of the fixture mods, the setup screens' summary, and
// what a saved game played with other mods needs (docs/sdk/packages-and-data.md
// "Choosing mods in the game").

#include "client/classic/mods_model.hpp"
#include "engine_fixture.hpp"
#include "mod_fixture.hpp"

#include "game/serialize.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::test;
using client::classic::ModsChoice;
namespace fs = std::filesystem;

namespace {

mods::ModLibrary fixtureLibrary() {
    mods::ModLibrary lib = mods::scanModsFolder(fixtureDir() / "mods");
    REQUIRE_MESSAGE(lib.problems.empty(), lib.problems.front());
    return lib;
}

std::vector<std::string> ids(const std::vector<ModsChoice::Row>& rows) {
    std::vector<std::string> out;
    for (const auto& r : rows) out.push_back(r.id);
    return out;
}

bool any(const std::vector<std::string>& lines, std::string_view part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

} // namespace

TEST_CASE("sdk client: the Mods window's choice, its order and its problems") {
    ModsChoice choice(fixtureLibrary(), {"test.needs-lib", "test.gone"});
    // The chosen first (in the player's order, one that is gone among them), then the others by name.
    const auto rows = choice.rows();
    CHECK(ids(rows) == std::vector<std::string>{"test.needs-lib", "test.gone", "test.ai-fixture", "test.ai-tweaks", "classic.classic-names",
                                                "test.common-lib", "test.escort-hull", "test.ui-fixture", "test.picture-pack",
                                                "test.rules-fixture", "test.rules-golden"});
    CHECK(rows[0].enabled);
    CHECK(rows[0].order == 1);
    CHECK(rows[1].package == nullptr);
    CHECK(rows[1].enabled);
    CHECK_FALSE(rows[2].enabled);
    CHECK(choice.enabled() == std::vector<std::string>{"test.needs-lib"});
    CHECK(choice.missing() == std::vector<std::string>{"test.gone"});
    CHECK(choice.changed());   // what is gone is left out

    // Its requirement is missing: the problem names it, and only it.
    CHECK_FALSE(choice.resolve().has_value());
    CHECK(any(choice.problemsOf("test.needs-lib"), "needs mod test.common-lib"));
    CHECK(choice.problemsOf("test.escort-hull").empty());
    CHECK(any(choice.problemsOf("test.gone"), "not in the mods folder"));

    // The library: the load order puts it first, the player's order second.
    choice.toggle("test.common-lib");
    CHECK(choice.enabled() == std::vector<std::string>{"test.needs-lib", "test.common-lib"});
    auto set = choice.resolve();
    REQUIRE(set);
    REQUIRE(set->packages.size() == 2);
    CHECK(set->packages[0].id() == "test.common-lib");
    CHECK(choice.problemsOf("test.needs-lib").empty());
    choice.move("test.common-lib", -1);
    CHECK(choice.enabled() == std::vector<std::string>{"test.common-lib", "test.needs-lib"});
    choice.move("test.common-lib", -1);   // first already
    CHECK(choice.rows()[0].id == "test.common-lib");
    choice.dropMissing("test.gone");
    CHECK(choice.missing().empty());
    CHECK(choice.changed());

    // Back to where it started: no change.
    ModsChoice same(fixtureLibrary(), {"test.escort-hull", "test.picture-pack"});
    CHECK_FALSE(same.changed());
    same.toggle("test.picture-pack");
    CHECK(same.changed());
    same.toggle("test.picture-pack");
    CHECK_FALSE(same.changed());
    same.move("test.picture-pack", -1);
    CHECK(same.changed());
    // Read again (Refresh): compared with the choice in use, not with the one read again.
    ModsChoice refreshed(fixtureLibrary(), {"test.picture-pack", "test.escort-hull"}, std::vector<std::string>{"test.escort-hull", "test.picture-pack"});
    CHECK(refreshed.changed());
    ModsChoice unchanged(fixtureLibrary(), {"test.escort-hull"}, std::vector<std::string>{"test.escort-hull"});
    CHECK_FALSE(unchanged.changed());

    // A message names a mod by its id as a word of its own.
    CHECK(client::classic::mentionsMod("mod test.lib 1.0 is enabled twice", "test.lib"));
    CHECK(client::classic::mentionsMod("needs mod test.lib.", "test.lib"));
    CHECK_FALSE(client::classic::mentionsMod("mod test.lib2 1.0", "test.lib"));
    CHECK_FALSE(client::classic::mentionsMod("mod my.test.lib 1.0", "test.lib"));
}

TEST_CASE("sdk client: the setup screens' summary of the mods") {
    using client::classic::modsSummary;
    const mods::Package hull = openFixtureMod("escort-hull"), pack = openFixtureMod("picture-pack"), lib = openFixtureMod("common-lib");
    CHECK(modsSummary({}) == "none");
    CHECK(modsSummary({pack}) == "Picture Pack (pictures and sounds)");
    CHECK(modsSummary({hull}) == "Escort Hull (changes the game)");
    CHECK(modsSummary({hull, pack}) == "Escort Hull, Picture Pack (1 of them changes the game)");
    CHECK(modsSummary({lib, hull}) == "Common Library, Escort Hull (they change the game)");
    CHECK(pack.tiers == mods::kTierAssets);
    // Mods with scripts: what the original's saved games cannot hold.
    ModDir ai("client_ai", "test.admiral");
    ai.file("ai/admiral.py", "class Admiral: pass\n");
    CHECK(client::classic::scriptedMods({hull, ai.open(), pack}) == std::vector<std::string>{"test.admiral"});
}

TEST_CASE("sdk client: a saved game played with other mods, and whether the mods folder has them") {
    TempDir dir("client_saved");
    const mods::Package lib = openFixtureMod("common-lib"), pack = openFixtureMod("picture-pack");
    game::GameState s = newEngineGame(5, 2, 6);
    s.mods = {lib.record(), pack.record()};
    const fs::path file = dir.path() / "modded.gam";
    REQUIRE(game::saveGame(file, s, game::SaveInfo{}).has_value());

    client::classic::LoadedMods loaded;
    loaded.folders.user = fixtureDir() / "mods";
    // Played here without mods: the library is missing, and the mods folder has it.
    const game::Rules& plain = engineRules();
    auto needs = client::classic::savedGameMods(file, plain, loaded);
    REQUIRE(needs);
    REQUIRE(needs->differences.size() == 1);
    CHECK(needs->differences[0] == "the game uses mod test.common-lib 1.2.0, which you do not have enabled");
    CHECK(needs->unavailable.empty());
    CHECK(needs->ids == std::vector<std::string>{"test.common-lib", "test.picture-pack"});   // the pictures too, when they are there
    // A mods folder without them.
    loaded.folders.user = dir.path() / "empty";
    fs::create_directories(loaded.folders.user);
    needs = client::classic::savedGameMods(file, plain, loaded);
    REQUIRE(needs);
    CHECK(needs->ids.empty());
    CHECK(any(needs->unavailable, "test.common-lib"));
    // With the same game-changing mods (the pictures may differ): nothing to say.
    ruleset::Ruleset data = engineRules().data();
    data.mods = {lib.record()};
    const game::Rules same(std::move(data));
    CHECK_FALSE(client::classic::savedGameMods(file, same, loaded));
    // A file that is no OpenSE4 save: its loading says what is wrong.
    writeText(dir.path() / "other.gam", "not a saved game");
    CHECK_FALSE(client::classic::savedGameMods(dir.path() / "other.gam", plain, loaded));
}

TEST_CASE("sdk client: a mod that comes with OpenSE4 in the Mods window and in a saved game") {
    // The player's folder is empty; the bundled folder (mods/ beside the programs) has the library.
    TempDir dir("client_bundled");
    const fs::path user = dir / "Mods";
    const fs::path bundled = dir / "OpenSE4" / "mods";
    fs::create_directories(user);
    fs::create_directories(bundled);
    fs::copy(fixtureMod("common-lib"), bundled / "common-lib", fs::copy_options::recursive);
    client::classic::LoadedMods loaded;
    loaded.folders = mods::ModFolders{user, bundled};

    // Listed, off until the player switches it on; the settings keep its id like any other.
    ModsChoice choice(mods::scanMods(loaded.folders), {});
    const auto rows = choice.rows();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].package);
    CHECK(rows[0].package->bundled);
    CHECK_FALSE(rows[0].enabled);
    choice.toggle("test.common-lib");
    CHECK(choice.enabled() == std::vector<std::string>{"test.common-lib"});
    CHECK(choice.resolve().has_value());

    // A game played with it finds it again, by id and identity.
    const mods::Package lib = openFixtureMod("common-lib");
    game::GameState s = newEngineGame(5, 2, 6);
    s.mods = {lib.record()};
    const fs::path file = dir.path() / "bundled.gam";
    REQUIRE(game::saveGame(file, s, game::SaveInfo{}).has_value());
    auto needs = client::classic::savedGameMods(file, engineRules(), loaded);
    REQUIRE(needs);
    CHECK(needs->unavailable.empty());
    CHECK(needs->ids == std::vector<std::string>{"test.common-lib"});
    // Without the bundled mods it is not there.
    loaded.folders.bundled.clear();
    needs = client::classic::savedGameMods(file, engineRules(), loaded);
    REQUIRE(needs);
    CHECK(needs->ids.empty());
    CHECK(any(needs->unavailable, "test.common-lib"));
}
