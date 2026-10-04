// Battles shown as they happen, as the classic client's session plays them
// (client/classic/session.hpp, game/turn.hpp): the turn stops at a battle,
// the Strategic Combat window fights it on the question's copy of the game,
// Close answers it and the turn goes on. The turn ends once, its autosave
// loads as it was, and a fault in an engine call leaves the game as it was
// before the call. Content is invented for the tests, but for the opt-in
// soak on the installed data (OPENSE4_BATTLE_SOAK).

#include "combat_fixture.hpp"

#include "client/classic/replay.hpp"
#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/session.hpp"

#include "game/commands.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::game;
namespace classic = opense4::client::classic;

namespace {

std::shared_ptr<const Rules> soakRules() {
    static const std::shared_ptr<const Rules> rules = []() -> std::shared_ptr<const Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_shared<const Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules;
}

// What the Strategic Combat window does with the question: the battle set up
// on the question's copy of the game, handed to the session, stepped one
// empire phase at a time with the forces list and the playback following it,
// then Close (ClassicSession::endTactical answers the question). False when
// no battle starts there (the window then answers at once).
bool fightStrategic(classic::ClassicSession& session, const Rules& rules) {
    const BattleQuestion q = *session.battleQuestion();
    REQUIRE(q.state);
    combat::TacticalBattle::Setup bs{q.where, q.entering, {}, std::nullopt, std::nullopt, std::nullopt, q.check};
    bs.stepped = true;
    auto preview = std::make_unique<combat::TacticalBattle>(rules, *q.state, std::move(bs));
    if (!preview->started()) {
        session.answerBattle({});
        return false;
    }
    classic::CombatForces forces;
    forces.setup(rules, preview->state(), preview->pieces());
    classic::TacticalFight f;
    f.kind = classic::TacticalFight::Kind::Game;
    f.battle = std::move(preview);
    f.title = "Strategic Combat";
    session.startTactical(std::move(f));
    classic::TacticalFight* fight = session.tactical();
    REQUIRE(fight);
    classic::CombatPlayback playback(fight->battle->record());
    while (fight->battle->step()) {
        playback = classic::CombatPlayback(fight->battle->record());
        playback.seekEvent(playback.eventCount());
        forces.count(rules, fight->battle->state(), fight->battle->pieces());
    }
    session.endTactical();
    return true;
}

int envInt(const char* name, int fallback) {
    const char* v = std::getenv(name);
    return v ? std::atoi(v) : fallback;
}

} // namespace

// Opt-in, on the installed data: OPENSE4_BATTLE_SOAK=<games> (seeds from
// OPENSE4_BATTLE_SOAK_SEED, OPENSE4_BATTLE_SOAK_TURNS turns each, _OPP
// computer players, _SYSTEMS systems, _SIM=1 for simultaneous turns). The
// player's empire is played by its ministers, every battle answered
// Strategic; each turn must come out as the same turn played without stops.
TEST_CASE("battle flow: quick games on the installed data, every battle Strategic (opt-in soak)") {
    const int games = envInt("OPENSE4_BATTLE_SOAK", 0);
    if (games <= 0) return;
    auto rules = soakRules();
    REQUIRE(rules);
    const int firstSeed = envInt("OPENSE4_BATTLE_SOAK_SEED", 1);
    const int turns = envInt("OPENSE4_BATTLE_SOAK_TURNS", 150);
    const bool simultaneous = envInt("OPENSE4_BATTLE_SOAK_SIM", 0) != 0;
    std::string preset;
    for (const auto& p : rules->racePresets())
        if (!p.neutral) {
            preset = p.folder;
            break;
        }
    for (int seed = firstSeed; seed < firstSeed + games; ++seed) {
        CAPTURE(seed);
        GameSetup setup = classic::setup::quickStartGame(*rules, preset, uint64_t(seed), envInt("OPENSE4_BATTLE_SOAK_OPP", 4));
        setup.options.simultaneous = simultaneous;
        setup.options.systemCount = envInt("OPENSE4_BATTLE_SOAK_SYSTEMS", 12);
        StartExtras extras;
        extras.designMinisterRun.push_back(EmpireId{0u});
        auto game = createGame(*rules, setup, extras);
        REQUIRE(game.has_value());
        classic::ClassicSession session(rules, std::move(*game), EmpireId{0u}, classic::SessionKind::Local);
        cmd::SetMinisters all;
        all.completeAi = true;
        REQUIRE(session.issue(all).ok);
        int battles = 0, warped = 0, ground = 0, diverged = 0;
        for (int t = 0; t < turns && !session.state().gameOver && !session.humansGone(); ++t) {
            const uint32_t before = session.state().turn;
            GameState silent = session.state();
            LiveOptions lo;
            lo.endWithoutHumans = true;
            endPlayerTurn(*rules, silent, session.player(), lo, nullptr);
            session.endTurn();
            int guard = 0;
            while (session.battleQuestion() && ++guard < 500) {
                const BattleQuestion q = *session.battleQuestion();
                if (q.kind == BattleQuestion::Kind::Ground) {
                    ++ground;
                    session.answerBattle({});
                    continue;
                }
                ++battles;
                REQUIRE(q.state);
                for (VehicleId v : q.entering.value_or(std::vector<VehicleId>{}))
                    if (const Vehicle* x = q.state->vehicle(v); x && x->cameFrom.system != x->location.system && x->cameFromTurn == q.state->turn) {
                        ++warped;
                        break;
                    }
                fightStrategic(session, *rules);
            }
            CHECK(guard < 500);
            if (stateChecksum(silent) != stateChecksum(session.state())) {
                ++diverged;
                std::string parts;
                for (const std::string& n : differingStateParts(statePartHashes(silent), statePartHashes(session.state()))) parts += " " + n;
                std::printf("seed %d turn %u: shown game differs from silent one in:%s\n", seed, before, parts.c_str());
            }
            CHECK_FALSE(session.battleQuestion().has_value());
            if (!session.state().gameOver && !session.humansGone()) CHECK(session.state().turn == before + 1);
        }
        std::printf("seed %d: turn %u battles %d warped %d ground %d diverged %d\n", seed, session.state().turn, battles, warped, ground, diverged);
        std::fflush(stdout);
    }
}

namespace {

// Two empires at war in a turn-based game: A (human) waits with two armed
// ships at a warp point of its home system; B (computer) has a ship at the
// other end with orders to come through.
struct WarpAmbush {
    ctest::Arena ar = ctest::makeArena(41);
    Location exit, entry;
    VehicleId raider;
};

WarpAmbush warpAmbush() {
    WarpAmbush w;
    GameState& s = w.ar.s;
    s.options.simultaneous = false;
    s.options.autosaveTurns = 1;
    s.empire(w.ar.a).kind = PlayerKind::Human;
    s.empire(w.ar.b).kind = PlayerKind::Computer;
    ObjectId near, far;
    for (ObjectId o : s.galaxy.system(w.ar.loc.system).objects)
        if (s.galaxy.object(o).kind == ObjectKind::WarpPoint) {
            near = o;
            far = s.galaxy.object(o).destination;
            break;
        }
    REQUIRE(near.valid());
    REQUIRE(far.valid());
    w.exit = {w.ar.loc.system, s.galaxy.object(near).sector};
    w.entry = {s.galaxy.object(far).system, s.galaxy.object(far).sector};
    for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
    const DesignId picket = ctest::frigate(s, w.ar.a, "Picket", 2, {"Test Laser", "CT Big Armor"});
    ctest::spawn(s, picket, w.exit);
    ctest::spawn(s, picket, w.exit);
    w.raider = ctest::spawn(s, ctest::frigate(s, w.ar.b, "Raider", 3, {"Test Laser"}), w.entry);
    Order warp;
    warp.kind = OrderKind::Warp;
    warp.object = far;
    s.vehicle(w.raider)->orders = {warp};
    return w;
}

std::shared_ptr<const Rules> fixtureRules() { return std::shared_ptr<const Rules>(&ctest::combatRules(), [](const Rules*) {}); }

std::filesystem::path autosaveFile(uint32_t turn) { return classic::savesDir() / (*classic::setup::autosaveName(1, turn) + ".gam"); }

} // namespace

TEST_CASE("battle flow: an enemy warps in at waiting ships; Strategic; the turn ends once and its autosave loads as it was") {
    const Rules& rules = ctest::combatRules();
    const WarpAmbush w = warpAmbush();
    classic::ClassicSession session(fixtureRules(), w.ar.s, w.ar.a, classic::SessionKind::Local);
    REQUIRE(session.myTurn());
    const uint32_t t0 = session.state().turn;
    const uint64_t before = stateChecksum(session.state());
    std::error_code ec;
    std::filesystem::remove(autosaveFile(t0 + 1), ec);

    // End Turn: B's turn comes, its raider warps in, and the battle stops the turn.
    const uint64_t revision = session.revision();
    session.endTurn();
    REQUIRE(session.battleQuestion().has_value());
    const BattleQuestion& q = *session.battleQuestion();
    CHECK(q.kind == BattleQuestion::Kind::Choose);
    CHECK(q.where == w.exit);
    CHECK(q.state->playerTurn.empire == w.ar.b);
    REQUIRE(q.entering.has_value());
    CHECK(std::find(q.entering->begin(), q.entering->end(), w.raider) != q.entering->end());
    CHECK(q.state->vehicle(w.raider)->cameFrom.system == w.entry.system);
    // Meanwhile the game is as it was before End Turn (put back from the
    // engine's copy: whoever held pointers into it must take them again, and
    // the revision says so), nothing is saved, and End Turn does nothing.
    CHECK(session.revision() != revision);
    CHECK(stateChecksum(session.state()) == before);
    CHECK(session.myTurn());
    CHECK_FALSE(std::filesystem::exists(autosaveFile(t0 + 1)));
    session.endTurn();
    CHECK(session.battleQuestion().has_value());
    CHECK(stateChecksum(session.state()) == before);

    // Strategic, then Close: the turn goes on and ends, once.
    REQUIRE(fightStrategic(session, rules));
    CHECK_FALSE(session.battleQuestion().has_value());
    CHECK_FALSE(session.tactical());
    CHECK(session.state().turn == t0 + 1);
    CHECK(session.myTurn());
    REQUIRE(session.state().combats.size() >= 1);
    CHECK(session.state().combats.back().location == w.exit);

    // The same game played without stops (a network host, automation) comes out the same.
    GameState silent = w.ar.s;
    resumeTurnBased(rules, silent);
    endPlayerTurn(rules, silent, w.ar.a, LiveOptions{{}, true}, nullptr);
    CHECK(stateChecksum(silent) == stateChecksum(session.state()));

    // Its autosave holds that game, and loading it plays nothing more.
    REQUIRE(std::filesystem::exists(autosaveFile(t0 + 1)));
    auto loaded = classic::ClassicSession::load(fixtureRules(), autosaveFile(t0 + 1));
    REQUIRE(loaded.has_value());
    CHECK((*loaded)->state().turn == t0 + 1);
    CHECK((*loaded)->myTurn());
    CHECK(stateChecksum((*loaded)->state()) == stateChecksum(session.state()));

    // The next End Turn ends the next turn.
    session.endTurn();
    for (int guard = 0; session.battleQuestion() && guard < 20; ++guard) fightStrategic(session, rules);
    CHECK(session.state().turn == t0 + 2);
}

TEST_CASE("battle flow: a fault in a call that can stop for battles puts the game back as it was") {
    ctest::Arena ar = ctest::makeArena(43);
    GameState& s = ar.s;
    const uint64_t before = stateChecksum(s);
    const size_t vehicles = s.vehicles.size();
    const uint32_t turn = s.turn;
    const std::vector<BattleAnswer> none;
    auto changeThen = [&](auto thrown) {
        return [&s, thrown](TurnContext::Battles*) -> TurnResult {
            ++s.turn;
            s.vehicles.emplace_back();
            throw thrown;
        };
    };
    // A fault: the state comes back and the exception goes on.
    CHECK_THROWS_AS(detail::withBattles(s, &none, changeThen(std::runtime_error("a fault"))), std::runtime_error);
    CHECK(stateChecksum(s) == before);
    CHECK(s.vehicles.size() == vehicles);
    // A stop: the state comes back and the result holds the question.
    BattleQuestion q;
    q.where = ar.loc;
    const TurnResult stopped = detail::withBattles(s, &none, changeThen(detail::BattleQuestionRaised{q}));
    REQUIRE(stopped.battle.has_value());
    CHECK(stopped.battle->where == ar.loc);
    CHECK(stateChecksum(s) == before);
    // Without answers nothing stops and no copy is kept.
    CHECK_THROWS_AS(detail::withBattles(s, nullptr, changeThen(std::runtime_error("a fault"))), std::runtime_error);
    CHECK(s.turn == turn + 1);
    CHECK(s.vehicles.size() == vehicles + 1);
}
