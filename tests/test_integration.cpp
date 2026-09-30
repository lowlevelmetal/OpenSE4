// Whole-engine checks: every subsystem together, many turns, computer players
// for every empire. Invariants are checked after each turn, and two runs with
// the same seed must stay identical.

#include "engine_fixture.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <set>

using namespace opense4;
using namespace opense4::game;

namespace {

void checkInvariants(const Rules& r, const GameState& s) {
    INFO("turn " << s.turn);
    // Containers sorted by id, ids unique.
    for (size_t i = 1; i < s.vehicles.size(); ++i) REQUIRE(s.vehicles[i - 1].id < s.vehicles[i].id);
    for (size_t i = 1; i < s.fleets.size(); ++i) REQUIRE(s.fleets[i - 1].id < s.fleets[i].id);
    REQUIRE(s.colonies.size() == s.galaxy.objects.size());

    for (const Vehicle& v : s.vehicles) {
        INFO("vehicle " << v.name);
        CHECK(v.count > 0);
        REQUIRE(v.design.valid());
        REQUIRE(v.design.index() < s.designs.size());
        CHECK(v.owner.valid());
        CHECK(v.owner.index() < s.empires.size());
        CHECK(v.location.system.index() < s.galaxy.systems.size());
        CHECK(v.location.sector.valid());
        CHECK(v.damage.size() == s.design(v.design).entries.size());
        CHECK(v.supply >= 0);
        // Unlimited supply (bases, quantum reactors) is held at its marker; other
        // supply never exceeds what the vehicle can hold now, damage included
        // (spec 03 §7). A ship that just lost its reactor keeps the marker until
        // it next moves.
        if (vehicleHasUnlimitedSupply(r, s, v)) CHECK(v.supply == kUnlimitedSupply);
        else if (v.supply != kUnlimitedSupply) CHECK(v.supply <= vehicleSupplyCapacity(r, s, v));
        // Cargo that no longer fits is lost at once (spec 03 §11).
        CHECK(cargoSpaceUsed(r, s, v.cargo) <= vehicleCargoCapacity(r, s, v));
        // A unit group that mixes designs holds units of one kind (spec 03 §12).
        for (const UnitStack& st : v.mixed) {
            REQUIRE(st.design.index() < s.designs.size());
            CHECK(r.hull(s.design(st.design).hull).type == vehicleType(r, s, v));
            CHECK(st.count > 0);
        }
        if (v.fleet.valid()) {
            const Fleet* f = s.fleet(v.fleet);
            REQUIRE(f);
            CHECK(f->owner == v.owner);
            CHECK(std::find(f->members.begin(), f->members.end(), v.id) != f->members.end());
        }
    }
    for (const Fleet& f : s.fleets) {
        CHECK_FALSE(f.members.empty());
        for (VehicleId id : f.members) {
            const Vehicle* v = s.vehicle(id);
            REQUIRE(v);
            CHECK(v->fleet == f.id);
        }
    }
    for (const auto& c : s.colonies) {
        if (!c) continue;
        INFO("colony " << s.galaxy.object(c->planet).name);
        CHECK(c->owner.index() < s.empires.size());
        CHECK(c->totalPopulation() >= 0);
        CHECK(c->totalPopulation() <= maxPopulation(r, s, *c) + 1);
        CHECK(static_cast<int>(c->facilities.size()) <= std::max(facilitySlots(r, s, *c), static_cast<int>(c->facilities.size())));
        for (const auto& p : c->population) CHECK(p.millions >= 0);
    }
    for (const Empire& e : s.empires) {
        INFO("empire " << e.name);
        CHECK_FALSE(e.stockpile.anyNegative());
        CHECK(e.research.size() <= 12);
        CHECK(e.relations.size() == s.empires.size());
        for (const Empire& o : s.empires)
            if (o.id != e.id) CHECK(e.relation(o.id).treaty == o.relation(e.id).treaty);
    }
}

GameState playAllAi(uint64_t seed, int empires, int turns, std::vector<uint64_t>* checksums = nullptr) {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(seed, empires, 16, /*allHuman=*/false);
    for (Empire& e : s.empires) e.kind = PlayerKind::Computer;
    for (int t = 0; t < turns && !s.gameOver; ++t) {
        processTurn(r, s, {});
        checkInvariants(r, s);
        if (checksums) checksums->push_back(stateChecksum(s));
    }
    return s;
}

} // namespace

TEST_CASE("integration: computer players run a full game on the test rules") {
    const GameState s = playAllAi(21, 4, 120);
    // The game went somewhere: colonies were founded, ships built, research done.
    int colonies = 0, settled = 0;
    for (const auto& c : s.colonies) {
        colonies += c.has_value();
        settled += c && !c->homeworld && c->totalPopulation() > 0;
    }
    CHECK(colonies > 4);
    // The computer players expand: colony ships are designed, built and
    // flown, and the colonies they found have people.
    CHECK(settled >= 20);
    CHECK(s.vehicles.size() > 8);
    int levels = 0;
    for (const Empire& e : s.empires)
        for (int l : e.techLevels) levels += l;
    const GameState fresh = test::newEngineGame(21, 4, 16, false);
    int startLevels = 0;
    for (const Empire& e : fresh.empires)
        for (int l : e.techLevels) startLevels += l;
    CHECK(levels > startLevels);
    for (const Empire& e : s.empires) CHECK(e.history.size() >= 1);
}

TEST_CASE("integration: same seed, same game") {
    std::vector<uint64_t> a, b;
    playAllAi(5, 3, 60, &a);
    playAllAi(5, 3, 60, &b);
    CHECK(a == b);
}

TEST_CASE("integration: save and load mid-game continues identically") {
    const Rules& r = test::engineRules();
    GameState s = playAllAi(9, 3, 25);
    const std::vector<uint8_t> bytes = serializeState(s);
    auto loaded = deserializeState(bytes);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    GameState t = std::move(*loaded);
    CHECK(stateChecksum(s) == stateChecksum(t));
    for (int i = 0; i < 20; ++i) {
        processTurn(r, s, {});
        processTurn(r, t, {});
    }
    CHECK(stateChecksum(s) == stateChecksum(t));
}

TEST_CASE("integration: installed data set plays 60 turns (opt-in)") {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return;
    auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
    REQUIRE(dir);
    auto loaded = ruleset::loadRuleset(*dir);
    REQUIRE(loaded.ruleset);
    const Rules r(std::move(*loaded.ruleset), dir->parent_path());
    GameSetup setup;
    setup.seed = 77;
    setup.options.systemCount = 30;
    // Frequent events of any severity and finite resources: events strike
    // after the empires' own upkeep (spec 05 §8), which the checks cover.
    setup.options.eventFrequency = 3;
    setup.options.maxEventSeverity = 3;
    setup.options.finiteResources = true;
    for (size_t i = 0; i < 5; ++i) {
        EmpireSetup e;
        e.preset = r.racePresets()[i * 2 % r.racePresets().size()].folder;
        e.kind = PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    auto game = createGame(r, setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    for (int t = 0; t < 60 && !s.gameOver; ++t) {
        processTurn(r, s, {});
        checkInvariants(r, s);
        CHECK(validateState(s, &r).empty());
        if (t % 20 == 19) {  // save and load give back the same bytes
            const std::vector<uint8_t> bytes = serializeState(s);
            auto back = deserializeState(bytes);
            REQUIRE(back.has_value());
            CHECK(serializeState(*back) == bytes);
        }
    }
    int colonies = 0;
    for (const auto& c : s.colonies) colonies += c.has_value();
    CHECK(colonies > 5);
}
