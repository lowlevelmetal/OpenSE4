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
[[maybe_unused]] Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }
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

    // The stored selection is an entry's index in the whole log, found again
    // in the filtered list (spec 06 §7 Q42).
    const std::vector<int32_t> shown{2, 5, 9};
    CHECK(logOpeningRow(5, shown) == 1);
    CHECK(logOpeningRow(9, shown) == 2);
    CHECK(logOpeningRow(4, shown) == 0);   // that entry is not listed: the first row
    CHECK(logOpeningRow(-1, shown) == 0);
    CHECK(logOpeningRow(2, {}) == -1);

    // Goto's window targets (spec 06 §7 Q41).
    CHECK(logWindowTarget(LogGoto::ConstructionQueues) == LogWindow::ConstructionQueues);
    CHECK(logWindowTarget(LogGoto::Research) == LogWindow::Research);
    CHECK(logWindowTarget(LogGoto::Intelligence) == LogWindow::Intelligence);
    CHECK(logWindowTarget(LogGoto::Empires) == LogWindow::Empires);
    CHECK(logWindowTarget(LogGoto::EmpireOptions) == LogWindow::EmpireOptions);
    CHECK(logWindowTarget(LogGoto::Designs) == LogWindow::Designs);
    CHECK_FALSE(logWindowTarget(LogGoto::Location));
    CHECK_FALSE(logWindowTarget(LogGoto::None));
}

TEST_CASE("classic log: a combat entry lists the pieces each empire had at the start, with their damage at the end") {
    World w;
    const Rules& r = w.rules();
    const DesignId design = w.ship(kA, "Hauler", 1);
    const std::string code = r.hull(w.s.design(design).hull).code;

    CombatRecord c;
    c.participants = {kA, kB};
    // Owner at the start, damage fixed at the end (-1: no row), the survivor's owner then.
    auto piece = [&](CombatPiece::Kind kind, EmpireId owner, std::string name, int damage, EmpireId survivor) {
        CombatPiece p;
        p.kind = kind;
        p.owner = owner;
        p.design = kind == CombatPiece::Kind::Vehicle ? design : DesignId{};
        p.name = std::move(name);
        p.damage = static_cast<int16_t>(damage);
        p.survivor = survivor;
        c.pieces.push_back(p);
        return uint32_t(c.pieces.size() - 1);
    };
    const uint32_t half = piece(CombatPiece::Kind::Vehicle, kA, "Half", 50, kA);
    const uint32_t intact = piece(CombatPiece::Kind::Vehicle, kA, "Intact", 0, kA);
    const uint32_t lost = piece(CombatPiece::Kind::Vehicle, kB, "Lost", 100, {});
    const uint32_t taken = piece(CombatPiece::Kind::Vehicle, kB, "Prize", 20, kA);   // captured: now ours
    const uint32_t wing = piece(CombatPiece::Kind::UnitGroup, kB, "Wing", 30, kB);
    const uint32_t world = piece(CombatPiece::Kind::Planet, kB, "World", 12, kB);
    const uint32_t dead = piece(CombatPiece::Kind::Planet, kB, "Ashes", 100, {});   // its colony died
    piece(CombatPiece::Kind::UnitGroup, kA, "Launched", -1, kA);                     // launched during the battle
    piece(CombatPiece::Kind::Seeker, kA, "Missile", -1, {});
    piece(CombatPiece::Kind::Obstacle, {}, "Rock", -1, {});

    const auto rows = combatDamageRows(r, w.s, c);
    auto rowOf = [&](uint32_t p) {
        for (const CombatDamageRow& row : rows)
            if (!row.header && row.piece == p) return row;
        return CombatDamageRow{{}, false, 0, "missing", "missing"};
    };
    REQUIRE(rows.size() == 9);   // two headers and seven pieces present at the start
    CHECK(rows[0].header);
    CHECK(rows[0].empire == kA);
    CHECK(rows[3].header);
    CHECK(rows[3].empire == kB);
    // Ships and bases add their hull Code (spec 06 §7 Q18); units and planets do not.
    CHECK(rowOf(half).name == (code.empty() ? std::string("Half") : std::format("Half ({})", code)));
    CHECK(rowOf(wing).name == "Wing");
    CHECK(rowOf(world).name == "World");
    CHECK(rowOf(half).damage == "50%");
    CHECK(rowOf(intact).damage == "0%");
    CHECK(rowOf(lost).damage == "Dead");
    CHECK(rowOf(taken).damage == "Taken");
    CHECK(rowOf(wing).damage == "30%");
    CHECK(rowOf(world).damage == "12%");
    CHECK(rowOf(dead).damage == "Dead");
}

TEST_CASE("classic log: the damage percentage rounds halves to even") {
    CHECK(logDamagePercent(0, 0) == 100);       // nothing to lose
    CHECK(logDamagePercent(100, 100) == 0);
    CHECK(logDamagePercent(100, 0) == 100);
    CHECK(logDamagePercent(100, 150) == 0);     // more than at the start
    CHECK(logDamagePercent(200, 1) == 100);     // 0.5 % kept rounds to 0
    CHECK(logDamagePercent(200, 3) == 98);      // 1.5 % kept rounds to 2
    CHECK(logDamagePercent(200, 5) == 98);      // 2.5 % kept rounds to 2
    CHECK(logDamagePercent(3, 2) == 33);        // 66.7 % kept
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
