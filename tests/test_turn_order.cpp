// The classic turn order (spec 05 §8): what happens before what within one
// processed turn.

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/ai.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};
constexpr size_t kMissing = std::numeric_limits<size_t>::max();

// Human players who send nothing and have no ministers at work: only the
// rules act.
void quietTurn(const Rules& r, GameState& s) {
    TurnOptions o;
    o.aiForMissing = false;
    processTurn(r, s, {}, o);
}

size_t logIndex(const GameState& s, EmpireId e, std::string_view title) {
    const auto& log = s.empire(e).log;
    for (size_t i = 0; i < log.size(); ++i)
        if (log[i].title == title) return i;
    return kMissing;
}

// Rules whose Events.txt holds exactly one record (the rest as politicsRules,
// where the High event frequency is a 100 % chance).
std::unique_ptr<Rules> rulesWithEvent(effects::Effect e, int amount, std::string to = "Owner") {
    ruleset::Ruleset rs = buildPoliticsRuleset();
    ruleset::EventType ev;
    ev.type = std::string(effects::identifier(e));
    ev.severity = "Low";
    ev.effectAmount = amount;
    ev.messageTo = std::move(to);
    ev.messages = {{"Omen", "Something happened."}};
    rs.eventTypes = {ev};
    return std::make_unique<Rules>(std::move(rs));
}

} // namespace

TEST_CASE("turn order: research spends the pool the previous turn filled, then the income refills it") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const auto beams = techArea(r, "Test Beams");
    auto& levels = s.empire(kA).techLevels;
    if (levels.size() <= beams.index()) levels.resize(beams.index() + 1, 0);
    levels[beams.index()] = 6;  // a level that takes a few turns
    s.empire(kA).research = {{beams, 0}};
    s.empire(kA).researchPool = 300;  // what the previous turn's income left for this turn

    quietTurn(r, s);
    // The research step (4) spent the 300 points; the income step (5) came after it.
    REQUIRE(s.empire(kA).research.size() == 1);
    CHECK(s.empire(kA).research.front().progress == 300);
    const int64_t filled = s.empire(kA).researchPool;
    CHECK(filled > 0);  // this turn's research income, for the next turn

    REQUIRE(300 + filled < research::levelCost(r, s, beams, s.empire(kA).techLevel(beams) + 1));
    quietTurn(r, s);
    CHECK(s.empire(kA).research.front().progress == 300 + filled);
}

TEST_CASE("turn order: a master receives its subject's tariffs at the subject's income step") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    TurnContext ctx = turnContext(r, s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Subjugation, true);  // A is the master
    REQUIRE(diplomacy::masterOf(s, kB) == kA);

    // The master's own processing comes first (lower empire number) and takes nothing.
    empireEndOfTurn(ctx, kA, false);
    CHECK(s.empire(kA).economy.tariffsIn == Resources{});
    const Resources before = s.empire(kA).stockpile;
    // The subject's processing pays the master at once, in the same turn.
    empireEndOfTurn(ctx, kB, false);
    const Resources paid = s.empire(kB).economy.tariffsOut;
    CHECK(paid.total() > 0);
    CHECK(s.empire(kA).stockpile == before + paid);
}

TEST_CASE("turn order: an empire's destruction is checked right after its own processing") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kC, kB);
    // B has nothing left: no colony, no ship.
    for (auto& c : s.colonies)
        if (c && c->owner == kB) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == kB) v.count = 0;
    s.removeDeadVehicles();
    // A and C both finish a tech level in their research step.
    const auto beams = techArea(r, "Test Beams");
    for (EmpireId e : {kA, kC}) {
        s.empire(e).research = {{beams, 0}};
        s.empire(e).researchPool = research::levelCost(r, s, beams, s.empire(e).techLevel(beams) + 1);
    }

    quietTurn(r, s);
    CHECK_FALSE(s.empire(kB).alive);
    // A was processed before B was destroyed, C after.
    const size_t aLevel = logIndex(s, kA, "New Tech Level"), aGone = logIndex(s, kA, "Empire Destroyed");
    const size_t cLevel = logIndex(s, kC, "New Tech Level"), cGone = logIndex(s, kC, "Empire Destroyed");
    REQUIRE(aLevel != kMissing);
    REQUIRE(aGone != kMissing);
    REQUIRE(cLevel != kMissing);
    REQUIRE(cGone != kMissing);
    CHECK(aLevel < aGone);
    CHECK(cGone < cLevel);
    // B's statistics row was written at the start of its processing, before the check.
    CHECK(s.empire(kB).history.size() == 1);
}

TEST_CASE("turn order: the event roll comes after the victory check") {
    auto r = rulesWithEvent(effects::Effect::ShipExperienceChange, 1, "All");
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 3;        // a 100 % chance in the fixture
    s.turn = 19;                         // the date reaches 2402.0 this turn: events may start
    s.options.victory.years = true;
    s.options.victory.yearsValue = 2;    // ... and the game ends

    quietTurn(*r, s);
    CHECK(s.gameOver);
    // The event step still ran in the last turn, after the victory check.
    bool struck = false;
    for (const Empire& e : s.empires) {
        const size_t over = logIndex(s, e.id, "Game Over"), omen = logIndex(s, e.id, "Omen");
        REQUIRE(over != kMissing);
        if (omen == kMissing) continue;
        struck = true;
        CHECK(over < omen);
    }
    CHECK(struck);
}

TEST_CASE("turn order: mood events raised after an empire's happiness update wait for its next one") {
    auto r = rulesWithEvent(effects::Effect::ShipDamage, 1'000'000);  // a ship is lost
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 3;
    s.turn = 19;
    auto lost = [&] {
        return std::count_if(s.pendingMood.begin(), s.pendingMood.end(), [](const MoodEvent& m) { return m.trigger == "Any Ship Lost"; });
    };

    // The event step comes after every empire's happiness update: its mood
    // event waits in the game state.
    quietTurn(*r, s);
    CHECK(lost() == 1);
    s.options.eventFrequency = 0;
    quietTurn(*r, s);
    CHECK(lost() == 0);  // used by the owner's update this turn

    // A waiting event changes the next update.
    GameState calm = newPoliticsGame();
    const auto& models = politicsRules().data().happinessModels;
    for (uint32_t i = 0; i < models.size(); ++i)
        if (models[i].name == "Test Politics Mood") calm.empire(kA).race.happinessModel = i;
    GameState stirred = calm;
    stirred.pendingMood.push_back({kA, "New Treaty War", {}, {}, 10});
    quietTurn(politicsRules(), calm);
    quietTurn(politicsRules(), stirred);
    CHECK(homeworld(stirred, kA).anger != homeworld(calm, kA).anger);
    CHECK(stirred.pendingMood.empty());
}

TEST_CASE("turn order: a player with missing orders is covered by every minister for one turn") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 2, 12, true);
    for (Empire& e : s.empires) e.research.clear();
    const int before = research::totalLevels(r, s.empire(kA));
    const int beforeB = research::totalLevels(r, s.empire(kB));
    GameState minimal = s;
    minimal.empire(kA).aiMinimalChanges = true;
    // The player's own switches: only Research; Politics is off.
    s.empire(kA).ministers = ministerBit(Minister::Research);
    setContact(s, kA, kB);
    s.empire(kA).relation(kB).anger = 50;
    s.turn = 1;  // a turn with a previous one, so the political step counts it

    processTurn(r, s, {});
    CHECK_FALSE(s.empire(kA).ministerAll);  // restored after the turn (ai::standIn / ai::restoreMinisters)
    CHECK(s.empire(kA).ministers == ministerBit(Minister::Research));
    // The political step ran for the stand-in although the player's Politics minister is off (spec 05 §7.1).
    CHECK(s.empire(kA).relation(kB).anger != 50);
    // The Research minister queued projects, and the research step spent the pool on them.
    CHECK(research::totalLevels(r, s.empire(kA)) > before);

    // A player who forbade AI changes gets the bookkeeping only.
    processTurn(r, minimal, {});
    CHECK_FALSE(minimal.empire(kA).ministerAll);
    CHECK(minimal.empire(kA).research.empty());
    CHECK(research::totalLevels(r, minimal.empire(kA)) == before);
    CHECK(research::totalLevels(r, minimal.empire(kB)) > beforeB);  // B was covered as usual
}

TEST_CASE("turn order: the Politics minister plans alone and first; the other ministers send no messages") {
    // Spec 05 §8 step 4: a computer player's messages take effect as its
    // Politics minister sends them, before its other ministers give orders.
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 3, 12, false);
    for (const Empire& e : s.empires) {
        for (const Command& c : ai::planOrdersAfterPolitics(r, s, e.id)) {
            CHECK_FALSE(std::holds_alternative<cmd::SendMessage>(c));
            CHECK_FALSE(std::holds_alternative<cmd::AnswerMessage>(c));
        }
        for (const Command& c : ai::planPoliticsOrders(r, s, e.id)) CHECK_FALSE(std::holds_alternative<cmd::SetOrders>(c));
    }
    // A few turns: every processed turn keeps the rules.
    for (int i = 0; i < 3; ++i) processTurn(r, s, {});
    CHECK(s.turn == 3);
}

TEST_CASE("turn order: every vehicle comes from where it stands after its owner's processing") {
    // Step 16 (spec 05 §8): the sector a vehicle came from is reset to its
    // current sector, so nothing counts as an arrival between turns.
    const Rules& r = engineRules();
    GameState s = newEngineGame(11, 3, 12, false);
    for (int i = 0; i < 4; ++i) processTurn(r, s, {});
    for (const Vehicle& v : s.vehicles) {
        const ruleset::VehicleType type = vehicleType(r, s, v);
        if (type == ruleset::VehicleType::Ship || type == ruleset::VehicleType::Base) CHECK(v.cameFrom == v.location);
    }
}
