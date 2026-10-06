// The classic AI's own bookkeeping for empires a script player plays
// (docs/sdk/ai-protocol.md §9, `classic_state` in mod.toml): a player that
// overrides nothing, every decision `ai.builtin`'s, plays exactly the game
// the built-in AI plays, in both turn styles; a player that asks for no
// classic state leaves the classic AI's memory as it found it; and the
// classic politics answer gives what the classic AI writes into a computer
// empire directly (its movement options, systems to avoid and claims) as
// commands.

#include "mod_fixture.hpp"
#include "players_fixture.hpp"

#include "game/ai.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "mods/manifest.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <string>
#include <variant>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;

namespace {

constexpr const char* kPlainMod = "test.plain";

// A mod with two players written with OpenSE4's own package that override
// nothing: Plain keeps the classic state (the default), Bare asks for none.
struct PlainMod {
    test::ModDir dir{"classic_state", kPlainMod, "1.0.0",
                     "[[ai.players]]\nname = \"Plain\"\nmodule = \"plain\"\nclass = \"Plain\"\n"
                     "[[ai.players]]\nname = \"Bare\"\nmodule = \"plain\"\nclass = \"Plain\"\nclassic_state = false\n"};
    PlainMod() { dir.file("ai/plain.py", "from opense4 import ai\n\n\nclass Plain(ai.Player):\n    \"\"\"Every decision the classic AI's.\"\"\"\n"); }
    mods::Package open() const { return dir.open(); }
};

Controller plainPlayer(const char* name) {
    Controller c;
    c.kind = Controller::Kind::Script;
    c.mod = kPlainMod;
    c.player = name;
    return c;
}

// The state as the built-in AI would have it: who plays and the players'
// own memory set aside.
GameState asBuiltin(GameState s) {
    for (Empire& e : s.empires) {
        e.controller = Controller{};
        e.script = ScriptPlayerState{};
    }
    return s;
}

std::string differences(const GameState& a, const GameState& b) {
    std::string out;
    for (const std::string& part : differingStateParts(statePartHashes(a), statePartHashes(b))) out += (out.empty() ? "" : ", ") + part;
    return out;
}

void playAlongside(uint64_t seed, bool simultaneous, int turns) {
    const PlainMod mod;
    sdk::PlayerSetup setup;
    setup.mods.push_back(mod.open());
    InstalledPlayers installed(std::move(setup), true);
    const Rules& r = test::engineRules();
    GameState classic = playersGame(seed, simultaneous, {Controller{}, Controller{}, Controller{}, Controller{}});
    GameState played = playersGame(seed, simultaneous, {Controller{}, plainPlayer("Plain"), Controller{}, plainPlayer("Plain")});
    REQUIRE(stateChecksum(classic) == stateChecksum(asBuiltin(played)));
    int battles = 0, colonies = 0;
    for (int t = 0; t < turns; ++t) {
        processTurn(r, classic, {});
        processTurn(r, played, {});
        const GameState normal = asBuiltin(played);
        REQUIRE_MESSAGE(stateChecksum(classic) == stateChecksum(normal),
                        std::format("{} game, seed {}, after turn {}: the states differ in {}", simultaneous ? "simultaneous" : "turn-based", seed, t,
                                    differences(classic, normal)));
        for (EmpireId e : {EmpireId{1u}, EmpireId{3u}}) REQUIRE(played.empire(e).script.failures == 0);
        battles += static_cast<int>(played.combats.size());
    }
    for (const auto& c : played.colonies) colonies += c && (c->owner == EmpireId{1u} || c->owner == EmpireId{3u}) ? 1 : 0;
    // The players did play: their requests are in the journal.
    CHECK_FALSE(journalOf(played, EmpireId{1u}, "economy").empty());
    MESSAGE(std::format("{} turns, {} battles, {} colonies of the players' empires", turns, battles, colonies));
    CHECK(colonies > 2);
}

} // namespace

TEST_CASE("sdk classic state: a player that overrides nothing plays the built-in AI's simultaneous game exactly") {
    playAlongside(51, true, 40);
}

TEST_CASE("sdk classic state: a player that overrides nothing plays the built-in AI's turn-based game exactly") {
    playAlongside(46, false, 40);
}

TEST_CASE("sdk classic state: a player without the classic state leaves the classic AI's memory as it found it") {
    const PlainMod mod;
    sdk::PlayerSetup setup;
    setup.mods.push_back(mod.open());
    InstalledPlayers installed(std::move(setup), true);
    const Rules& r = test::engineRules();
    for (bool simultaneous : {true, false}) {
        CAPTURE(simultaneous);
        GameState s = playersGame(51, simultaneous, {Controller{}, plainPlayer("Bare"), Controller{}, plainPlayer("Plain")});
        const Empire before = s.empire(EmpireId{1u});
        const Empire plainBefore = s.empire(EmpireId{3u});
        for (int t = 0; t < 25; ++t) processTurn(r, s, {});
        const Empire& bare = s.empire(EmpireId{1u});
        const Empire& plain = s.empire(EmpireId{3u});
        CHECK(bare.script.failures == 0);
        // Its state machine, memory, political marks and counters never moved...
        CHECK(bare.aiState == before.aiState);
        CHECK(bare.aiTurnsInState == before.aiTurnsInState);
        CHECK(bare.politicsMark.nextMessage == before.politicsMark.nextMessage);
        for (size_t i = 0; i < bare.relations.size(); ++i) {
            CHECK(bare.relations[i].turnsSinceWar == before.relations[i].turnsSinceWar);
            CHECK(bare.relations[i].treatyAge == before.relations[i].treatyAge);
        }
        // ...while the classic state of the empire Plain plays did.
        CHECK((plain.aiState != plainBefore.aiState || plain.aiTurnsInState > plainBefore.aiTurnsInState));
        bool counted = false;
        for (size_t i = 0; i < plain.relations.size(); ++i) counted = counted || (i != 3 && plain.relations[i].treatyAge > 0);
        CHECK(counted);
        // Both still claim their territory and keep the classic movement
        // options: the classic politics answer gives them as commands.
        CHECK(bare.claimedSystems.size() > before.claimedSystems.size());
        for (EmpireId id : {EmpireId{1u}, EmpireId{3u}})
            for (const Command& c : ai::politicsStartCommands(r, s, id)) CHECK_FALSE(std::holds_alternative<cmd::SetEncounterOptions>(c));
    }
}

TEST_CASE("sdk classic state: the classic politics answer writes the options, systems to avoid and claims with commands") {
    const Rules& r = test::engineRules();
    GameState s = playersGame(51, true, {Controller{}, Controller{}, Controller{}, Controller{}});
    TurnContext ctx{r, s, {}, {}, {}};
    for (Empire& e : s.empires) ai::updateAiState(ctx, e.id);
    for (Empire& e : s.empires) ai::claimTerritory(ctx, e.id);
    // As the classic AI leaves a computer empire, nothing is left to write.
    const EmpireId id{1u};
    CHECK(ai::politicsStartCommands(r, s, id).empty());
    const Empire kept = s.empire(id);
    REQUIRE_FALSE(kept.claimedSystems.empty());
    // Changed by hand (as a player would), and agreed to leave a system.
    Empire& e = s.empire(id);
    const SystemId claimed = kept.claimedSystems.front();
    SystemId stranger;
    for (size_t i = 0; i < s.galaxy.systems.size() && !stranger.valid(); ++i)
        if (!std::binary_search(kept.claimedSystems.begin(), kept.claimedSystems.end(), SystemId{i})) stranger = SystemId{i};
    REQUIRE(stranger.valid());
    e.claimedSystems.erase(e.claimedSystems.begin());
    e.claimedSystems.insert(std::upper_bound(e.claimedSystems.begin(), e.claimedSystems.end(), stranger), stranger);
    e.avoidTaggedMinefields = !kept.avoidTaggedMinefields;
    e.clearOrdersOnEncounter = kept.clearOrdersOnEncounter == EncounterClear::Never ? EncounterClear::Any : EncounterClear::Never;
    e.aiMemory.avoid.push_back(stranger);
    const std::vector<Command> commands = ai::politicsStartCommands(r, s, id);
    REQUIRE(commands.size() == 3);
    const auto* options = std::get_if<cmd::SetEncounterOptions>(&commands[0]);
    REQUIRE(options);
    CHECK(options->avoidTaggedMinefields == kept.avoidTaggedMinefields);
    CHECK(options->clearOrdersOnEncounter == kept.clearOrdersOnEncounter);
    CHECK_FALSE(options->avoidRestrictedSystems);
    // The system agreed to leave: avoided, and no longer claimed; the claim
    // taken away comes back. Applied, they leave the classic AI's lists.
    GameState after = s;
    for (const Command& c : commands) REQUIRE(apply(r, after, id, c).ok);
    CHECK(after.empire(id).avoidTaggedMinefields == kept.avoidTaggedMinefields);
    CHECK(after.empire(id).clearOrdersOnEncounter == kept.clearOrdersOnEncounter);
    CHECK(after.empire(id).systemsToAvoid == std::vector<SystemId>{stranger});
    std::vector<SystemId> claims = kept.claimedSystems;
    std::erase(claims, stranger);
    CHECK(after.empire(id).claimedSystems == claims);
    CHECK(std::binary_search(after.empire(id).claimedSystems.begin(), after.empire(id).claimedSystems.end(), claimed));
    // The politics answer starts with them, and its minister plans on the
    // empire as they leave it.
    const std::vector<Command> answer = ai::planPlayerPolitics(r, s, id);
    REQUIRE(answer.size() >= commands.size());
    for (size_t i = 0; i < commands.size(); ++i) CHECK(commandName(answer[i]) == commandName(commands[i]));
    CHECK(ai::planPlayerPolitics(r, s, id, kAllMinisters & ~ministerBit(Minister::Politics)).size() == ai::planPoliticsOrders(r, s, id, kAllMinisters & ~ministerBit(Minister::Politics)).size());
    // None for a human empire.
    s.empire(id).kind = PlayerKind::Human;
    CHECK(ai::politicsStartCommands(r, s, id).empty());
}

TEST_CASE("sdk classic state: mod.toml says whether a player keeps the classic state") {
    const std::string head = "[mod]\nid = \"test.cs\"\nname = \"CS\"\nversion = \"1.0\"\napi = 1\n";
    auto m = mods::parseManifest(head + "[[ai.players]]\nname = \"On\"\nmodule = \"a\"\nclass = \"A\"\n"
                                        "[[ai.players]]\nname = \"Off\"\nmodule = \"a\"\nclass = \"A\"\nclassic_state = false\n",
                                 "mod.toml");
    REQUIRE(m.has_value());
    REQUIRE(m->aiPlayers.size() == 2);
    CHECK(m->aiPlayers[0].classicState);
    CHECK_FALSE(m->aiPlayers[1].classicState);
    // Written back as it was read: only a player without it says so.
    const std::string written = mods::writeManifest(*m);
    CHECK(written.find("classic_state = false") != std::string::npos);
    auto again = mods::parseManifest(written, "mod.toml");
    REQUIRE(again.has_value());
    CHECK(again->aiPlayers[0].classicState);
    CHECK_FALSE(again->aiPlayers[1].classicState);
    // Anything but true or false is an error.
    auto bad = mods::parseManifest(head + "[[ai.players]]\nname = \"X\"\nmodule = \"a\"\nclass = \"A\"\nclassic_state = \"no\"\n", "mod.toml");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().front().find("'classic_state' should be true or false") != std::string::npos);
}
