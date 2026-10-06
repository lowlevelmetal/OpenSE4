// Computer players in the engine (docs/sdk/ai-protocol.md, src/sdk/players.hpp):
// controllers, sessions, every decision point and the request it gets,
// responses, failures and their fallbacks, budgets, memory, notes and the
// services. The players are the fixture mod's (tests/fixtures/mods/ai-fixture),
// written straight to the protocol, run by a stand-in of the opense4 package.

#include "players_fixture.hpp"

#include "combat_fixture.hpp"
#include "movement_fixture.hpp"

#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/economy.hpp"
#include "game/players.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "mods/manifest.hpp"
#include "sdk/codec.hpp"
#include "sdk/view.hpp"
#include "sdk/players.hpp"
#include "sdk/worker.hpp"
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;
using script::ValueList;

namespace {

std::shared_ptr<const sdk::PlayerSetup> fixtureSetup() {
    static const std::shared_ptr<const sdk::PlayerSetup> setup = [] {
        auto p = std::make_shared<sdk::PlayerSetup>();
        p->package = standInPackage();
        p->mods.push_back(aiFixtureMod());
        return std::shared_ptr<const sdk::PlayerSetup>(p);
    }();
    return setup;
}

// A session of the fixture's players on `s`, for calling the engine's own
// steps (movement, colonization, a battle) as a turn would.
struct Direct {
    std::unique_ptr<Players> players;
    TurnContext ctx;
    Direct(const Rules& r, GameState& s) : players(sdk::makeSession(r, s, fixtureSetup())), ctx{r, s, {}, {}, {}} { ctx.players = players.get(); }
    void end() { players->endSession(ctx); }
};

// The calls a Probe's memory recorded, by name.
std::vector<Value> callsOf(const GameState& s, EmpireId e, std::string_view call = {}) {
    std::vector<Value> out;
    const Value m = memoryOf(s, e);
    const Value* calls = m.find("calls");
    if (!calls || !calls->isList()) return out;
    for (const Value& c : calls->asList())
        if (call.empty() || c.find("call")->asString() == call) out.push_back(c);
    return out;
}

std::vector<std::string> callNames(const GameState& s, EmpireId e) {
    std::vector<std::string> out;
    for (const Value& c : callsOf(s, e)) out.push_back(c.find("call")->asString());
    return out;
}

std::vector<std::string> keysOf(const Value& list) {
    std::vector<std::string> out;
    for (const Value& v : list.asList()) out.push_back(v.asString());
    return out;
}

// A tech area the empire may queue now (its view's `researchable`).
int researchableArea(const Rules& r, const GameState& s, EmpireId e) {
    const Value view = sdk::buildView(r, s, e);
    const Value& areas = *view.find("my")->find("research")->find("researchable");
    REQUIRE(areas.size() > 0);
    return static_cast<int>(areas.asList().front().asInt());
}

// Makes an empire a computer player played by the fixture's Probe, with a script.
void probe(GameState& s, EmpireId e, std::string_view script = "{}") {
    s.empire(e).kind = PlayerKind::Computer;
    s.empire(e).controller = scriptPlayer("Probe");
    setMemory(s, e, std::format(R"({{"script": {}}})", script));
}

Order mk(OrderKind k, Location l = {}, ObjectId obj = {}) {
    Order o;
    o.kind = k;
    o.location = l;
    o.object = obj;
    return o;
}

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

void fuel(mvtest::World& w, VehicleId id) {
    Design d = w.s.design(w.v(id).design);
    d.entries.push_back({test::componentIndex(w.rules(), "Mv Fuel Cell"), -1});
    const DesignId fuelled = addDesign(w.s, std::move(d));
    Vehicle& v = w.v(id);
    v.design = fuelled;
    v.damage.resize(w.s.design(fuelled).entries.size(), 0);
    v.supply = 1'000'000;
}

// The state without what only script players keep (their memory and notes),
// to compare a game a player played with the same game played otherwise.
uint64_t checksumWithoutPlayers(GameState s) {
    for (Empire& e : s.empires) {
        e.script = {};
        e.controller = {};
        e.kind = PlayerKind::Human;
    }
    return stateChecksum(s);
}

} // namespace

// ---- Controllers and manifests --------------------------------------------------------------------

TEST_CASE("sdk players: controllers are written and read as setup files and the command line give them") {
    CHECK(controllerText(Controller{}) == "builtin");
    CHECK(controllerText(scriptPlayer("Steady")) == "test.ai-fixture:Steady");
    CHECK(controllerText(externalPlayer(3)) == "external:3");
    CHECK(parseController("builtin") == Controller{});
    CHECK(parseController("test.ai-fixture:Steady") == scriptPlayer("Steady"));
    CHECK(parseController("external:3") == externalPlayer(3));
    CHECK_FALSE(parseController("nobody").has_value());
    CHECK_FALSE(parseController(":Steady").has_value());
    CHECK_FALSE(parseController("mod:").has_value());
    CHECK_FALSE(parseController("external:x").has_value());
    CHECK_FALSE(parseController("a:b:c").has_value());
}

TEST_CASE("sdk players: a mod declares its players in [[ai.players]], and mistakes are named") {
    const mods::Package fixture = aiFixtureMod();
    REQUIRE(fixture.manifest.aiPlayers.size() == 4);
    CHECK(fixture.manifest.aiPlayers[0].name == "Steady");
    CHECK(fixture.manifest.aiPlayers[0].module == "fixture_player");
    CHECK(fixture.manifest.aiPlayers[0].className == "Steady");
    CHECK(fixture.manifest.aiPlayer("Probe") != nullptr);
    CHECK(fixture.manifest.aiPlayer("probe") == nullptr);
    CHECK((fixture.tiers & mods::kTierAi) != 0);
    // Written back, read again.
    auto again = mods::parseManifest(mods::writeManifest(fixture.manifest), "mod.toml");
    REQUIRE(again.has_value());
    REQUIRE(again->aiPlayers.size() == 4);
    CHECK(again->aiPlayers[2].description == fixture.manifest.aiPlayers[2].description);

    const std::string head = "[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 1\n";
    auto errorsOf = [&](std::string_view players) {
        auto m = mods::parseManifest(head + std::string(players), "mod.toml");
        std::string all;
        if (!m)
            for (const std::string& e : m.error()) all += e + "\n";
        return all;
    };
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a.b_c\"\nclass = \"A\"\n").empty());
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a\"\nclass = \"A\"\n[[ai.players]]\nname = \"A\"\nmodule = \"b\"\nclass = \"B\"\n")
              .find("two computer players are named 'A'") != std::string::npos);
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a-b\"\nclass = \"A\"\n").find("Python names") != std::string::npos);
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a\"\nclass = \"1A\"\n").find("not a Python name") != std::string::npos);
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a\"\n").find("needs the player's class") != std::string::npos);
    CHECK(errorsOf("[[ai.players]]\nname = \"A:B\"\nmodule = \"a\"\nclass = \"A\"\n").find("may not hold ':'") != std::string::npos);
    CHECK(errorsOf("[[ai.players]]\nname = \"A\"\nmodule = \"a\"\nclass = \"A\"\nmodel = \"x\"\n").find("unknown key 'model'") != std::string::npos);
    CHECK(errorsOf("[ai]\nplayer = 3\n").find("unknown key 'player' in [ai]") != std::string::npos);
}

TEST_CASE("sdk players: opense4-sdk check finds a player's missing module or class, and Python it cannot read") {
    test::TempDir dir("sdk_player_check");
    auto write = [&](const std::string& rel, std::string_view text) {
        std::filesystem::create_directories((dir.path() / rel).parent_path());
        std::ofstream(dir.path() / rel, std::ios::binary) << text;
    };
    write("mod.toml", "[mod]\nid = \"test.checked\"\nname = \"Checked\"\nversion = \"1.0\"\napi = 1\n"
                      "[[ai.players]]\nname = \"Good\"\nmodule = \"good\"\nclass = \"Good\"\n"
                      "[[ai.players]]\nname = \"Packaged\"\nmodule = \"pack.inner\"\nclass = \"Inner\"\n"
                      "[[ai.players]]\nname = \"Lost\"\nmodule = \"nowhere\"\nclass = \"Lost\"\n"
                      "[[ai.players]]\nname = \"Classless\"\nmodule = \"good\"\nclass = \"Other\"\n");
    write("ai/good.py", "import helpers\n\nclass Good:\n    pass\n");
    write("ai/helpers.py", "X = 1\n");
    write("ai/pack/__init__.py", "");
    write("ai/pack/inner.py", "class Inner(object):\n    pass\n");
    write("ai/broken.py", "def f(:\n    pass\n");
    write("ai/bad-name.py", "x = 1\n");
    auto p = mods::openPackage(dir.path());
    REQUIRE(p.has_value());
    const sdk::PlayerCheck check = sdk::checkModPlayers(*p);
    std::string errors, warnings;
    for (const std::string& e : check.errors) errors += e + "\n";
    for (const std::string& w : check.warnings) warnings += w + "\n";
    INFO(errors << warnings);
    CHECK(check.errors.size() == 3);
    CHECK(errors.find("'Lost' is in module nowhere") != std::string::npos);
    CHECK(errors.find("'Classless' is class Other, but ai/good.py defines no class Other") != std::string::npos);
    CHECK(errors.find("ai/broken.py") != std::string::npos);
    CHECK(warnings.find("ai/bad-name.py: not a Python module name") != std::string::npos);
    // The fixture mod is clean.
    const sdk::PlayerCheck fixture = sdk::checkModPlayers(aiFixtureMod());
    CHECK(fixture.errors.empty());
    CHECK(fixture.warnings.empty());
}

// ---- Sessions -------------------------------------------------------------------------------------------

TEST_CASE("sdk players: only a game with a script or external computer player gets a session") {
    InstalledPlayers installed;
    GameState s = playersGame(11, true, {Controller{}, Controller{}});
    CHECK_FALSE(makePlayers(test::engineRules(), s));
    s.empire(EmpireId{1u}).controller = scriptPlayer("Idle");
    CHECK(makePlayers(test::engineRules(), s));
    // A human empire's controller is not used, nor a dead empire's.
    s.empire(EmpireId{1u}).kind = PlayerKind::Human;
    CHECK_FALSE(makePlayers(test::engineRules(), s));
    s.empire(EmpireId{1u}).kind = PlayerKind::Computer;
    s.empire(EmpireId{1u}).alive = false;
    CHECK_FALSE(makePlayers(test::engineRules(), s));
    // Without the SDK installed, no session: the built-in AI plays every empire.
    s.empire(EmpireId{1u}).alive = true;
    sdk::uninstallPlayers();
    CHECK_FALSE(makePlayers(test::engineRules(), s));
}

TEST_CASE("sdk players: players written with the opense4 package play in the engine, the same way every time") {
    REQUIRE_FALSE(sdk::packageFiles().empty());
    InstalledPlayers installed({}, true);
    const Rules& r = test::engineRules();
    for (const bool simultaneous : {true, false}) {
        INFO("simultaneous " << simultaneous);
        uint64_t first = 0;
        for (int run = 0; run < 2; ++run) {
            GameState s = playersGame(51, simultaneous, {scriptPlayer("Captain"), Controller{}, scriptPlayer("Captain"), Controller{}});
            for (int t = 0; t < 20; ++t) processTurn(r, s, {});
            int colonies = 0;
            for (EmpireId e : {EmpireId{0u}, EmpireId{2u}}) {
                INFO(s.empire(e).script.memory);
                CHECK(s.empire(e).script.failures == 0);
                const Value m = memoryOf(s, e);
                CHECK(intIn(m, "plans") == 20);
                CHECK(intIn(m, "sessions") == 20);
                if (const Value* c = m.find("colonies")) colonies += static_cast<int>(c->asInt());
                CHECK_FALSE(s.empire(e).aiNotes.empty());
            }
            CHECK(colonies > 0);
            if (run == 0) first = stateChecksum(s);
            else CHECK(stateChecksum(s) == first);
        }
    }
}

TEST_CASE("sdk players: two script empires play simultaneous turns, and their memory counts them") {
    InstalledPlayers installed;
    GameState s = playersGame(11, true, {scriptPlayer("Steady"), Controller{}, scriptPlayer("Steady"), Controller{}});
    for (int t = 0; t < 5; ++t) processTurn(test::engineRules(), s, {});
    for (EmpireId e : {EmpireId{0u}, EmpireId{2u}}) {
        const Value m = memoryOf(s, e);
        INFO(s.empire(e).script.memory);
        CHECK(intIn(m, "sessions") == 5);
        CHECK(intIn(m, "plans") == 5);
        CHECK(s.empire(e).script.failures == 0);
        // Its notes of the last two turns, kept in memory only; a later note on
        // the same vehicle replaced the earlier one.
        const auto& notes = s.empire(e).aiNotes;
        REQUIRE_FALSE(notes.empty());
        CHECK(std::any_of(notes.begin(), notes.end(), [](const PlayerNote& n) { return n.turn == 4 && n.text == "turn 4" && n.kind == "vehicle"; }));
        CHECK(std::all_of(notes.begin(), notes.end(), [](const PlayerNote& n) { return n.turn >= 3 && n.text == std::format("turn {}", n.turn); }));
        for (const PlayerNote& n : notes)
            CHECK(std::count_if(notes.begin(), notes.end(), [&](const PlayerNote& o) { return o.kind == n.kind && o.object == n.object; }) == 1);
    }
    CHECK(s.empire(EmpireId{1u}).script.memory.empty());
    // Notes are never saved; memory is.
    const auto loaded = deserializeState(serializeState(s));
    REQUIRE(loaded.has_value());
    CHECK(loaded->empire(EmpireId{0u}).aiNotes.empty());
    CHECK(loaded->empire(EmpireId{0u}).script.memory == s.empire(EmpireId{0u}).script.memory);
}

// ---- The planning calls -----------------------------------------------------------------------------

TEST_CASE("sdk players: a simultaneous turn asks politics, orders and economy with the view, then end_session") {
    InstalledPlayers installed;
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    probe(s, EmpireId{1u}, R"({"politics": {"keep_view": true}})");
    processTurn(test::engineRules(), s, {});
    const EmpireId p{1u};
    CHECK(callNames(s, p) == std::vector<std::string>{"politics", "orders", "economy", "end_session"});
    const std::vector<Value> calls = callsOf(s, p);
    // The first request of the session makes the player and gives it its memory.
    const Value* player = calls[0].find("player");
    REQUIRE(player);
    CHECK(player->find("mod")->asString() == kFixtureMod);
    CHECK(player->find("name")->asString() == "Probe");
    CHECK(player->find("module")->asString() == "fixture_player");
    CHECK(player->find("class")->asString() == "Probe");
    CHECK(calls[0].find("memory")->asBool());
    for (size_t i = 1; i < calls.size(); ++i) CHECK_FALSE(calls[i].find("player"));
    // Planning calls get the empire's own view; the others none.
    for (size_t i = 0; i < 3; ++i) CHECK(calls[i].find("view")->asBool());
    CHECK_FALSE(calls[3].find("view")->asBool());
    CHECK(calls[0].find("view_empire")->asInt() == 1);
    CHECK_FALSE(calls[0].find("view_whole")->asBool());
    CHECK(keysOf(*calls[0].find("view_keys")) == std::vector<std::string>{"api", "battles", "colonies", "designs", "empire", "empires", "fleets", "game",
                                                                           "log", "messages", "my", "objects", "systems", "vehicles", "whole"});
    // Each request has a seed of its own; all are turn 0.
    std::vector<int64_t> seeds;
    for (const Value& c : calls) {
        CHECK(c.find("turn")->asInt() == 0);
        CHECK(c.find("seed")->asInt() >= 0);
        seeds.push_back(c.find("seed")->asInt());
    }
    std::sort(seeds.begin(), seeds.end());
    CHECK(std::adjacent_find(seeds.begin(), seeds.end()) == seeds.end());
    // The journal holds every answer, with the turn and the call.
    const auto journal = journalOf(s, p);
    REQUIRE(journal.size() == 4);
    CHECK(journal[0].call == "politics");
    CHECK(journal[3].call == "end_session");
    // The next turn is a new session: the player is made again.
    processTurn(test::engineRules(), s, {});
    CHECK(callsOf(s, p).size() == 8);
    CHECK(callsOf(s, p)[4].find("player"));
    CHECK(callsOf(s, p)[4].find("turn")->asInt() == 1);
}

TEST_CASE("sdk players: the game option lets computer players see the whole game") {
    InstalledPlayers installed;
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    s.options.aiSeesEverything = true;
    probe(s, EmpireId{0u}, R"({"politics": {"keep_view": true}})");
    processTurn(test::engineRules(), s, {});
    CHECK(callsOf(s, EmpireId{0u}, "politics").front().find("view_whole")->asBool());
}

TEST_CASE("sdk players: a turn-based player's turn asks politics and orders at its start, economy at its end") {
    InstalledPlayers installed;
    GameState s = playersGame(5, false, {Controller{}, Controller{}, Controller{}});
    probe(s, EmpireId{1u});
    resumeTurnBased(test::engineRules(), s);   // one game turn of computer players
    CHECK(callNames(s, EmpireId{1u}) == std::vector<std::string>{"politics", "orders", "economy", "end_session"});
    CHECK(s.turn == 1);
}

TEST_CASE("sdk players: the player's commands are carried out, refused ones come back in the next request") {
    InstalledPlayers installed;
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    const int area = researchableArea(test::engineRules(), s, p);
    probe(s, p,
          std::format(R"({{"orders": {{"commands": [{{"kind": "leave_fleet", "vehicle": 99999}}, {{"kind": "dance"}},
                                                     {{"kind": "set_research", "queue": [{{"area": {}}}], "evenly": false}}]}},
                        "economy": {{"keep_args": true}}}})",
                      area));
    processTurn(test::engineRules(), s, {});
    // The good command took effect.
    CHECK_FALSE(s.empire(p).researchEvenly);
    // The next request reports the others: their place in the list, the command and why.
    const Value economy = callsOf(s, p, "economy").front();
    const Value* refused = economy.find("refused");
    REQUIRE(refused);
    REQUIRE(refused->size() == 2);
    CHECK(refused->asList()[0].find("index")->asInt() == 0);
    CHECK(refused->asList()[0].find("command")->find("kind")->asString() == "leave_fleet");
    CHECK_FALSE(refused->asList()[0].find("reason")->asString().empty());
    CHECK(refused->asList()[1].find("index")->asInt() == 1);
    CHECK(refused->asList()[1].find("reason")->asString().find("'dance' is not a command kind") != std::string::npos);
    CHECK(economy.find("given")->find("refused") != nullptr);
    // Reported once only.
    CHECK_FALSE(callsOf(s, p, "end_session").front().find("refused"));
}

TEST_CASE("sdk players: memory persists across turns, saves and loads, within its size limit") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(9, true, {scriptPlayer("Steady"), Controller{}, Controller{}});
    for (int t = 0; t < 3; ++t) processTurn(r, s, {});
    CHECK(intIn(memoryOf(s, EmpireId{0u}), "sessions") == 3);
    auto loaded = deserializeState(serializeState(s));
    REQUIRE(loaded.has_value());
    GameState l = std::move(*loaded);
    CHECK(l.empire(EmpireId{0u}).script == s.empire(EmpireId{0u}).script);
    CHECK(l.empire(EmpireId{0u}).controller == scriptPlayer("Steady"));
    // A game saved and loaded goes on as one that was not.
    for (int t = 0; t < 2; ++t) {
        processTurn(r, s, {});
        processTurn(r, l, {});
    }
    CHECK(intIn(memoryOf(l, EmpireId{0u}), "sessions") == 5);
    CHECK(stateChecksum(l) == stateChecksum(s));
    // Other empires never see a player's memory.
    const GameState view = redactForEmpire(r, s, EmpireId{1u});
    CHECK(view.empire(EmpireId{0u}).script.memory.empty());
    CHECK(view.journal.entries.empty());
    CHECK(view.empire(EmpireId{0u}).controller == scriptPlayer("Steady"));
}

// ---- Failures ---------------------------------------------------------------------------------------------

TEST_CASE("sdk players: an error falls back to the classic AI for that request; three in a turn, for the rest of it") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    // The classic economy makes designs; the Probe's own answer makes none.
    GameState answered = playersGame(5, true, {Controller{}, Controller{}});
    probe(answered, EmpireId{1u});
    processTurn(r, answered, {});
    CHECK(answered.empire(EmpireId{1u}).designs.empty());
    CHECK(answered.empire(EmpireId{1u}).script.failures == 0);

    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    probe(s, p, R"({"economy": {"do": "raise"}})");
    processTurn(r, s, {});
    CHECK_FALSE(s.empire(p).designs.empty());   // the classic economy answered: its Design minister made designs
    CHECK(s.empire(p).script.failures == 1);
    CHECK(s.empire(p).script.failureTurn == 0);
    const auto economy = journalOf(s, p, "economy");
    REQUIRE(economy.size() == 1);
    const Value failed = responseOf(economy[0]);
    const Value error = *failed.find("error");
    CHECK(error.find("type")->asString() == "ValueError");
    CHECK(error.find("message")->asString() == "as the test asked");
    // The player itself goes on: its later answers come from the same object.
    CHECK(callNames(s, p) == std::vector<std::string>{"politics", "orders", "economy", "end_session"});

    // Three failures: nothing more is asked that turn, and the next turn starts afresh.
    GameState t = playersGame(5, true, {Controller{}, Controller{}});
    probe(t, p, R"({"politics": {"do": "raise"}, "orders": {"do": "raise"}, "economy": {"do": "raise"}})");
    processTurn(r, t, {});
    CHECK(t.empire(p).script.failures == 3);
    std::vector<std::string> asked;
    for (const JournalEntry& j : journalOf(t, p)) asked.push_back(j.call);
    CHECK(asked == std::vector<std::string>{"politics", "orders", "economy"});   // no end_session
    processTurn(r, t, {});
    CHECK(t.empire(p).script.failureTurn == 1);
    CHECK(t.empire(p).script.failures == 3);
    CHECK(journalOf(t, p).size() == 3);   // the journal keeps the last turn's
    CHECK(journalOf(t, p).front().turn == 1);
}

TEST_CASE("sdk players: the setup's observer hears what each live request cost") {
    std::vector<sdk::RequestCost> heard;
    std::vector<std::string> calls;
    sdk::PlayerSetup setup;
    setup.observe = [&](const sdk::RequestCost& c) {
        heard.push_back(c);
        calls.emplace_back(c.call);
    };
    InstalledPlayers installed(std::move(setup));
    const Rules& r = test::engineRules();
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    probe(s, p, R"({"economy": {"do": "raise"}})");
    processTurn(r, s, {});
    // Only the script empire's requests, each once, with what they cost; the failed one says so.
    CHECK(calls == std::vector<std::string>{"politics", "orders", "economy", "end_session"});
    for (const sdk::RequestCost& c : heard) {
        INFO(c.call);
        CHECK(c.empire == p);
        CHECK(c.turn == 0);
        CHECK(c.bytecodes > 0);
        CHECK(c.time.count() > 0);
        CHECK(c.failed == (c.call == "economy"));
    }
}

TEST_CASE("sdk players: a player that runs out of its budget, its heap or its memory limit fails, the same way every time") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    const EmpireId p{1u};
    auto run = [&](std::string_view script, auto&& tweak) {
        GameState s = playersGame(5, true, {Controller{}, Controller{}});
        tweak(s);
        probe(s, p, script);
        processTurn(r, s, {});
        return s;
    };
    auto errorOf = [&](const GameState& s, std::string_view call) {
        const auto j = journalOf(s, p, call);
        REQUIRE(j.size() == 1);
        const Value response = responseOf(j[0]);
        const Value* e = response.find("error");
        REQUIRE(e);
        return e->find("type")->asString() + ": " + e->find("message")->asString();
    };
    // The budget: a planning call's, from the game's options.
    auto small = [](GameState& s) { s.options.aiPlanningBudget = 200'000; };
    const GameState looped = run(R"({"orders": {"do": "loop"}})", small);
    CHECK(errorOf(looped, "orders").find("BudgetExceeded") == 0);
    CHECK(looped.empire(p).script.failures == 1);
    CHECK(stateChecksum(run(R"({"orders": {"do": "loop"}})", small)) == stateChecksum(looped));
    // A mid-turn call has the smaller budget.
    auto smallCall = [](GameState& s) { s.options.aiCallBudget = 50'000; };
    const GameState session = run(R"({"end_session": {"do": "loop"}})", smallCall);
    CHECK(errorOf(session, "end_session").find("BudgetExceeded") == 0);
    // The heap.
    const GameState hoard = run(R"({"orders": {"do": "heap"}})", [](GameState&) {});
    CHECK(errorOf(hoard, "orders").find("MemoryError") == 0);
    // The memory's size limit.
    auto limit = [](GameState& s) { s.options.aiMemoryLimit = 100'000; };
    const GameState grown = run(R"({"orders": {"do": "grow", "size": 200000}})", limit);
    CHECK(errorOf(grown, "orders").find("MemoryError: the memory takes") == 0);
    CHECK(grown.empire(p).script.memory.size() < 100'000);
    // A response of the wrong shape.
    const GameState bad = run(R"({"orders": {"respond": {"comands": []}}})", [](GameState&) {});
    CHECK_MESSAGE(errorOf(bad, "orders").find("ValueError: comands: unknown field") == 0, errorOf(bad, "orders"));
    const GameState list = run(R"({"orders": {"respond": [1, 2]}})", [](GameState&) {});
    CHECK(errorOf(list, "orders").find("ValueError: the response is a list") == 0);
}

TEST_CASE("sdk players: a player whose mod or module is missing fails each request and the classic AI plays") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    s.empire(p).controller = scriptPlayer("Nobody");
    processTurn(r, s, {});
    CHECK(s.empire(p).script.failures == 3);
    CHECK_FALSE(s.empire(p).designs.empty());
    const Value first = responseOf(journalOf(s, p).front());
    const Value error = *first.find("error");
    CHECK(error.find("message")->asString().find("declares no computer player named 'Nobody'") != std::string::npos);
    s.empire(p).controller.mod = "test.nowhere";
    processTurn(r, s, {});
    const Value last = responseOf(journalOf(s, p).back());
    CHECK(last.find("error")->find("message")->asString().find("the mod test.nowhere is not here") != std::string::npos);
}

// ---- Mid-turn calls ------------------------------------------------------------------------------------

TEST_CASE("sdk players: colony_type names the new colony's type from the empire's list") {
    using namespace mvtest;
    for (const char* mode : {"answer", "null", "bad"}) {
        INFO(mode);
        World w;
        const Rules& r = w.rules();
        const SystemId b = w.system("B", 10, 0);
        const ObjectId target = w.planet(b, {5, 6});
        const std::vector<std::string> types = w.s.empire(kA).colonyTypes;
        REQUIRE(types.size() > 1);
        const std::string chosen = types.back();
        const std::string answer = std::string_view(mode) == "answer" ? std::format("\"{}\"", chosen)
                                   : std::string_view(mode) == "null" ? "null"
                                                                      : "\"Nonsense Colony\"";
        probe(w.s, kA, std::format(R"({{"colony_type": {{"answer": {}, "keep_args": true}}}})", answer));
        const VehicleId ship = w.spawn(w.ship(kA, "Settler", 4, {"Test Rock Pod"}), at(b, 5, 6));
        fuel(w, ship);
        w.v(ship).cargo.population.push_back({kA, 2});
        w.order(ship, mk(OrderKind::Colonize, {}, target));
        const std::string classic = ai::colonyTypeAtColonization(r, w.s, kA, target);
        Direct d(r, w.s);
        movement::runColonization(d.ctx);
        d.end();
        REQUIRE(w.s.colony(target));
        const std::vector<Value> asked = callsOf(w.s, kA, "colony_type");
        REQUIRE(asked.size() == 1);
        CHECK(keysOf(*asked[0].find("args")) == std::vector<std::string>{"choices", "colony", "planet", "vehicle"});
        const Value& given = *asked[0].find("given");
        CHECK(given.find("planet")->asInt() == static_cast<int64_t>(target.value));
        CHECK(given.find("colony")->asInt() == static_cast<int64_t>(target.value));
        CHECK(given.find("vehicle")->asInt() == static_cast<int64_t>(ship.value));
        CHECK(given.find("choices")->size() == types.size());
        if (std::string_view(mode) == "answer") {
            CHECK(w.s.colony(target)->colonyType == chosen);
            CHECK(w.s.empire(kA).script.failures == 0);
        } else {
            CHECK(w.s.colony(target)->colonyType == classic);
            CHECK(w.s.empire(kA).script.failures == (std::string_view(mode) == "bad" ? 1 : 0));
        }
    }
}

TEST_CASE("sdk players: enter_sector asks before a step into a sector with enemies; a group that declines stops there") {
    using namespace mvtest;
    for (const bool enter : {false, true}) {
        INFO("enter " << enter);
        World w;
        const Rules& r = w.rules();
        const SystemId a = w.system("A");
        w.exploreAll(kA);
        w.exploreAll(kB);
        const VehicleId mover = w.spawn(w.ship(kA, "Scout", 4, {"Mv Sensor 3"}), at(a, 2, 6));
        fuel(w, mover);
        w.spawn(w.ship(kB, "Picket", 1), at(a, 4, 6));
        probe(w.s, kA, std::format(R"({{"enter_sector": {{"answer": {}, "keep_args": true}}}})", enter ? "true" : "false"));
        w.order(mover, mk(OrderKind::MoveTo, at(a, 4, 6)));   // into the enemy's sector
        sight::updateKnowledge(r, w.s);
        Direct d(r, w.s);
        movement::startTurn(d.ctx);
        movement::runMovementAndCombat(d.ctx);
        d.end();
        const std::vector<Value> asked = callsOf(w.s, kA, "enter_sector");
        REQUIRE(asked.size() == 1);   // once, even though the group tries again on later days
        const Value& given = *asked[0].find("given");
        CHECK(given.find("vehicles")->asList().front().asInt() == static_cast<int64_t>(mover.value));
        CHECK(given.find("sector")->find("x")->asInt() == 4);
        CHECK(given.find("sector")->find("y")->asInt() == 6);
        CHECK(given.find("enemies")->asList().front().asInt() == 1);
        const Location there = w.v(mover).location;
        if (enter) CHECK(there == at(a, 4, 6));
        else CHECK((there.sector.x == 3 && std::abs(there.sector.y - 6) <= 1));
        CHECK(w.v(mover).orders.size() == (enter ? 0u : 1u));   // a group that declines keeps its order
    }
}

TEST_CASE("sdk players: decloak asks before a cloaked colony carries out an order") {
    using namespace mvtest;
    for (const bool decloak : {false, true}) {
        INFO("decloak " << decloak);
        World w;
        const Rules& r = w.rules();
        const SystemId a = w.system("A");
        const ObjectId home = w.planet(a, {6, 6});
        w.colony(home, kA, 1000, {"Mv Converter", "Mv Planet Cloak"});
        w.s.colony(home)->cloaked = true;
        w.s.empire(kA).stockpile = {10000, 10000, 10000};
        probe(w.s, kA, std::format(R"({{"decloak": {{"answer": {}, "keep_args": true}}}})", decloak ? "true" : "false"));
        w.s.colony(home)->orders = economy::conversionOrders(Resource::Minerals, Resource::Organics, 1000);
        Direct d(r, w.s);
        movement::startTurn(d.ctx);
        movement::runMovementAndCombat(d.ctx);
        d.end();
        CHECK(w.s.empire(kA).stockpile[Resource::Minerals] == 9000);   // the order ran either way
        const std::vector<Value> asked = callsOf(w.s, kA, "decloak");
        REQUIRE(asked.size() == 1);
        const Value& given = *asked[0].find("given");
        CHECK(given.find("object")->asInt() == static_cast<int64_t>(home.value));
        CHECK(given.find("planet")->asInt() == static_cast<int64_t>(home.value));
        CHECK(given.find("vehicle")->isNull());
        CHECK(given.find("reason")->asString() == "order");
        CHECK(w.s.colony(home)->cloaked);   // kept cloaked, or cloaked again after the order
    }
}

TEST_CASE("sdk players: decloak asks before a cloaked ship's attack in a turn-based game") {
    using namespace mvtest;
    for (const bool decloak : {false, true}) {
        INFO("decloak " << decloak);
        World w;
        const Rules& r = w.rules();
        w.s.options.simultaneous = false;
        const SystemId a = w.system("A");
        w.exploreAll(kA);
        const VehicleId raider = w.spawn(w.ship(kA, "Raider", 2, {"Mv Cloak"}), at(a, 4, 6));
        fuel(w, raider);
        w.v(raider).status = VehicleStatus::Cloaked;
        const VehicleId target = w.spawn(w.ship(kB, "Picket", 1), at(a, 4, 6));
        probe(w.s, kA, std::format(R"({{"decloak": {{"answer": {}, "keep_args": true}}}})", decloak ? "true" : "false"));
        Order attack = mk(OrderKind::Attack, at(a, 4, 6));
        attack.vehicle = target;
        w.order(raider, attack);
        Direct d(r, w.s);
        movement::startTurn(d.ctx, kA);
        movement::LiveMove move;
        move.empire = kA;
        movement::runLive(d.ctx, move);
        d.end();
        const std::vector<Value> asked = callsOf(w.s, kA, "decloak");
        REQUIRE(asked.size() == 1);
        const Value& given = *asked[0].find("given");
        CHECK(given.find("object")->asInt() == static_cast<int64_t>(raider.value));
        CHECK(given.find("vehicle")->asInt() == static_cast<int64_t>(raider.value));
        CHECK(given.find("planet")->isNull());
        CHECK(given.find("reason")->asString() == "attack");
    }
}

TEST_CASE("sdk players: battle_round gives a side's orders each combat turn; refused ones come back; null leaves it to the strategies") {
    const Rules& r = ctest::combatRules();
    for (int variant : {0, 1, 3}) {
        INFO("variant " << variant);
        // Null every round: the battle is the strategies' own.
        auto [plain, where] = ctest::battleScenario(variant, 23);
        GameState scripted = plain;
        for (GameState* s : {&plain, &scripted}) s->empire(EmpireId{0u}).kind = PlayerKind::Computer;
        probe(scripted, EmpireId{0u}, R"({"battle_round": {"keep_args": true}})");
        {
            TurnContext ctx = ctest::context(plain, r);
            combat::resolveSpaceCombat(ctx, where);
        }
        {
            Direct d(r, scripted);
            combat::resolveSpaceCombat(d.ctx, where);
            d.end();
        }
        const std::vector<Value> rounds = callsOf(scripted, EmpireId{0u}, "battle_round");
        REQUIRE_FALSE(rounds.empty());
        CHECK(checksumWithoutPlayers(scripted) == checksumWithoutPlayers(plain));
        const Value& battle = *rounds.front().find("given")->find("battle");
        CHECK(battle.find("round")->asInt() == 1);
        CHECK(battle.find("rounds_max")->asInt() >= 1);
        CHECK(battle.find("location")->find("system")->asInt() == static_cast<int64_t>(where.system.value));
        const Value& piece = battle.find("pieces")->asList().front();
        for (const char* key : {"id", "kind", "owner", "design", "position", "facing", "damage", "shields", "weapons", "speed", "cloaked"})
            CHECK_MESSAGE(piece.find(key), key);
        for (size_t i = 1; i < rounds.size(); ++i) CHECK(rounds[i].find("given")->find("battle")->find("round")->asInt() > 1);
    }
    // Orders: a refused one is reported in the next request; End Turn leaves the pieces idle.
    auto [s, where] = ctest::battleScenario(0, 23);
    s.empire(EmpireId{0u}).kind = PlayerKind::Computer;
    probe(s, EmpireId{0u}, R"({"battle_round": {"answer": {"orders": [{"kind": "fire", "piece": 999, "target": 0}, {"kind": "auto", "piece": -1},
                                                                        {"kind": "end_phase"}]}, "keep_args": true}})");
    Direct d(r, s);
    combat::resolveSpaceCombat(d.ctx, where);
    d.end();
    const std::vector<Value> rounds = callsOf(s, EmpireId{0u}, "battle_round");
    REQUIRE(rounds.size() >= 2);
    const Value* refused = rounds[1].find("refused");
    REQUIRE(refused);
    REQUIRE(refused->size() == 2);
    CHECK(refused->asList()[0].find("index")->asInt() == 0);
    CHECK(refused->asList()[0].find("reason")->asString() == "No such piece.");
    CHECK(refused->asList()[1].find("index")->asInt() == 1);
    // Its pieces did nothing but react (point defence): far fewer hits than the strategies make.
    auto hitsBy = [](const GameState& g, EmpireId e) {
        int n = 0;
        for (const CombatRecord& rec : g.combats)
            for (const CombatEvent& ev : rec.events) n += ev.kind == CombatEvent::Kind::Hit && ev.piece < rec.pieces.size() && rec.pieces[ev.piece].owner == e;
        return n;
    };
    auto [plain, plainWhere] = ctest::battleScenario(0, 23);
    {
        TurnContext ctx = ctest::context(plain, r);
        combat::resolveSpaceCombat(ctx, plainWhere);
    }
    CHECK(hitsBy(s, EmpireId{0u}) < hitsBy(plain, EmpireId{0u}));
}

// ---- Services ------------------------------------------------------------------------------------------------

TEST_CASE("sdk players: services: builtin gives the classic ministers' commands, all of them or some; the others answer as documented") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(13, true, {Controller{}, Controller{}});
    for (int t = 0; t < 3; ++t) processTurn(r, s, {});   // a game with some history
    const EmpireId p{1u};
    const ObjectId home = test::homeworld(s, p).planet;
    probe(s, p,
          std::format(R"({{"orders": {{"builtin": {{"call": "orders"}}, "rules": true,
                                       "builtin_answer": {{"call": "colony_type", "args": {{"planet": {}}}}},
                                       "query": {{"name": "path", "args": {{"origin": {}, "destination": {}}}}}}},
                         "economy": {{"builtin": {{"call": "economy", "ministers": ["research"]}}}}}})",
                      home.value, s.galaxy.object(home).system.value, s.galaxy.object(home).system.value));
    // The classic commands for the same state.
    GameState before = s;
    const std::vector<SystemId> territory = before.empire(p).claimedSystems;
    const std::vector<Command> classic = ai::planOrdersAfterPolitics(r, before, p, &territory);
    const std::string classicType = ai::colonyTypeAtColonization(r, before, p, home);

    Direct d(r, s);
    const CommandSink sink{[&](const Command& c) { return apply(r, s, p, c); }, [] {}};
    CHECK(d.players->plan(d.ctx, p, PlanCall::Orders, sink));
    CHECK(d.players->plan(d.ctx, p, PlanCall::Economy, sink));
    d.end();
    const Value m = memoryOf(s, p);
    REQUIRE(m.find("builtin_orders"));
    CHECK(*m.find("builtin_orders") == sdk::encodeCommands(classic));
    CHECK_FALSE(classic.empty());
    CHECK(m.find("rules")->asInt() == static_cast<int64_t>(r.data().components.size()));
    CHECK(m.find("builtin_answer")->asString() == classicType);
    CHECK(m.find("query")->find("found")->asBool());
    // Research only: every command is the Research minister's.
    REQUIRE(m.find("builtin_economy"));
    for (const Value& c : m.find("builtin_economy")->asList()) CHECK(c.find("kind")->asString() == "set_research");
}

TEST_CASE("sdk players: services: builtin with ministers runs only those; apply carries a command out at once and reports what changed") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(13, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    const int area = researchableArea(r, s, p);
    probe(s, p,
          std::format(R"({{"economy": {{"builtin": {{"call": "economy", "ministers": ["research"]}},
                                        "apply": [{{"kind": "set_research", "queue": [{{"area": {}}}]}}, {{"kind": "leave_fleet", "vehicle": 99999}}]}}}})",
                      area));
    Direct d(r, s);
    std::vector<Command> carried;
    const CommandSink sink{[&](const Command& c) {
                               carried.push_back(c);
                               return apply(r, s, p, c);
                           },
                           [] {}};
    CHECK(d.players->plan(d.ctx, p, PlanCall::Economy, sink));
    d.end();
    const Value m = memoryOf(s, p);
    const Value& research = *m.find("builtin_economy");
    REQUIRE(research.size() >= 1);
    for (const Value& c : research.asList()) CHECK(c.find("kind")->asString() == "set_research");
    const Value& applied = *m.find("applied");
    REQUIRE(applied.size() == 2);
    CHECK(applied.asList()[0].find("ok")->asBool());
    CHECK(keysOf(*applied.asList()[0].find("changed")) == std::vector<std::string>{"my"});
    CHECK_FALSE(applied.asList()[1].find("ok")->asBool());
    CHECK_FALSE(applied.asList()[1].find("reason")->asString().empty());
    CHECK(carried.size() == 2);
    REQUIRE(s.empire(p).research.size() == 1);
    // The journal keeps what apply carried out, to carry it out again in a replay.
    const Value response = responseOf(journalOf(s, p, "economy").front());
    CHECK(response.find("applied")->size() == 2);
}

TEST_CASE("sdk players: services cost budget, and apply is for planning calls only") {
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    GameState s = playersGame(13, true, {Controller{}, Controller{}});
    const EmpireId p{1u};
    s.options.aiPlanningBudget = 1'000'000;   // less than one builtin call costs
    probe(s, p, R"({"orders": {"builtin": {"call": "orders"}}, "end_session": {"apply": [{"kind": "leave_fleet", "vehicle": 1}]}})");
    processTurn(r, s, {});
    auto errorOf = [&](std::string_view call) {
        const auto j = journalOf(s, p, call);
        REQUIRE(j.size() == 1);
        const Value response = responseOf(j[0]);
        const Value* e = response.find("error");
        return e ? e->find("type")->asString() + ": " + e->find("message")->asString() : std::string();
    };
    CHECK(errorOf("orders").find("BudgetExceeded") == 0);
    CHECK(errorOf("end_session") == "RuntimeError: apply: commands are given only in the planning calls (politics, orders, economy)");
}

TEST_CASE("sdk players: a session's players run on a thread of their own, whatever stack the engine's caller has") {
    InstalledPlayers installed;
    GameState s = playersGame(11, true, {scriptPlayer("Steady"), Controller{}});
    sdk::Worker small(size_t{256} << 10);   // a thread with a small stack plays the turn
    small.run([&] { processTurn(test::engineRules(), s, {}); });
    CHECK(intIn(memoryOf(s, EmpireId{0u}), "sessions") == 1);
    CHECK(s.empire(EmpireId{0u}).script.failures == 0);
}
