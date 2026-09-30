// Intelligence: project lifecycle, counter-intelligence and the espionage,
// theft, defection and political handlers (docs/spec/05 §2).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using effects::Effect;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

TurnContext context(GameState& s) { return turnContext(politicsRules(), s); }

IntelProjectOrder order(Effect e, EmpireId target) {
    IntelProjectOrder o;
    o.project = projectFor(e);
    o.target = target;
    return o;
}

uint32_t defenseProject(int level) {
    const auto& list = politicsRules().data().intelProjects;
    for (uint32_t i = 0; i < list.size(); ++i)
        if (list[i].type == "Intelligence Defense" && list[i].effectAmount == level) return i;
    FAIL("no defense project");
    return 0;
}

int totalDamage(const GameState& s, EmpireId owner) {
    int n = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == owner) n += vehicleDamageTaken(s, v);
    return n;
}

effects::Outcome run(GameState& s, Effect e, effects::Target request, int amount = 1, uint64_t seed = 3) {
    Rng rng(seed);
    request.source = request.source.valid() ? request.source : kA;
    request.empire = request.empire.valid() ? request.empire : kB;
    auto t = effects::pickTarget(politicsRules(), s, e, request, rng);
    REQUIRE_MESSAGE(t.has_value(), effects::identifier(e));
    TurnContext ctx = context(s);
    return effects::apply(ctx, e, *t, amount, rng);
}

} // namespace

TEST_CASE("intel: every stock-style type identifier round-trips") {
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        CHECK(effects::parseEffect(effects::identifier(e)) == e);
    }
    CHECK(effects::parseEffect("ship  -  DAMAGE") == Effect::ShipDamage);
    CHECK_FALSE(effects::parseEffect("Something From A Mod").has_value());
    CHECK(effects::isSabotage(Effect::PointsSteal));
    CHECK_FALSE(effects::isSabotage(Effect::ResearchSteal));
    CHECK(effects::needsSource(Effect::PoliticsTreatyInfo));
    CHECK_FALSE(effects::needsSource(Effect::PlanetPopulationRebel));
}

TEST_CASE("intel: defense points and attack strength") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Empire& b = s.empire(kB);
    b.intel.push_back({defenseProject(2), {}, {}, {}, {}, 1000});
    b.intel.push_back(order(Effect::ShipDamage, kA));
    b.intel.back().progress = 999;
    CHECK(intel::defensePoints(r, s, kB) == 1000 * 2 * 120 / 100);
    CHECK(intel::isDefense(r, defenseProject(1)));
    CHECK_FALSE(intel::isDefense(r, projectFor(Effect::ShipDamage)));
    IntelProjectOrder o = order(Effect::ShipDamage, kB);
    CHECK(intel::attackStrength(r, o) == 1000 * 125 / 100);  // "Any" target
    o.targetVehicle = s.vehicles.front().id;
    CHECK(intel::attackStrength(r, o) == 1000);
}

TEST_CASE("intel: a funded project runs, logs both sides and leaves the queue") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB)};
    a.economy.intelligence = 1000;
    TurnContext ctx = context(s);
    intel::runIntel(ctx);
    CHECK(a.intel.empty());
    CHECK(totalDamage(s, kB) == 1);
    const LogEntry* src = findLog(s, kA, politicsRules().data().intelProjects[projectFor(Effect::ShipDamage)].name);
    REQUIRE(src);
    CHECK(src->category == LogCategory::Intelligence);
    CHECK(src->text.starts_with("Intelligence Minister: Done to Realm 2 Union: "));
    CHECK(src->picture == "ProbeUs");
    const LogEntry* tgt = findLog(s, kB, "Probe Hit");
    REQUIRE(tgt);
    CHECK(tgt->text.starts_with("Intelligence Minister: They got "));
    CHECK(tgt->picture == "ProbeThem");
    CHECK(tgt->text.find("[%") == std::string::npos);
}

TEST_CASE("intel: even funding, repeat and invalid targets") {
    GameState s = newPoliticsGame();
    Empire& a = s.empire(kA);
    TurnContext ctx = context(s);

    // Two projects share the points evenly: nothing finishes yet.
    setContact(s, kA, kB);
    a.intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB)};
    a.intelEvenly = true;
    a.economy.intelligence = 1000;
    intel::runIntel(ctx);
    REQUIRE(a.intel.size() == 2);
    CHECK(a.intel[0].progress == 500);
    CHECK(a.intel[1].progress == 500);

    // Repeat: the finished project stays and starts over.
    a.repeatIntel = true;
    a.intel = {order(Effect::ShipDamage, kB)};
    intel::runIntel(ctx);
    REQUIRE(a.intel.size() == 1);
    CHECK(a.intel[0].progress == 0);
    CHECK(totalDamage(s, kB) == 1);

    // Without contact the operation cannot proceed; it is used up.
    a.repeatIntel = false;
    a.intel = {order(Effect::ShipDamage, kC)};
    s.empire(kA).log.clear();
    intel::runIntel(ctx);
    CHECK(a.intel.empty());
    REQUIRE_FALSE(s.empire(kA).log.empty());
    CHECK(s.empire(kA).log.back().text.find("contact") != std::string::npos);
    CHECK(totalDamage(s, kC) == 0);
}

TEST_CASE("intel: counter-intelligence defeats attacks and is drained") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& b = s.empire(kB);
    b.intel = {{defenseProject(3), {}, {}, {}, {}, 10000000}};
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB)};
    a.economy.intelligence = 1000;
    TurnContext ctx = context(s);
    intel::runIntel(ctx);
    CHECK(a.intel.empty());
    CHECK(totalDamage(s, kB) == 0);
    CHECK(b.intel[0].progress == 10000000 - 1250);
    const LogEntry* defended = findLog(s, kB, "Shield Level 3");
    REQUIRE(defended);
    CHECK(defended->text == "Intelligence Minister: Stopped the Realm 1 Union.");
    const LogEntry* stopped = findLog(s, kA, "Probe Stopped");
    REQUIRE(stopped);
    CHECK(stopped->text == "Intelligence Minister: Our probe against the Realm 2 Union was stopped.");
}

TEST_CASE("intel: the bad-intelligence ability of the target system foils operations") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    homeworld(s, kB).facilities.push_back(facilityIndex(r, "Test Security Center"));  // -100 %
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB)};
    a.economy.intelligence = 1000;
    TurnContext ctx = context(s);
    intel::runIntel(ctx);
    CHECK(totalDamage(s, kB) == 0);
    CHECK(hasLog(s, kB, "Operation Foiled"));
    CHECK(a.intel.empty());
}

TEST_CASE("intel: disabled by the game option") {
    GameState s = newPoliticsGame();
    s.options.allowIntel = false;
    setContact(s, kA, kB);
    s.empire(kA).intel = {order(Effect::ShipDamage, kB)};
    s.empire(kA).economy.intelligence = 1000;
    TurnContext ctx = context(s);
    intel::runIntel(ctx);
    CHECK(s.empire(kA).intel.size() == 1);
    CHECK(s.empire(kA).intel[0].progress == 0);
    CHECK_FALSE(intel::orderProblem(politicsRules(), s, kA, s.empire(kA).intel[0]).empty());
}

TEST_CASE("intel: theft of technology, resources and designs") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const auto beams = techArea(r, "Test Beams");

    // Research - Steal: one level beyond ours in an area where they lead.
    s.empire(kB).techLevels = s.empire(kA).techLevels;
    s.empire(kB).techLevels[beams.index()] = 6;
    auto out = run(s, Effect::ResearchSteal, {});
    CHECK(out.applied);
    CHECK(s.empire(kA).techLevel(beams) == 2);
    CHECK(out.tokens.techName == "Test Beams");
    s.empire(kB).techLevels = s.empire(kA).techLevels;
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchSteal, {kB, kA, {}, {}, {}, {}}, rng).has_value());

    // Points - Steal: up to the amount of each resource.
    s.empire(kB).stockpile = {500, 20000, 0};
    const Resources before = s.empire(kA).stockpile;
    out = run(s, Effect::PointsSteal, {}, 1000);
    CHECK(out.actual == 1500);
    CHECK(s.empire(kB).stockpile == Resources{0, 19000, 0});
    CHECK(s.empire(kA).stockpile == before + Resources{500, 1000, 0});

    // Ship Designs - Steal: a copy joins our designs; we know the original.
    const size_t designs = s.empire(kA).designs.size();
    out = run(s, Effect::ShipDesignsSteal, {});
    REQUIRE(out.applied);
    CHECK(s.empire(kA).designs.size() == designs + 1);
    const Design& copy = s.design(s.empire(kA).designs.back());
    CHECK(copy.owner == kA);
    CHECK(copy.name.starts_with(out.tokens.designName));
    CHECK_FALSE(s.empire(kA).knowledge.seenDesigns.empty());

    // Unit Designs - Steal needs a unit design.
    CHECK_FALSE(effects::pickTarget(r, s, Effect::UnitDesignsSteal, {kB, kA, {}, {}, {}, {}}, rng).has_value());
    addTestDesign(s, r, kB, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    out = run(s, Effect::UnitDesignsSteal, {});
    CHECK(out.applied);
    CHECK(out.tokens.designName == "Wasp");
}

TEST_CASE("intel: ships and planets defect to the source") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    std::vector<VehicleId> theirs;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kB) theirs.push_back(v.id);
    REQUIRE(theirs.size() >= 2);
    REQUIRE(apply(r, s, kB, cmd::CreateFleet{"Pair", theirs}).ok);

    effects::Target t;
    t.vehicle = theirs[0];
    Rng rng(1);
    auto picked = effects::pickTarget(r, s, Effect::ShipRebel, {kB, kA, {}, theirs[0], {}, {}}, rng);
    REQUIRE(picked);
    TurnContext ctx = context(s);
    auto out = effects::apply(ctx, Effect::ShipRebel, *picked, 1, rng);
    CHECK(out.applied);
    const Vehicle* v = s.vehicle(theirs[0]);
    CHECK(v->owner == kA);
    CHECK_FALSE(v->fleet.valid());
    CHECK(v->orders.empty());
    CHECK(s.fleets.size() == 1);
    CHECK(std::find(s.fleets[0].members.begin(), s.fleets[0].members.end(), theirs[0]) == s.fleets[0].members.end());
    CHECK(std::binary_search(s.empire(kA).knowledge.seenDesigns.begin(), s.empire(kA).knowledge.seenDesigns.end(), v->design));
    CHECK(hasMood(ctx, kB, "Any Ship Lost"));

    // Population Rebel: the homeworld is never picked as "Any"...
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPopulationRebel, {kB, kA, {}, {}, {}, {}}, rng).has_value());
    // ...but can be targeted on purpose.
    const ObjectId home = homeworld(s, kB).planet;
    picked = effects::pickTarget(r, s, Effect::PlanetPopulationRebel, {kB, kA, {}, {}, home, {}}, rng);
    REQUIRE(picked);
    out = effects::apply(ctx, Effect::PlanetPopulationRebel, *picked, 1, rng);
    CHECK(out.applied);
    CHECK(s.colony(home)->owner == kA);
    CHECK_FALSE(s.colony(home)->homeworld);
    CHECK(hasMood(ctx, kB, "Homeworld Lost"));
    CHECK(s.empire(kA).hasExplored(s.galaxy.object(home).system));
}

TEST_CASE("intel: political operations") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);

    // Disrupt Trade needs real trade between the target and a third empire.
    Rng rng(2);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PoliticsDisruptTrade, {kB, kA, {}, {}, {}, {}}, rng).has_value());
    diplomacy::setTreaty(ctx, kB, kC, Treaty::TradeAlliance);
    s.empire(kB).relation(kC).tradePercent = s.empire(kC).relation(kB).tradePercent = 12;
    auto out = run(s, Effect::PoliticsDisruptTrade, {});
    CHECK(out.applied);
    CHECK(out.actual == 12);
    CHECK(s.empire(kB).relation(kC).tradePercent == 0);
    CHECK(s.empire(kC).relation(kB).tradePercent == 0);

    // Treaty Info names the treaty.
    out = run(s, Effect::PoliticsTreatyInfo, {kB, kA, kC, {}, {}, {}});
    CHECK(out.tokens.treatyName == "Trade Alliance");
    REQUIRE_FALSE(out.report.empty());

    // Intercept Messages reports what passed between them.
    DiplomaticMessage m;
    m.type = MessageType::General;
    m.text = "Secret plans";
    m.to = kC;
    REQUIRE(apply(r, s, kB, cmd::SendMessage{m}).ok);
    diplomacy::deliverMessages(ctx);
    out = run(s, Effect::PoliticsInterceptMessages, {kB, kA, kC, {}, {}, {}});
    REQUIRE(out.report.size() == 1);
    CHECK(out.report[0].find("Secret plans") != std::string::npos);

    // Fake Messages: a forged demand from the target reaches the third empire.
    const size_t messages = s.messages.size();
    out = run(s, Effect::PoliticsFakeMessages, {kB, kA, kC, {}, {}, {}});
    REQUIRE(s.messages.size() == messages + 1);
    CHECK(s.messages.back().from == kB);
    CHECK(s.messages.back().to == kC);
    CHECK(s.messages.back().delivered);
    CHECK(hasLog(s, kC, std::string(displayName(MessageType::DemandTribute))));

    // Prevent Messages: the next turn's messages between them are lost, but
    // a declaration of war still goes through.
    out = run(s, Effect::PoliticsPreventMessages, {kB, kA, kC, {}, {}, {}}, 1);
    CHECK(s.empire(kB).relation(kC).messagesBlockedUntil == s.turn + 2);
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    ++s.turn;
    m.text = "Hello?";
    REQUIRE(apply(r, s, kB, cmd::SendMessage{m}).ok);
    DiplomaticMessage war;
    war.type = MessageType::DeclareWar;
    war.to = kB;
    REQUIRE(apply(r, s, kC, cmd::SendMessage{war}).ok);
    diplomacy::deliverMessages(ctx);
    CHECK(std::none_of(s.messages.begin(), s.messages.end(), [](const DiplomaticMessage& x) { return x.text == "Hello?"; }));
    CHECK(s.empire(kB).relation(kC).treaty == Treaty::War);
}

TEST_CASE("intel: espionage reports") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const SystemId theirHome = s.galaxy.object(homeworld(s, kB).planet).system;
    REQUIRE_FALSE(s.empire(kA).hasExplored(theirHome));

    auto out = run(s, Effect::PlanetLocations, {});
    CHECK(out.actual == 1);
    CHECK(s.empire(kA).hasExplored(theirHome));

    out = run(s, Effect::PlanetInfo, {kB, kA, {}, {}, homeworld(s, kB).planet, {}});
    CHECK(out.report.size() >= 4);
    CHECK(out.tokens.planetName == s.galaxy.object(homeworld(s, kB).planet).name);

    GameState t = newPoliticsGame();
    out = run(t, Effect::SystemInfo, {});
    CHECK(out.applied);
    CHECK(t.empire(kA).hasExplored(t.galaxy.object(homeworld(t, kB).planet).system));

    for (Effect e : {Effect::EmpireInfo, Effect::TechLevelInfo, Effect::ShipLocations, Effect::ShipConcentrations,
                     Effect::ShipConstructionInfo}) {
        out = run(s, e, {});
        CHECK(out.applied);
        CHECK_FALSE(out.report.empty());
    }
    out = run(s, Effect::ShipConcentrations, {});
    CHECK(out.report[0].find("3 ships") != std::string::npos);  // two scouts and a colony ship at home
    (void)r;
}

TEST_CASE("intel: effects that need a source cannot be events") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PointsSteal, {kB, {}, {}, {}, {}, {}}, rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchSteal, {kB, kB, {}, {}, {}, {}}, rng).has_value());  // not self
    CHECK_FALSE(effects::pickTarget(r, s, Effect::IntelligenceDefense, {kB, kA, {}, {}, {}, {}}, rng).has_value());
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        if (effects::needsSource(e)) CHECK_FALSE(effects::pickTarget(r, s, e, {kB, {}, {}, {}, {}, {}}, rng).has_value());
    }
}

TEST_CASE("intel: turns are deterministic") {
    auto play = [] {
        GameState s = newPoliticsGame(17);
        setContact(s, kA, kB);
        setContact(s, kB, kC);
        std::vector<std::string> titles;
        for (int turn = 0; turn < 6; ++turn) {
            for (Empire& e : s.empires) e.economy.intelligence = 1500;
            s.empire(kA).intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB), order(Effect::PlanetFacilityDamage, kB)};
            s.empire(kC).intel = {order(Effect::ShipExperienceChange, kB)};
            s.empire(kB).intel = {{defenseProject(1), {}, {}, {}, {}, 500}};
            TurnContext ctx = context(s);
            intel::runIntel(ctx);
            ++s.turn;
        }
        for (const Empire& e : s.empires)
            for (const LogEntry& l : e.log) titles.push_back(l.title + "|" + l.text);
        return std::pair{titles, s.rng};
    };
    const auto a = play();
    const auto b = play();
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}
