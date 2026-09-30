// Long-lived empire knowledge: the History window's record (spec 05 §3.4,
// §5) and the memory of foreign designs, forgotten 50 turns after they were
// last seen (spec 05 §8 step 12).

#include "combat_fixture.hpp"
#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/combat.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

void quietTurn(const Rules& r, GameState& s) {
    TurnOptions o;
    o.aiForMissing = false;
    processTurn(r, s, {}, o);
}

std::vector<const HistoryEntry*> about(const GameState& s, EmpireId owner, EmpireId subject) {
    std::vector<const HistoryEntry*> out;
    for (const HistoryEntry& h : s.empire(owner).historyEvents)
        if (h.empire == subject) out.push_back(&h);
    return out;
}

const HistoryEntry* findHistory(const GameState& s, EmpireId owner, std::string_view text) {
    for (const HistoryEntry& h : s.empire(owner).historyEvents)
        if (h.text.find(text) != std::string::npos) return &h;
    return nullptr;
}

// A design of `owner` that nothing uses.
DesignId spareDesign(GameState& s, const Rules& r, EmpireId owner, std::string_view name) {
    return addTestDesign(s, r, owner, name, "Test Frigate", {"Test Bridge"});
}

} // namespace

// ---- Seen designs ---------------------------------------------------------------------------------

TEST_CASE("history: a seen design keeps the turn of its latest sighting") {
    Knowledge k;
    seeDesign(k, DesignId{3u}, 10);
    seeDesign(k, DesignId{1u}, 20);
    seeDesign(k, DesignId{3u}, 5);  // an older report changes nothing
    CHECK(designSeenTurn(k, DesignId{3u}) == 10u);
    seeDesign(k, DesignId{3u}, 12);
    CHECK(designSeenTurn(k, DesignId{3u}) == 12u);
    CHECK(seenDesignIds(k) == std::vector<DesignId>{DesignId{1u}, DesignId{3u}});
    CHECK(knowsDesign(k, DesignId{1u}));
    CHECK_FALSE(knowsDesign(k, DesignId{2u}));
    CHECK_FALSE(designSeenTurn(k, DesignId{2u}).has_value());
    seeDesign(k, DesignId{}, 1);  // "no design" is never recorded
    CHECK(k.seenDesigns.size() == 2);
}

TEST_CASE("history: designs seen more than 50 turns ago are forgotten") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const DesignId old = spareDesign(s, r, kB, "Old Sighting");
    const DesignId recent = spareDesign(s, r, kB, "Recent Sighting");
    seeDesign(s.empire(kA).knowledge, old, 10);
    seeDesign(s.empire(kA).knowledge, recent, 11);
    s.turn = 60;
    CHECK(sight::forgetOldDesigns(s, kA) == 0);  // 50 turns ago is still remembered
    s.turn = 61;
    CHECK(sight::forgetOldDesigns(s, kA) == 1);
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, old));
    CHECK(knowsDesign(s.empire(kA).knowledge, recent));
}

TEST_CASE("turn order: step 12 forgets old designs in each empire's end-of-turn processing") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const DesignId gone = spareDesign(s, r, kB, "Faded");
    const DesignId kept = spareDesign(s, r, kB, "Remembered");
    s.turn = 70;
    seeDesign(s.empire(kA).knowledge, gone, 19);  // 51 turns ago
    seeDesign(s.empire(kA).knowledge, kept, 20);  // 50 turns ago
    quietTurn(r, s);
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, gone));
    CHECK(knowsDesign(s.empire(kA).knowledge, kept));
}

TEST_CASE("turn order: a forgotten obsolete design can be purged at the new year") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const DesignId faded = spareDesign(s, r, kB, "Faded Relic");
    const DesignId fresh = spareDesign(s, r, kB, "Fresh Relic");
    s.design(faded).obsolete = s.design(fresh).obsolete = true;
    s.turn = 69;  // the date reaches a new year in this turn: the design cleanup runs
    seeDesign(s.empire(kA).knowledge, faded, 10);
    seeDesign(s.empire(kA).knowledge, fresh, 30);
    quietTurn(r, s);
    const auto& own = s.empire(kB).designs;
    // A forgot the first at its step 12, before the cleanup; it still knows the second.
    CHECK(std::find(own.begin(), own.end(), faded) == own.end());
    CHECK(std::find(own.begin(), own.end(), fresh) != own.end());
}

TEST_CASE("history: shared designs keep the partner's date; a master dates its subject's designs by their creation") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kA, kC);
    TurnContext ctx = turnContext(r, s);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::Partnership);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);
    const DesignId cDesign = s.empire(kC).designs.front();
    s.turn = 40;
    seeDesign(s.empire(kB).knowledge, cDesign, 12);
    const DesignId cOther = spareDesign(s, r, kC, "Subject Hull");
    s.design(cOther).createdTurn = 35;
    const DesignId cBig = addTestDesign(s, r, kC, "Out Of Reach", "Test Cruiser", {"Test Bridge"});
    s.empire(kC).techLevels[techArea(r, "Test Construction").index()] = 1;  // the cruiser hull needs level 2
    diplomacy::treatyStep(ctx, kA);
    // A master learns the designs of its subject it does not know and the
    // subject can build, dated with their creation (spec 05 §8). From the
    // partner: as old as the partner's own sighting, so partners cannot keep a
    // design alive by passing it back and forth; the later date wins.
    CHECK(designSeenTurn(s.empire(kA).knowledge, cDesign) == 12u);
    CHECK(designSeenTurn(s.empire(kA).knowledge, cOther) == 35u);
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, cBig));
    // Created more than 50 turns ago: forgotten again at step 12 of the same processing.
    GameState old = s;
    old.turn = 90;
    sight::forgetOldDesigns(old, kA);
    CHECK_FALSE(knowsDesign(old.empire(kA).knowledge, cOther));

    // Without the subjugation, only the partner's date remains.
    GameState t = newPoliticsGame();
    setContact(t, kA, kB);
    TurnContext tctx = turnContext(r, t);
    diplomacy::setTreaty(tctx, kA, kB, Treaty::Partnership);
    const DesignId seenByB = t.empire(kC).designs.front();
    t.turn = 40;
    seeDesign(t.empire(kB).knowledge, seenByB, 12);
    diplomacy::treatyStep(tctx, kA);
    CHECK(designSeenTurn(t.empire(kA).knowledge, seenByB) == 12u);
    t.turn = 63;
    CHECK(sight::forgetOldDesigns(t, kA) == 1);
    CHECK(sight::forgetOldDesigns(t, kB) == 1);
}

TEST_CASE("history: a battle dates the pieces' designs and their cargo's; a mine strike the mine's") {
    // Spec 05 §8 "Design knowledge" (confirmed: binary).
    using namespace opense4::ctest;
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        s.turn = 9;
        const DesignId hunter = frigate(s, ar.a, "Hunter", 3, {"CT Big Gun", "CT Big Armor"});
        const DesignId carrier = frigate(s, ar.b, "Carrier", 1, {"Test Cargo Bay"});
        const DesignId sat = ctest::design(s, ar.b, "Cargo Sat", "Test Satellite Hull", {"Test Armor Plate"});
        spawn(s, hunter, ar.loc);
        const VehicleId c = spawn(s, carrier, ar.loc);
        s.vehicle(c)->cargo.units = {{sat, 1}};
        TurnContext ctx = ctest::context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(designSeenTurn(s.empire(ar.a).knowledge, carrier) == std::optional<uint32_t>(9));
        CHECK(designSeenTurn(s.empire(ar.a).knowledge, sat) == std::optional<uint32_t>(9));
        CHECK(designSeenTurn(s.empire(ar.b).knowledge, hunter) == std::optional<uint32_t>(9));
    }
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        s.turn = 4;
        const DesignId mine = ctest::design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        spawn(s, mine, ar.loc, 1);
        spawn(s, frigate(s, ar.a, "Victim", 1, {"Test Armor Plate", "Test Armor Plate", "Test Armor Plate"}), ar.loc);
        TurnContext ctx = ctest::context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(designSeenTurn(s.empire(ar.a).knowledge, mine) == std::optional<uint32_t>(4));
    }
}

// ---- The history record ------------------------------------------------------------------------------

TEST_CASE("history: contact and treaty changes are listed under the other empire") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    s.turn = 4;
    TurnContext ctx = turnContext(r, s);
    diplomacy::makeContact(ctx, kA, kB);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::TradeAlliance);
    s.turn = 5;
    diplomacy::declareWar(ctx, kB, kA);
    s.turn = 6;
    diplomacy::setTreaty(ctx, kA, kB, Treaty::None);

    for (auto [owner, other] : {std::pair{kA, kB}, std::pair{kB, kA}}) {
        const auto lines = about(s, owner, other);
        REQUIRE(lines.size() == 4);
        CHECK(lines[0]->text.starts_with("First contact with the"));
        CHECK(lines[0]->turn == 4);
        CHECK(lines[1]->text.starts_with("Trade Alliance with the"));
        CHECK(lines[2]->text.starts_with("War with the"));
        CHECK(lines[2]->turn == 5);
        CHECK(lines[3]->text.starts_with("Peace with the"));
        CHECK(lines[3]->turn == 6);
        CHECK(about(s, owner, EmpireId{}).empty());
    }
    CHECK(s.empire(kC).historyEvents.empty());

    // Dominance is part of the line.
    setContact(s, kA, kC);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Protectorate, true);
    CHECK(about(s, kA, kC).back()->text.ends_with("(we are the master)"));
    CHECK(about(s, kC, kA).back()->text.ends_with("(we are the subject)"));
}

TEST_CASE("history: the record outlives the log") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    TurnContext ctx = turnContext(r, s);
    diplomacy::makeContact(ctx, kA, kB);
    quietTurn(r, s);
    quietTurn(r, s);
    quietTurn(r, s);
    CHECK_FALSE(hasLog(s, kA, "First Contact"));  // the log keeps only the last turn
    const auto lines = about(s, kA, kB);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0]->turn == 0);
}

TEST_CASE("history: a lost colony is recorded with its place") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    s.turn = 7;
    const ObjectId home = homeworld(s, kA).planet;
    TurnContext ctx = turnContext(r, s);
    economy::colonyDiesOut(ctx, home, "plague");
    const HistoryEntry* h = findHistory(s, kA, "died out: plague");
    REQUIRE(h != nullptr);
    CHECK(h->empire == kA);
    CHECK(h->turn == 7);
    REQUIRE(h->location.has_value());
    CHECK(*h->location == locationOf(s.galaxy, home));
}

TEST_CASE("history: random events go to the General list with their place") {
    ruleset::Ruleset rs = buildPoliticsRuleset();
    ruleset::EventType ev;
    ev.type = std::string(effects::identifier(effects::Effect::ShipExperienceChange));
    ev.severity = "Low";
    ev.effectAmount = 1;
    ev.messageTo = "All";
    ev.messages = {{"Omen", "Something happened."}};
    rs.eventTypes = {ev};
    const Rules r{std::move(rs)};
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 3;  // a 100 % chance in the fixture
    s.turn = 19;                   // the first turn events may happen
    quietTurn(r, s);
    for (const Empire& e : s.empires) {
        const auto general = about(s, e.id, EmpireId{});
        REQUIRE(general.size() == 1);
        CHECK(general[0]->text == "Omen: Something happened.");
        CHECK(general[0]->turn == 19);
        CHECK(general[0]->location.has_value());
    }
}

TEST_CASE("history: each player's view holds only its own record and seen designs") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    TurnContext ctx = turnContext(r, s);
    diplomacy::makeContact(ctx, kA, kB);
    seeDesign(s.empire(kA).knowledge, s.empire(kB).designs.front(), 0);
    const GameState v = redactForEmpire(s, kA);
    CHECK(validateState(v, &r).empty());
    CHECK(v.empire(kA).historyEvents.size() == s.empire(kA).historyEvents.size());
    CHECK(v.empire(kB).historyEvents.empty());
    CHECK(knowsDesign(v.empire(kA).knowledge, s.empire(kB).designs.front()));
    const GameState spectator = redactForEmpire(s, EmpireId{});
    for (const Empire& e : spectator.empires) CHECK(e.historyEvents.empty());
}

TEST_CASE("history: the record and the design dates are saved") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    TurnContext ctx = turnContext(r, s);
    diplomacy::makeContact(ctx, kA, kB);
    addHistory(s, kA, EmpireId{}, "A comet passed", Location{SystemId{0u}, Sector{1, 2}});
    addHistory(s, kA, EmpireId{}, "Nowhere", Location{SystemId{0u}, Sector{-1, 2}});  // an invalid place is dropped
    seeDesign(s.empire(kA).knowledge, s.empire(kB).designs.front(), 3);
    auto back = deserializeState(serializeState(s));
    REQUIRE(back.has_value());
    CHECK(validateState(*back, &r).empty());
    const auto& h = back->empire(kA).historyEvents;
    REQUIRE(h.size() == 3);
    CHECK(h[0].empire == kB);
    CHECK(h[1].text == "A comet passed");
    CHECK(h[1].location == Location{SystemId{0u}, Sector{1, 2}});
    CHECK_FALSE(h[2].location.has_value());
    CHECK(designSeenTurn(back->empire(kA).knowledge, s.empire(kB).designs.front()) == 3u);

    // A record about a missing empire or a missing design is refused.
    GameState bad = s;
    bad.empire(kA).historyEvents.push_back({0, EmpireId{9u}, "?", std::nullopt});
    CHECK_FALSE(validateState(bad, &r).empty());
    bad = s;
    bad.empire(kA).knowledge.seenDesigns.push_back({DesignId{static_cast<uint32_t>(s.designs.size())}, 0});
    CHECK_FALSE(validateState(bad, &r).empty());
}
