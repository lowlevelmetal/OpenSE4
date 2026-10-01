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

TEST_CASE("classic options: the per-computer settings keep the Options window's switches") {
    ClassicSettings s;
    CHECK(s.musicVolume == 100);
    CHECK_FALSE(s.showMovementLines);
    CHECK_FALSE(s.classicSoundEffects);
    s.animateCombatMovement = false;
    s.fastTacticalCombat = true;
    s.musicVolume = 40;
    s.showToHitChances = true;
    const ClassicSettings back = settingsFromToml(settingsToToml(s));
    CHECK_FALSE(back.animateCombatMovement);
    CHECK(back.fastTacticalCombat);
    CHECK(back.musicVolume == 40);
    CHECK(back.showToHitChances);
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
