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
#include "game/research.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

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

IntelProjectOrder defense(int level, int64_t progress) {
    IntelProjectOrder o;
    o.project = defenseProject(level);
    o.progress = progress;
    return o;
}

effects::Target target(EmpireId empire, EmpireId source) {
    effects::Target t;
    t.empire = empire;
    t.source = source;
    return t;
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

// Funds and runs one empire's intelligence step with `points` in its pool.
void step(GameState& s, EmpireId e, int64_t points) {
    s.empire(e).intelPool = points;
    TurnContext ctx = context(s);
    intel::intelStep(ctx, e);
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

TEST_CASE("intel: defense points") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Empire& b = s.empire(kB);
    b.intel.push_back(defense(2, 1000));
    b.intel.push_back(order(Effect::ShipDamage, kA));
    b.intel.back().progress = 999;
    // trunc(Amount × progress × `Intelligence Defense Modifier Percent` / 100).
    CHECK(intel::defensePoints(r, s, kB) == xmath::pctTrunc(2 * 1000, 120));
    CHECK(intel::isDefense(r, defenseProject(1)));
    CHECK_FALSE(intel::isDefense(r, projectFor(Effect::ShipDamage)));
    CHECK(intel::requirementLevel(r, projectFor(Effect::ShipDamage)) == 0);
}

TEST_CASE("intel: the pool is spent at the step and emptied") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB)};
    a.intelEvenly = false;
    s.empire(kA).intelPool = 400;
    TurnContext ctx = context(s);
    intel::intelStep(ctx, kA);
    // Last turn's 400 points were spent and the pool is empty; this turn's
    // income (the economy's income step, research::addToPools) waits for the
    // next step.
    CHECK(a.intel[0].progress == 400);
    CHECK(a.intelPool == 0);
    research::addToPools(a, 0, 700);
    CHECK(a.intelPool == 700);
    intel::intelStep(ctx, kA);
    CHECK(a.intelPool == 0);
}

TEST_CASE("intel: a funded project runs, logs both sides and leaves the queue") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB)};
    step(s, kA, 1000);
    CHECK(a.intel.empty());
    CHECK(a.intelPool == 0);
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

TEST_CASE("intel: Planet - Conditions Change tells the victim like any other effect (spec 05 open question 39)") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    IntelProjectOrder o = order(Effect::PlanetConditionsChange, kB);
    o.targetPlanet = homeworld(s, kB).planet;
    s.empire(kA).intel = {o};
    step(s, kA, 1000);
    CHECK(s.empire(kA).intel.empty());
    CHECK(findLog(s, kA, politicsRules().data().intelProjects[projectFor(Effect::PlanetConditionsChange)].name) != nullptr);
    const LogEntry* tgt = findLog(s, kB, "Probe Hit");
    REQUIRE(tgt);
    CHECK(tgt->text.starts_with("Intelligence Minister: They got "));
}

TEST_CASE("intel: there is no success roll, and the source is named one time in five") {
    int named = 0, runs = 0;
    const GameState base = newPoliticsGame(5);
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        GameState s = base;
        s.rng.reseed(seed);
        setContact(s, kA, kB);
        s.empire(kA).intel = {order(Effect::PointsChange, kB)};
        s.empire(kB).stockpile = {5000, 5000, 5000};
        step(s, kA, 1000);
        REQUIRE(s.empire(kB).stockpile == Resources{5001, 5001, 5001});  // always succeeds
        const LogEntry* hit = findLog(s, kB, "Probe Hit");
        REQUIRE(hit);
        ++runs;
        if (hit->text.find("Evidence points to the Realm 1 Union") != std::string::npos) ++named;
    }
    CHECK(named > runs * 12 / 100);
    CHECK(named < runs * 28 / 100);
}

TEST_CASE("intel: even funding rounds the pool share and does not cap it") {
    GameState s = newPoliticsGame();
    Empire& a = s.empire(kA);
    setContact(s, kA, kB);

    // Two projects share the points evenly: nothing finishes yet.
    a.intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB)};
    a.intelEvenly = true;
    step(s, kA, 1001);  // round(500.5) = 500, ties to even
    REQUIRE(a.intel.size() == 2);
    CHECK(a.intel[0].progress == 500);
    CHECK(a.intel[1].progress == 500);
    step(s, kA, 1003);  // round(501.5) = 502
    CHECK(a.intel.empty());  // both reached their Cost of 1000 and ran

    // Repeat: the finished project stays and starts over.
    a.repeatIntel = true;
    a.intel = {order(Effect::ShipDamage, kB)};
    const int before = totalDamage(s, kB);
    step(s, kA, 1000);
    REQUIRE(a.intel.size() == 1);
    CHECK(a.intel[0].progress == 0);
    CHECK(totalDamage(s, kB) == before + 1);

    // Without contact with the target the operation fails; it is used up.
    a.repeatIntel = false;
    a.intel = {order(Effect::ShipDamage, kC)};
    s.empire(kA).log.clear();
    step(s, kA, 1000);
    CHECK(a.intel.empty());
    REQUIRE_FALSE(s.empire(kA).log.empty());
    CHECK(s.empire(kA).log.back().text.find("contact") != std::string::npos);
    CHECK(totalDamage(s, kC) == 0);
}

TEST_CASE("intel: an empire in contact with nobody loses its queue; projects against the dead go") {
    GameState s = newPoliticsGame();
    Empire& a = s.empire(kA);
    a.intel = {order(Effect::ShipDamage, kB), defense(1, 10)};
    step(s, kA, 0);
    CHECK(a.intel.empty());

    setContact(s, kA, kB);
    setContact(s, kA, kC);
    a.intel = {order(Effect::ShipDamage, kB), order(Effect::ShipDamage, kC)};
    s.empire(kC).alive = false;
    step(s, kA, 0);
    REQUIRE(a.intel.size() == 1);
    CHECK(a.intel[0].target == kB);
}

TEST_CASE("intel: counter-intelligence drains defenses bottom-up and defeats attacks") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& b = s.empire(kB);
    Empire& a = s.empire(kA);

    // One strong defense: the attack is defeated, the defense keeps the surplus.
    b.intel = {defense(3, 10000000)};
    a.intel = {order(Effect::ShipDamage, kB)};
    step(s, kA, 1000);
    CHECK(a.intel.empty());
    CHECK(totalDamage(s, kB) == 0);
    // D = trunc(3 × 10,000,000 × 1.2) = 36,000,000; keeps trunc((D − 1000) / 3 / 1.2).
    CHECK(b.intel[0].progress == (xmath::Ext(36000000 - 1000) / xmath::Ext(3) / xmath::percent(120)).trunc());
    CHECK(b.intel[0].progress == 9999722);
    const LogEntry* defended = findLog(s, kB, "Shield Level 3");
    REQUIRE(defended);
    CHECK(defended->text == "Intelligence Minister: Stopped the Realm 1 Union.");
    const LogEntry* stopped = findLog(s, kA, "Probe Stopped");
    REQUIRE(stopped);
    CHECK(stopped->text == "Intelligence Minister: Our probe against the Realm 2 Union was stopped.");

    // Taken from the bottom of the queue upwards: the last defense is drained
    // first, and the walk stops once the attack is beaten.
    b.intel = {defense(1, 500), defense(1, 300), defense(1, 400)};
    CHECK(intel::counterIntelligence(politicsRules(), s, kB, 700) == 1);
    CHECK(b.intel[2].progress == 0);    // 480 counted, all used
    CHECK(b.intel[1].progress == 116);  // 480 + 360 = 840 ≥ 700: keeps trunc(140 / 1 / 1.2)
    CHECK(b.intel[0].progress == 500);  // never reached
    // The surplus converted back: trunc((360 − 1) / 3 / 1.2) = 99.
    b.intel = {defense(3, 100)};
    CHECK(intel::counterIntelligence(politicsRules(), s, kB, 1) == 0);
    CHECK(b.intel[0].progress == 99);
}

TEST_CASE("intel: an attack that beats every defense goes ahead, draining them all") {
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    Empire& b = s.empire(kB);
    b.intel = {defense(1, 100), order(Effect::ShipDamage, kA), defense(2, 200)};
    CHECK(intel::counterIntelligence(politicsRules(), s, kB, 1000000) == -1);
    CHECK(b.intel[0].progress == 0);
    CHECK(b.intel[2].progress == 0);

    s.empire(kA).intel = {order(Effect::ShipDamage, kB)};
    b.intel = {defense(1, 100)};
    step(s, kA, 1000);
    CHECK(totalDamage(s, kB) == 1);
    CHECK(b.intel[0].progress == 0);
}

TEST_CASE("intel: a finished defense deletes one hostile project within its level") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    s.empire(kA).intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB)};
    s.empire(kC).intel = {order(Effect::ShipDamage, kB)};
    const IntelProjectOrder shield = defense(1, r.data().intelProjects[defenseProject(1)].cost);
    s.empire(kB).intel = {shield};
    step(s, kB, 0);
    CHECK(s.empire(kB).intel.empty());  // it ran and left the queue
    const size_t left = s.empire(kA).intel.size() + s.empire(kC).intel.size();
    CHECK(left == 2);  // exactly one hostile project was deleted

    // Repeat keeps it in place, starting over.
    s.empire(kB).repeatIntel = true;
    s.empire(kB).intel = {shield};
    step(s, kB, 0);
    REQUIRE(s.empire(kB).intel.size() == 1);
    CHECK(s.empire(kB).intel[0].progress == 0);
    CHECK(s.empire(kA).intel.size() + s.empire(kC).intel.size() == 1);
}

TEST_CASE("intel: a finished defense compares the sum of a project's requirement levels") {
    // A project that needs two areas at level 1 has a requirement of 2 (spec
    // 05 §2.4): a level-1 defense cannot delete it, a level-2 one can.
    ruleset::Ruleset rs = buildPoliticsRuleset();
    const uint32_t probe = projectFor(Effect::ShipDamage);
    rs.intelProjects[probe].requirements = {{*rs.findTechArea("Test Physics"), 1}, {*rs.findTechArea("Test Espionage"), 1}};
    const Rules r{std::move(rs)};
    CHECK(intel::requirementLevel(r, probe) == 2);
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    for (int level : {1, 2}) {
        s.empire(kA).intel = {order(Effect::ShipDamage, kB)};
        s.empire(kB).intel = {defense(level, r.data().intelProjects[defenseProject(level)].cost)};
        TurnContext ctx = turnContext(r, s);
        intel::intelStep(ctx, kB);
        CHECK(s.empire(kA).intel.size() == (level == 1 ? 1u : 0u));
    }
}

TEST_CASE("intel: the bad-intelligence ability counts only unowned objects and positive values") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    // A colony facility with -100: it belongs to an empire, so it never counts.
    homeworld(s, kB).facilities.push_back(facilityIndex(r, "Test Security Center"));
    const SystemId home = s.galaxy.object(homeworld(s, kB).planet).system;
    CHECK(effects::unownedChanceValue(s, home, AbilityKind::ChangeBadIntelChanceSystem) == 0);
    s.empire(kA).intel = {order(Effect::ShipDamage, kB)};
    step(s, kA, 1000);
    CHECK(totalDamage(s, kB) == 1);

    // A system ability of 100 changes nothing; a positive value rejects
    // candidates with that chance.
    ruleset::Ability a;
    a.type = std::string(identifier(AbilityKind::ChangeBadIntelChanceSystem));
    a.value1 = "100";
    s.galaxy.system(home).abilities.push_back(a);
    CHECK(effects::unownedChanceValue(s, home, AbilityKind::ChangeBadIntelChanceSystem) == 100);
    Rng rng(9);
    int rejected = 0;
    for (int i = 0; i < 1000; ++i) rejected += effects::chanceRejects(100, rng) ? 1 : 0;
    CHECK(rejected == 0);
    for (int i = 0; i < 1000; ++i) rejected += effects::chanceRejects(30, rng) ? 1 : 0;
    CHECK(rejected > 230);
    CHECK(rejected < 370);
    CHECK_FALSE(effects::chanceRejects(-100, rng));
    CHECK_FALSE(effects::chanceRejects(0, rng));
}

TEST_CASE("intel: disabled by the game option") {
    GameState s = newPoliticsGame();
    s.options.allowIntel = false;
    setContact(s, kA, kB);
    s.empire(kA).intel = {order(Effect::ShipDamage, kB)};
    step(s, kA, 1000);
    CHECK(s.empire(kA).intel.size() == 1);
    CHECK(s.empire(kA).intel[0].progress == 0);
    CHECK(s.empire(kA).intelPool == 1000);  // the step is skipped entirely
    CHECK_FALSE(intel::orderProblem(politicsRules(), s, kA, s.empire(kA).intel[0]).empty());
}

TEST_CASE("intel: theft of technology, resources and designs") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const auto beams = techArea(r, "Test Beams");
    const auto armor = techArea(r, "Test Armor");

    // Research - Steal on a chosen area where they lead: exactly one level.
    s.empire(kB).techLevels = s.empire(kA).techLevels;
    s.empire(kB).techLevels[beams.index()] = 6;
    effects::Target request = target(kB, kA);
    request.tech = beams;
    auto out = run(s, Effect::ResearchSteal, request);
    CHECK(out.applied);
    CHECK(s.empire(kA).techLevel(beams) == 2);
    CHECK(out.tokens.techName == "Test Beams");
    // An area where they do not lead: nothing.
    request.tech = armor;
    out = run(s, Effect::ResearchSteal, request);
    CHECK_FALSE(out.applied);

    // "Any": the operatives pick an area where we already lead, so the steal
    // always fails (a quirk of the original, kept on purpose).
    s.empire(kA).techLevels[armor.index()] = 3;
    out = run(s, Effect::ResearchSteal, target(kB, kA));
    CHECK_FALSE(out.applied);
    CHECK(s.empire(kA).techLevel(armor) == 3);
    CHECK(s.empire(kA).techLevel(beams) == 2);
    s.empire(kA).techLevels = s.empire(kB).techLevels;
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchSteal, target(kB, kA), rng).has_value());

    // Points - Steal: up to the amount of each resource.
    s.empire(kB).stockpile = {500, 20000, 0};
    const Resources before = s.empire(kA).stockpile;
    out = run(s, Effect::PointsSteal, {}, 1000);
    CHECK(out.actual == 1500);
    CHECK(s.empire(kB).stockpile == Resources{0, 19000, 0});
    CHECK(s.empire(kA).stockpile == before + Resources{500, 1000, 0});

    // Ship Designs - Steal: we learn the newest design built at least once
    // (a queue completed one, or a ship was retrofitted to it) that we do not
    // know and they can build; nothing joins our own designs (spec 05 §2.3,
    // open question 41). The starting ships do not mark their designs.
    const size_t designs = s.empire(kA).designs.size();
    const size_t allDesigns = s.designs.size();
    std::vector<DesignId> built;
    for (DesignId d : s.empire(kB).designs)
        if (s.design(d).built > 0 && !isUnitType(r.hull(s.design(d).hull).type)) built.push_back(d);
    REQUIRE(built.size() >= 2);
    CHECK_FALSE(run(s, Effect::ShipDesignsSteal, {}).applied);
    for (DesignId d : built) s.design(d).everBuilt = true;
    resetDesignStatistics(s.design(built.front()));  // no statistics reset clears the mark
    CHECK(s.design(built.front()).everBuilt);
    const DesignId unbuilt = addTestDesign(s, r, kB, "Paper Ship", "Test Frigate", {"Test Bridge"});
    out = run(s, Effect::ShipDesignsSteal, {});
    REQUIRE(out.applied);
    CHECK(s.empire(kA).designs.size() == designs);
    CHECK(s.designs.size() == allDesigns + 1);
    CHECK(out.tokens.designName == s.design(built.back()).name);
    CHECK(designSeenTurn(s.empire(kA).knowledge, built.back()) == std::optional<uint32_t>(s.turn));
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, unbuilt));
    // The next theft takes the next newest; one they can no longer build is skipped.
    const DesignId cruiser = addTestDesign(s, r, kB, "Big Ship", "Test Cruiser", {"Test Bridge"});
    s.design(cruiser).everBuilt = true;
    s.empire(kB).techLevels[techArea(r, "Test Construction").index()] = 1;  // the cruiser hull needs level 2
    out = run(s, Effect::ShipDesignsSteal, {});
    REQUIRE(out.applied);
    CHECK(out.tokens.designName == s.design(built[built.size() - 2]).name);
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, cruiser));

    // Unit Designs - Steal needs a unit design that was built.
    const DesignId wasp = addTestDesign(s, r, kB, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    s.empire(kB).techLevels = s.empire(kA).techLevels;
    CHECK_FALSE(run(s, Effect::UnitDesignsSteal, {}).applied);
    s.design(wasp).everBuilt = true;
    out = run(s, Effect::UnitDesignsSteal, {});
    CHECK(out.applied);
    CHECK(out.tokens.designName == "Wasp");
    CHECK(knowsDesign(s.empire(kA).knowledge, wasp));
    CHECK_FALSE(run(s, Effect::UnitDesignsSteal, {}).applied);  // nothing left we do not know
}

TEST_CASE("intel: ships defect to the source") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    std::vector<VehicleId> theirs;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kB) theirs.push_back(v.id);
    REQUIRE(theirs.size() >= 2);
    REQUIRE(apply(r, s, kB, cmd::CreateFleet{"Pair", theirs}).ok);

    Rng rng(1);
    effects::Target request = target(kB, kA);
    request.vehicle = theirs[0];
    auto picked = effects::pickTarget(r, s, Effect::ShipRebel, request, rng);
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
    // A defection is not one of the ways a design is learned (spec 05 §8).
    CHECK_FALSE(knowsDesign(s.empire(kA).knowledge, v->design));
    CHECK(hasMood(ctx, kB, "Any Ship Lost"));
}

TEST_CASE("intel: a rebel planet breaks away 25 %, joins the source 18.75 %, stays 56.25 %") {
    const Rules& r = politicsRules();
    int independent = 0, joined = 0, stayed = 0;
    constexpr int kRuns = 800;
    const GameState base = newPoliticsGame(5);
    for (int k = 0; k < kRuns; ++k) {
        GameState s = base;
        const ObjectId home = homeworld(s, kB).planet;
        effects::Target request = target(kB, kA);
        request.object = home;
        Rng rng(static_cast<uint64_t>(k) * 7919 + 1);
        auto picked = effects::pickTarget(r, s, Effect::PlanetPopulationRebel, request, rng);
        REQUIRE(picked);
        TurnContext ctx = context(s);
        const auto out = effects::apply(ctx, Effect::PlanetPopulationRebel, *picked, 1, rng);
        const EmpireId owner = s.colony(home)->owner;
        if (owner == kB) {
            ++stayed;
            CHECK_FALSE(out.applied);
        } else if (owner == kA) {
            ++joined;
            CHECK(out.applied);
            CHECK_FALSE(s.colony(home)->homeworld);
            CHECK(hasMood(ctx, kB, "Homeworld Lost"));
            CHECK(s.empire(kA).hasExplored(s.galaxy.object(home).system));
        } else {
            ++independent;
            CHECK(out.applied);
            REQUIRE(s.empires.size() == 4);
            CHECK(owner == EmpireId{3u});
            CHECK(s.empire(owner).kind == PlayerKind::Computer);
            CHECK(s.empire(owner).relation(kB).treaty == Treaty::None);
            CHECK_FALSE(s.empire(kB).relation(owner).contact);
            for (const Empire& e : s.empires) CHECK(e.relations.size() == 4);
        }
    }
    CHECK(independent > kRuns * 20 / 100);
    CHECK(independent < kRuns * 30 / 100);
    CHECK(joined > kRuns * 14 / 100);
    CHECK(joined < kRuns * 24 / 100);
    CHECK(stayed > kRuns * 50 / 100);
    CHECK(stayed < kRuns * 62 / 100);

    // "Any" can pick the homeworld: it is a colony like any other (spec 05 §2.3).
    GameState s = newPoliticsGame(5);
    Rng rng(1);
    const auto any = effects::pickTarget(r, s, Effect::PlanetPopulationRebel, target(kB, kA), rng);
    REQUIRE(any.has_value());
    CHECK(any->object == homeworld(s, kB).planet);

    // With 20 empires (destroyed ones count) the operation does nothing, and
    // no roll is made.
    while (s.empires.size() < effects::kMaxEmpires) {
        Empire extra = s.empire(kC);
        extra.id = EmpireId{s.empires.size()};
        extra.alive = false;
        s.empires.push_back(extra);
    }
    for (Empire& e : s.empires) e.relations.resize(s.empires.size());
    const Rng before = rng;
    TurnContext ctx = context(s);
    CHECK_FALSE(effects::apply(ctx, Effect::PlanetPopulationRebel, *any, 1, rng).applied);
    CHECK(rng == before);
    CHECK(s.colony(any->object)->owner == kB);
}

TEST_CASE("intel: ship sabotage takes supply and this turn's movement only") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Vehicle* ship = nullptr;
    for (Vehicle& v : s.vehicles)
        if (v.owner == kB) ship = &v;
    REQUIRE(ship);
    const VehicleId id = ship->id;
    ship->movement = 4;
    ship->supply = 300;
    effects::Target request = target(kB, kA);
    request.vehicle = id;
    auto out = run(s, Effect::ShipLoseMovement, request, 3);
    CHECK(out.actual == 3);
    CHECK(s.vehicle(id)->movement == 1);
    CHECK(s.vehicle(id)->immobileUntil <= s.turn);
    out = run(s, Effect::ShipLoseMovement, request, 3);
    CHECK(s.vehicle(id)->movement == 0);  // never below 0
    out = run(s, Effect::ShipLoseSupply, request, 120);
    CHECK(s.vehicle(id)->supply == 180);
    out = run(s, Effect::ShipLoseSupply, request, 1000);
    CHECK(s.vehicle(id)->supply == 0);

    // Orders Change: one move order to a random system; mothballed ships are immune.
    out = run(s, Effect::ShipOrdersChange, request);
    REQUIRE(s.vehicle(id)->orders.size() == 1);
    CHECK(s.vehicle(id)->orders[0].kind == OrderKind::MoveTo);
    s.vehicle(id)->status = VehicleStatus::Mothballed;
    s.vehicle(id)->orders.clear();
    Rng rng(4);
    CHECK(effects::pickTarget(r, s, Effect::ShipOrdersChange, request, rng).has_value());  // the handler checks
    CHECK_FALSE(run(s, Effect::ShipOrdersChange, request).applied);
    CHECK(s.vehicle(id)->orders.empty());
}

TEST_CASE("intel: political operations") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kB);
    setContact(s, kB, kC);
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    auto third = [&](EmpireId other) {
        effects::Target t = target(kB, kA);
        t.other = other;
        return t;
    };

    // Disrupt Trade needs trade between the target and a third empire; its
    // counter restarts. "Any" draws among the living empires the source has
    // contact with, other than the source and the target (spec 05 open
    // question 38); the handler checks the rest.
    Rng rng(2);
    const auto any = effects::pickTarget(r, s, Effect::PoliticsDisruptTrade, target(kB, kA), rng);
    REQUIRE(any.has_value());
    CHECK(any->other == kC);
    for (const bool destroyed : {false, true}) {
        GameState g = s;
        if (destroyed) g.empire(kC).alive = false;
        else g.empire(kA).relation(kC).contact = false;
        Rng again(2);
        CHECK_FALSE(effects::pickTarget(r, g, Effect::PoliticsDisruptTrade, target(kB, kA), again).has_value());
    }
    CHECK_FALSE(run(s, Effect::PoliticsDisruptTrade, {}).applied);
    // Intercept Messages with nothing to report fails.
    CHECK_FALSE(run(s, Effect::PoliticsInterceptMessages, third(kC)).applied);
    diplomacy::setTreaty(ctx, kB, kC, Treaty::TradeAlliance);
    s.empire(kB).relation(kC).tradeTurns = s.empire(kC).relation(kB).tradeTurns = 12;
    auto out = run(s, Effect::PoliticsDisruptTrade, {});
    CHECK(out.applied);
    CHECK(out.actual == 12);
    CHECK(s.empire(kB).relation(kC).tradeTurns == 0);
    CHECK(s.empire(kC).relation(kB).tradeTurns == 0);

    // Treaty Info names the treaty.
    out = run(s, Effect::PoliticsTreatyInfo, third(kC));
    CHECK(out.tokens.treatyName == "Trade Alliance");
    REQUIRE_FALSE(out.report.empty());

    // Intercept Messages reports the latest message between them.
    DiplomaticMessage m;
    m.type = MessageType::General;
    m.text = "Old plans";
    m.to = kC;
    REQUIRE(apply(r, s, kB, cmd::SendMessage{m}).ok);
    diplomacy::deliverMessages(ctx);
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    ++s.turn;
    m.text = "Secret plans";
    REQUIRE(apply(r, s, kB, cmd::SendMessage{m}).ok);
    out = run(s, Effect::PoliticsInterceptMessages, third(kC));
    REQUIRE(out.report.size() == 1);
    CHECK(out.report[0].find("Secret plans") != std::string::npos);

    // Prevent Messages: the messages of the last two turns between them are
    // deleted, so they are never answered; later messages go through.
    out = run(s, Effect::PoliticsPreventMessages, third(kC));
    CHECK(out.actual == 2);
    CHECK(std::none_of(s.messages.begin(), s.messages.end(), [](const DiplomaticMessage& x) { return x.from == kB && x.to == kC; }));

    // Fake Messages: a real declaration of war in the target's name.
    out = run(s, Effect::PoliticsFakeMessages, third(kC));
    CHECK(out.applied);
    CHECK(s.empire(kB).relation(kC).treaty == Treaty::War);
    CHECK(s.empire(kC).relation(kB).treaty == Treaty::War);
    REQUIRE_FALSE(s.messages.empty());
    CHECK(s.messages.back().type == MessageType::DeclareWar);
    CHECK(s.messages.back().from == kB);
    CHECK(s.messages.back().to == kC);
    CHECK(hasLog(s, kC, "War Declared"));
}

TEST_CASE("intel: espionage reports") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const SystemId theirHome = s.galaxy.object(homeworld(s, kB).planet).system;
    REQUIRE_FALSE(s.empire(kA).hasExplored(theirHome));

    auto out = run(s, Effect::PlanetLocations, {});
    CHECK(out.actual == 1);
    CHECK(s.empire(kA).hasExplored(theirHome));

    effects::Target request = target(kB, kA);
    request.object = homeworld(s, kB).planet;
    out = run(s, Effect::PlanetInfo, request);
    CHECK(out.report.size() >= 4);
    CHECK(out.tokens.planetName == s.galaxy.object(homeworld(s, kB).planet).name);

    // System - Info: the highest-numbered system they explored and we did not;
    // the system drawn for the order does not matter (spec 05 §2.3).
    GameState t = newPoliticsGame();
    std::optional<SystemId> highest;
    for (const StarSystem& sys : t.galaxy.systems)
        if (t.empire(kB).hasExplored(sys.id) && !t.empire(kA).hasExplored(sys.id)) highest = sys.id;
    REQUIRE(highest);
    effects::Target named = target(kB, kA);
    named.system = SystemId{0u};
    out = run(t, Effect::SystemInfo, named);
    CHECK(out.applied);
    CHECK(t.empire(kA).hasExplored(*highest));
    for (const StarSystem& sys : t.galaxy.systems)
        if (t.empire(kB).hasExplored(sys.id)) t.empire(kA).knowledge.explored[sys.id.index()] = 1;
    CHECK_FALSE(run(t, Effect::SystemInfo, {}).applied);  // nothing left to learn

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

TEST_CASE("intel: effects that need a source achieve nothing without one") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PointsSteal, target(kB, {}), rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchSteal, target(kB, kB), rng).has_value());  // not self
    CHECK_FALSE(effects::pickTarget(r, s, Effect::IntelligenceDefense, target(kB, kA), rng).has_value());
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        if (effects::needsSource(e)) CHECK_FALSE(effects::pickTarget(r, s, e, target(kB, {}), rng).has_value());
    }
}

TEST_CASE("intel: turns are deterministic") {
    auto play = [] {
        GameState s = newPoliticsGame(17);
        setContact(s, kA, kB);
        setContact(s, kB, kC);
        std::vector<std::string> titles;
        for (int turn = 0; turn < 6; ++turn) {
            for (Empire& e : s.empires) e.intelPool = 1500;
            s.empire(kA).intel = {order(Effect::ShipDamage, kB), order(Effect::PointsChange, kB), order(Effect::PlanetFacilityDamage, kB)};
            s.empire(kC).intel = {order(Effect::ShipExperienceChange, kB)};
            s.empire(kB).intel = {defense(1, 500)};
            TurnContext ctx = context(s);
            for (size_t i = 0; i < s.empires.size(); ++i)
                if (s.empires[i].alive) intel::intelStep(ctx, EmpireId{i});
            ++s.turn;
        }
        for (const Empire& e : s.empires)
            for (const LogEntry& l : e.log) titles.push_back(l.title + "|" + l.text);
        return std::pair{titles, s.rng};
    };
    const auto a = play();
    const auto b = play();
    CHECK_FALSE(a.first.empty());
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}
