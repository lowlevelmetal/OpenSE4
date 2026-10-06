// The data set's identity with the AI tables and the mod set, old saves,
// saving a game's mods, and the lobby refusing a player whose mods differ
// (docs/sdk/packages-and-data.md "Identity").

#include "game/serialize.hpp"
#include "game/serialize_io.hpp"
#include "game/setup.hpp"
#include "engine_fixture.hpp"
#include "mod_fixture.hpp"
#include "net_fixture.hpp"

#include <doctest/doctest.h>

#include <cstring>

using namespace opense4;
using namespace opense4::mods;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

game::Rules rulesOf(const GameFolder& g, std::vector<Package> packages = {}) {
    LoadedDataSet d = loadDataSet(g.root, g.data(), modSet(std::move(packages)));
    REQUIRE_MESSAGE(d.ruleset, allErrors(d.diagnostics));
    REQUIRE_MESSAGE(d.diagnostics.errors.empty(), allErrors(d.diagnostics));
    return game::Rules(std::move(*d.ruleset));
}

std::string fingerprint(const std::string& id) { return id.substr(id.rfind('#') + 1); }

} // namespace

TEST_CASE("sdk identity: the data set's identity covers the AI tables") {
    GameFolder a("ida"), b("idb");
    a.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9");
    b.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -8");
    const game::Rules ra = rulesOf(a), rb = rulesOf(b);
    // The identity of format 8 and older could not tell them apart.
    CHECK(fingerprint(game::legacyDataSetIdentity(ra)) == fingerprint(game::legacyDataSetIdentity(rb)));
    CHECK_FALSE(game::sameDataSet(game::dataSetIdentity(ra), game::dataSetIdentity(rb)));
    // Line ends do not count; the same files give the same identity.
    GameFolder c("idc");
    c.writePlain("Ai/Default_AI_Anger.txt", "Written by opense4 tests.\r\n*BEGIN*\r\nRegular Decrease := -9\r\n*END*\r\n");
    CHECK(game::sameDataSet(game::dataSetIdentity(rulesOf(c)), game::dataSetIdentity(ra)));
    // Race files and design names too.
    GameFolder d("idd");
    d.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9");
    d.writePlain("Dsgnname/Extra.txt", "Name\n");
    CHECK_FALSE(game::sameDataSet(game::dataSetIdentity(rulesOf(d)), game::dataSetIdentity(ra)));
    // The plain loader with the game folder gives the same identity as the mods' loader without mods.
    auto plain = ruleset::loadRuleset(a.data());
    REQUIRE(plain.ruleset);
    const game::Rules viaInstall(std::move(*plain.ruleset), a.root);
    CHECK(game::dataSetIdentity(viaInstall) == game::dataSetIdentity(ra));
    CHECK(game::legacyDataSetIdentity(viaInstall) == game::legacyDataSetIdentity(ra));
}

TEST_CASE("sdk identity: game-affecting mods change it, asset mods do not") {
    GameFolder g("idmods");
    const std::string none = game::dataSetIdentity(rulesOf(g));
    const game::Rules withData = rulesOf(g, {openFixtureMod("common-lib")});
    const std::string data = game::dataSetIdentity(withData);
    CHECK_FALSE(game::sameDataSet(none, data));
    CHECK(data.find("+1 mod#") != std::string::npos);
    ModDir pictures("idpictures", "test.pictures");
    pictures.file("assets/Pictures/RaceGeneric/Generic_Mini_X.bmp", "BM");
    const game::Rules withPictures = rulesOf(g, {pictures.open()});
    CHECK(game::sameDataSet(none, game::dataSetIdentity(withPictures)));
    REQUIRE(withPictures.mods().size() == 1);
    CHECK_FALSE(withPictures.mods()[0].affectsGame);
    // Another version of the same mod is another data set.
    ModDir other("idother", "test.common-lib", "1.3.0");
    other.file("data/abilities.toml", "[[abilities.declare]]\nname = \"Hyperspace Anchor\"\ncombine = \"max\"\n");
    CHECK_FALSE(game::sameDataSet(data, game::dataSetIdentity(rulesOf(g, {other.open()}))));
}

TEST_CASE("sdk identity: saves of format 8 compare with the identity they hold") {
    GameFolder g("idold");
    g.write("Ai/Default_AI_Anger.txt", "Regular Decrease := -9");
    const game::Rules rules = rulesOf(g);
    game::SaveInfo info;
    info.gameName = "Old game";
    info.dataSet = game::legacyDataSetIdentity(rules);
    info.formatVersion = 8;
    CHECK(game::sameDataSet(info, rules));
    CHECK_FALSE(game::sameDataSet(info.dataSet, game::dataSetIdentity(rules)));  // what a naive check would say
    info.formatVersion = 9;
    CHECK_FALSE(game::sameDataSet(info, rules));
    info.dataSet = game::dataSetIdentity(rules);
    CHECK(game::sameDataSet(info, rules));
    info.dataSet.clear();
    CHECK(game::sameDataSet(info, rules));

    // A save file written in format 8: its header has no mods and says its format.
    const game::GameState state = test::newEngineGame(7, 2, 6);
    game::SaveInfo old;
    old.gameName = "Format 8";
    old.dataSet = game::legacyDataSetIdentity(rules);
    old.turn = state.turn;
    for (const game::Empire& e : state.empires) old.empires.push_back(e.name);
    std::vector<uint8_t> payload;
    game::serial::write(payload, old, 8);
    game::serial::write(payload, game::serializeState(state), 8);
    std::vector<uint8_t> file = game::wrapEnvelope("OSE4SAVE", payload);
    const uint32_t eight = 8;
    std::memcpy(file.data() + 8, &eight, sizeof eight);
    auto loaded = game::deserializeSave(file);
    REQUIRE_MESSAGE(loaded, (loaded ? std::string{} : loaded.error()));
    CHECK(loaded->second.formatVersion == 8);
    CHECK(loaded->second.mods.empty());
    CHECK(game::sameDataSet(loaded->second, rules));
}

TEST_CASE("sdk identity: a game records its mods, and saves keep them") {
    GameFolder g("idsave");
    ModDir pictures("idsave_pictures", "test.pictures");
    pictures.file("assets/Pictures/RaceGeneric/Generic_Mini_X.bmp", "BM");
    const game::Rules rules = rulesOf(g, {openFixtureMod("common-lib"), pictures.open()});
    REQUIRE(rules.mods().size() == 2);
    game::GameSetup setup;
    setup.seed = 5;
    setup.options.systemCount = 4;
    game::EmpireSetup e;
    e.name = "Testers";
    setup.empires.push_back(e);
    auto state = game::createGame(rules, setup);
    REQUIRE_MESSAGE(state, (state ? std::string{} : state.error()));
    CHECK(state->mods == std::vector<ruleset::ModRecord>(rules.mods().begin(), rules.mods().end()));
    game::SaveInfo info;
    info.dataSet = game::dataSetIdentity(rules);
    const auto bytes = game::serializeSave(*state, info);
    auto back = game::deserializeSave(bytes);
    REQUIRE(back);
    CHECK(back->first.mods == state->mods);
    CHECK(back->second.mods == state->mods);  // the header carries them, for a host to find before loading
    CHECK(back->second.formatVersion == game::kSaveVersion);
    CHECK(game::stateChecksum(back->first) == game::stateChecksum(*state));
    CHECK(game::sameDataSet(back->second, rules));
    // Loading needs the same game-affecting mods; the pictures may be missing.
    CHECK(game::modDifferences(back->first.mods, rules).empty());
    const game::Rules dataOnly = rulesOf(g, {openFixtureMod("common-lib")});
    CHECK(game::modDifferences(back->first.mods, dataOnly).empty());
    const game::Rules none = rulesOf(g);
    const auto missing = game::modDifferences(back->first.mods, none);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == "the saved game uses mod test.common-lib 1.2.0, which you do not have enabled");
}

TEST_CASE("sdk identity: the lobby refuses a player whose game-affecting mods differ") {
    // The host plays with a data mod; its rules record it.
    ruleset::Ruleset data = test::engineRules().data();
    const ruleset::ModRecord lib = openFixtureMod("common-lib").record();
    ruleset::ModRecord pictures{"test.pictures", "1.0.0", std::string(32, 'p'), false};
    data.mods = {lib, pictures};
    const game::Rules hostRules(std::move(data));
    net::HostSession host(hostRules, hostConfig(2));
    REQUIRE(host.start().has_value());
    CHECK(host.lobby().mods == hostRules.data().mods);

    // Without the mod: refused, naming it.
    net::ClientSession without(clientConfig(host, "alice"));
    without.config().dataSet = host.config().dataSet;
    // With it (and without the pictures): welcome.
    net::ClientSession with(clientConfig(host, "bob"));
    with.config().dataSet = host.config().dataSet;
    with.config().mods = {lib};
    Loop loop(host, {&without, &with});
    REQUIRE(without.connect().has_value());
    REQUIRE(with.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(0, net::EventType::Rejected) != nullptr && with.phase() == net::ClientPhase::Lobby; }));
    const std::string why = loop.clientSaw(0, net::EventType::Rejected)->text;
    CHECK_MESSAGE(why.find("Your mods differ from the host's: the host uses mod test.common-lib 1.2.0, which you do not have enabled") !=
                      std::string::npos,
                  why);
    // The client knows it was the mods (the lobby window then offers the Mods window).
    CHECK(without.refusedForMods());
    CHECK_FALSE(with.refusedForMods());
    REQUIRE(loop.until([&] { return !with.lobby().slots.empty(); }));
    CHECK(with.lobby().mods == hostRules.data().mods);
}
