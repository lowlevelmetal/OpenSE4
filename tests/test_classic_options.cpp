// The classic client's Empire Options and Options windows and the Log's
// rules (docs/spec/06 §1.9, §4.1): what the windows open with, the Damage
// list of a combat entry, the facility markers and the settings file.

#include "movement_fixture.hpp"

#include "client/classic/facility_markers.hpp"
#include "client/classic/screens/empire_logic.hpp"
#include "client/classic/settings.hpp"

#include <doctest/doctest.h>

#include <format>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;
using namespace opense4::client::classic;

namespace {
Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }
} // namespace

TEST_CASE("classic log: the filter and the row a new opening starts with") {
    std::vector<int> counts(kLogCategories, 0);
    counts[size_t(LogCategory::Combat)] = 2;
    const uint8_t combat = uint8_t(LogCategory::Combat) + 1;
    CHECK(logOpeningFilter(0, counts) == 0);
    CHECK(logOpeningFilter(combat, counts) == combat);
    // A category without entries this turn opens on All.
    CHECK(logOpeningFilter(uint8_t(LogCategory::Research) + 1, counts) == 0);
    CHECK(logOpeningFilter(99, counts) == 0);

    CHECK(logOpeningRow(3, 5) == 3);
    CHECK(logOpeningRow(5, 5) == 0);   // the stored position is gone: the first row
    CHECK(logOpeningRow(-1, 5) == 0);
    CHECK(logOpeningRow(2, 0) == -1);

    CHECK(logWindowTarget(LogCategory::Construction) == LogWindow::ConstructionQueues);
    CHECK(logWindowTarget(LogCategory::Research) == LogWindow::Research);
    CHECK(logWindowTarget(LogCategory::Intelligence) == LogWindow::Intelligence);
    CHECK(logWindowTarget(LogCategory::Politics) == LogWindow::Empires);
    CHECK_FALSE(logWindowTarget(LogCategory::Combat));
}

TEST_CASE("classic log: a combat entry lists each empire's ships, groups and planets with their damage") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId design = w.ship(kA, "Hauler", 1);
    const VehicleId hurt = w.spawn(design, at(a, 1, 1));
    const VehicleId whole = w.spawn(design, at(a, 1, 1));
    Vehicle& v = w.v(hurt);
    const int structure = vehicleStructure(r, w.s, v);
    REQUIRE(structure > 0);
    v.damage.assign(w.s.design(design).entries.size(), 0);
    v.damage[0] = structure / 2;

    CombatRecord c;
    c.participants = {kA, kB};
    auto piece = [&](CombatPiece::Kind kind, EmpireId owner, std::string name, VehicleId id = {}, int count = 1) {
        CombatPiece p;
        p.kind = kind;
        p.owner = owner;
        p.vehicle = id;
        p.name = std::move(name);
        p.count = count;
        c.pieces.push_back(p);
        return uint32_t(c.pieces.size() - 1);
    };
    const uint32_t half = piece(CombatPiece::Kind::Vehicle, kA, "Half", hurt);
    const uint32_t intact = piece(CombatPiece::Kind::Vehicle, kA, "Intact", whole);
    const uint32_t lost = piece(CombatPiece::Kind::Vehicle, kB, "Lost");
    const uint32_t taken = piece(CombatPiece::Kind::Vehicle, kB, "Taken");
    const uint32_t wing = piece(CombatPiece::Kind::UnitGroup, kB, "Wing", {}, 10);
    const uint32_t world = piece(CombatPiece::Kind::Planet, kB, "World");
    const uint32_t quiet = piece(CombatPiece::Kind::Planet, kB, "Quiet");
    piece(CombatPiece::Kind::Seeker, kA, "Missile");
    auto event = [&](CombatEvent::Kind kind, uint32_t p, uint32_t target = 0, int amount = 0) {
        CombatEvent e;
        e.kind = kind;
        e.piece = p;
        e.target = target;
        e.amount = amount;
        c.events.push_back(e);
    };
    event(CombatEvent::Kind::Destroyed, lost);
    event(CombatEvent::Kind::Captured, taken, half);
    event(CombatEvent::Kind::UnitsLost, wing, half, 3);
    event(CombatEvent::Kind::Hit, half, world, 40);

    const auto rows = combatDamageRows(r, w.s, c);
    auto damageOf = [&](uint32_t p) {
        for (const CombatDamageRow& row : rows)
            if (!row.header && row.piece == p) return row.damage;
        return std::string("missing");
    };
    REQUIRE(rows.size() == 9);   // two headers and seven pieces; the seeker is left out
    CHECK(rows[0].header);
    CHECK(rows[0].empire == kA);
    CHECK(rows[3].header);
    CHECK(rows[3].empire == kB);
    CHECK(damageOf(half) == std::format("{}%", (structure / 2) * 100 / structure));
    CHECK(damageOf(intact) == "0%");
    CHECK(damageOf(lost) == "Dead");
    CHECK(damageOf(taken) == "Taken");
    CHECK(damageOf(wing) == "30%");
    CHECK(damageOf(world) == "Hit");
    CHECK(damageOf(quiet) == "0%");
}

TEST_CASE("classic options: facility markers follow the switched-on groups") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId planet = w.planet(a, {5, 6});
    const Colony& c = w.colony(planet, kA, 10, {"Mv Fleet Trainer", "Mv Trainer", "Mv System Scanner"});
    const uint16_t training = 1u << 2, scanners = 1u << 11, yards = 1u << 0;
    CHECK(facilityMarkers(r, c, 0) == "");
    CHECK(facilityMarkers(r, c, yards) == "");
    CHECK(facilityMarkers(r, c, training) == "St Ft");
    CHECK(facilityMarkers(r, c, scanners) == "Slr");
    CHECK(facilityMarkers(r, c, uint16_t(training | scanners | yards)) == "St Ft Slr");
}

TEST_CASE("classic options: facility marker letters and their tests (spec 06 §7 Q44)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId planet = w.planet(a, {5, 6});
    const Colony& c = w.colony(planet, kA, 10, {"Mv Converter", "Mv Repair Shop", "Mv Upkeep Office", "Mv Nursery", "Mv Depot", "Mv Port", "Mv Yard"});
    const uint16_t yards = 1u << 0, repair = 1u << 3, upkeep = 1u << 8, plague = 1u << 9;
    // Rc is Resource Conversion, not Component Repair.
    CHECK(facilityMarkerGroups(r, c, repair) == std::vector<std::string>{"Rc"});
    // Srm is Reduced Maintenance Cost - System; Src is Modify Reproduction - System.
    CHECK(facilityMarkerGroups(r, c, upkeep) == std::vector<std::string>{"Srm"});
    CHECK(facilityMarkerGroups(r, c, plague) == std::vector<std::string>{"Src"});
    // R, S, Y in the table's order; Y needs a working (uncloaked) yard.
    CHECK(facilityMarkerGroups(r, c, yards) == std::vector<std::string>{"R", "S", "Y"});
    CHECK(facilityMarkerGroups(r, c, yards, false) == std::vector<std::string>{"R", "S"});

    // Whose colonies are marked: ours, and Military Alliance or Partnership partners'.
    CHECK(showsFacilityMarkers(w.s, kA, kA));
    w.setTreaty(kA, kB, Treaty::NonAggression);
    CHECK_FALSE(showsFacilityMarkers(w.s, kA, kB));
    w.setTreaty(kA, kB, Treaty::MilitaryAlliance);
    CHECK(showsFacilityMarkers(w.s, kA, kB));
    w.setTreaty(kA, kB, Treaty::Partnership);
    CHECK(showsFacilityMarkers(w.s, kA, kB));
}

TEST_CASE("classic options: facility markers pack right to left (spec 06 §2.4)") {
    // "CvYSR": R ends at the square's right edge, each group to the left of the one before.
    const std::vector<MarkerPlace> p = packFacilityMarkers({4, 4, 4, 8}, 36);
    REQUIRE(p.size() == 4);
    CHECK(p[0].x == 32);
    CHECK(p[1].x == 28);
    CHECK(p[2].x == 24);
    CHECK(p[3].x == 16);
    for (const MarkerPlace& m : p) CHECK(m.line == 0);
    // A group that would start at or left of the left edge starts a new line, again from the right.
    const std::vector<MarkerPlace> q = packFacilityMarkers({12, 12, 12, 10}, 36);
    CHECK(q[1].x == 12);
    CHECK(q[2].line == 1);   // it would start at 0
    CHECK(q[2].x == 24);
    CHECK(q[3].line == 1);
    CHECK(q[3].x == 14);
    // A group wider than the square still takes a line of its own.
    const std::vector<MarkerPlace> wide = packFacilityMarkers({40, 4}, 36);
    CHECK(wide[0].line == 0);
    CHECK(wide[1].line == 1);
}

TEST_CASE("classic options: the per-computer settings keep the Options window's switches") {
    ClassicSettings s;
    CHECK(s.musicVolume == 100);
    CHECK_FALSE(s.showMovementLines);
    CHECK_FALSE(s.classicSoundEffects);
    s.animateCombatMovement = false;
    s.fastTacticalCombat = true;
    s.musicVolume = 40;
    s.showToHitChances = true;
    s.learnDone = {"tutorial:first-steps"};   // lesson progress lives in the same file
    const ClassicSettings back = settingsFromToml(settingsToToml(s));
    CHECK_FALSE(back.animateCombatMovement);
    CHECK(back.fastTacticalCombat);
    CHECK(back.musicVolume == 40);
    CHECK(back.showToHitChances);
    CHECK(back.learnDone == s.learnDone);
    // A volume between the steps comes back as the nearest step.
    const ClassicSettings odd = settingsFromToml("[sound]\nmusic_percent = 57\n");
    CHECK(odd.musicVolume == 60);
    // New empires start with the Empire Options defaults of spec 06 §1.9.
    const InterfaceOptions o;
    CHECK(o.confirmEndTurn);
    CHECK(o.showLogAtTurnStart);
    CHECK_FALSE(o.planetNames);
    CHECK(o.colonizableMarkers);
    CHECK(o.coordinateLocation);
    CHECK(o.facilityMarkers == 0);
    CHECK(o.autoClaimColonized);
    CHECK(o.queuesShown == 0x0f);
}
