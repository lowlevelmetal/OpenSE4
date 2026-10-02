// What other players see of a colony (docs/spec/01 §6.9 "What other players
// see"): the "seeing the planet" test, the detection rule for the colony, the
// windows that follow them.

#include "movement_fixture.hpp"

#include "client/classic/map_style.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/sector_view.hpp"
#include "game/commands.hpp"
#include "game/sight.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <vector>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;
namespace ui = opense4::client::classic;

namespace {

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

bool contains(const std::vector<ObjectId>& list, ObjectId id) { return std::find(list.begin(), list.end(), id) != list.end(); }

std::optional<ui::PlanetInfo> listed(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet) {
    for (const ui::PlanetInfo& p : ui::surveyPlanets(r, s, viewer))
        if (p.id == planet) return p;
    return std::nullopt;
}

bool marked(const std::vector<std::vector<EmpireId>>& presence, SystemId sys, EmpireId e) {
    const auto& list = presence[sys.index()];
    return std::find(list.begin(), list.end(), e) != list.end();
}

// Two systems. In "Near", A's colony at (1,1) (a sensor source, EM Active 1)
// and B's colony at (6,6), which can cloak at level 3 in every sight type. In
// "Far", B's colony at (4,4) and nothing of A's: no sensor source of A there.
struct Sky {
    World w;
    SystemId nearSys, farSys;
    ObjectId mine, cloaker, far;
    Sky() {
        nearSys = w.system("Near");
        farSys = w.system("Far", 10, 0);
        mine = w.planet(nearSys, {1, 1});
        w.colony(mine, kA, 1000);
        cloaker = w.planet(nearSys, {6, 6});
        w.colony(cloaker, kB, 1000, {"Mv Planet Cloak"});
        far = w.planet(farSys, {4, 4});
        w.colony(far, kB, 1000);
        w.exploreAll(kA);
        w.exploreAll(kB);
        sight::updateKnowledge(w.rules(), w.s);
    }
    void cloak() {
        REQUIRE(apply(w.rules(), w.s, kB, cmd::CloakColony{cloaker, true}).ok);
        sight::updateKnowledge(w.rules(), w.s);
    }
};

} // namespace

TEST_CASE("hidden colonies: seeing the planet and seeing the colony are two tests (spec 01 §6.9)") {
    Sky sky;
    const Rules& r = sky.w.rules();
    GameState& s = sky.w.s;
    // Uncloaked, with our colony in the system: planet and colony seen.
    CHECK(sight::canSeePlanet(r, s, kA, sky.cloaker));
    CHECK(sight::canSeeColony(r, s, kA, sky.cloaker));
    // Without a sensor source in the system the planet is seen, the colony not.
    CHECK(sight::canSeePlanet(r, s, kA, sky.far));
    CHECK_FALSE(sight::canSeeColony(r, s, kA, sky.far));
    // Cloaked above our sensors in every type: the planet is hidden too.
    sky.cloak();
    CHECK_FALSE(sight::canSeePlanet(r, s, kA, sky.cloaker));
    CHECK_FALSE(sight::canSeeColony(r, s, kA, sky.cloaker));
    // The owner always sees both.
    CHECK(sight::canSeePlanet(r, s, kB, sky.cloaker));
    CHECK(sight::canSeeColony(r, s, kB, sky.cloaker));
    // EM Active counts as at least 1 without any sensor source: a cloak of
    // level 1 in EM Active and higher elsewhere leaves the planet seen and
    // the colony unseen.
    Colony& farColony = *s.colony(sky.far);
    farColony.cloakLevels = {1, 3, 3, 3, 3};
    farColony.cloaked = true;
    CHECK(sight::canSeePlanet(r, s, kA, sky.far));
    CHECK_FALSE(sight::canSeeColony(r, s, kA, sky.far));
    farColony.cloakLevels = {2, 3, 3, 3, 3};
    CHECK_FALSE(sight::canSeePlanet(r, s, kA, sky.far));
    // Sensors that reach the cloak in one type reveal both.
    const VehicleId eye = sky.w.spawn(sky.w.ship(kA, "Eye", 1, {"Mv Sensor 3"}), at(sky.nearSys, 2, 2));
    CHECK(sight::canSeePlanet(r, s, kA, sky.cloaker));
    CHECK(sight::canSeeColony(r, s, kA, sky.cloaker));
    sky.w.v(eye).count = 0;
    s.removeDeadVehicles();
    CHECK_FALSE(sight::canSeePlanet(r, s, kA, sky.cloaker));
}

TEST_CASE("hidden colonies: the system panel draws and lists only the planets the player sees") {
    Sky sky;
    const Rules& r = sky.w.rules();
    GameState& s = sky.w.s;
    CHECK(contains(ui::shownStellarObjects(r, s, kA, sky.nearSys), sky.cloaker));
    sky.cloak();
    const auto shown = ui::shownStellarObjects(r, s, kA, sky.nearSys);
    CHECK_FALSE(contains(shown, sky.cloaker));
    CHECK(contains(shown, sky.mine));
    // The sector click and the sector list (one sector) find nothing there.
    CHECK(ui::shownStellarObjects(r, s, kA, sky.nearSys, Sector{6, 6}).empty());
    // So the sector shows no stellar object and counts none.
    const ui::SectorView view = ui::sectorView(r, s, kA, ui::shownStellarObjects(r, s, kA, sky.nearSys, Sector{6, 6}), {}, 36);
    CHECK_FALSE(view.stellar);
    CHECK(view.stellarCount == 0);
    // Its owner still sees it; an unexplored system shows nothing.
    CHECK(contains(ui::shownStellarObjects(r, s, kB, sky.nearSys), sky.cloaker));
    s.empire(kA).knowledge.explored[sky.farSys.index()] = 0;
    CHECK(ui::shownStellarObjects(r, s, kA, sky.farSys).empty());
}

TEST_CASE("hidden colonies: the Planets window, the colonize star and the statistics (spec 06 §1.8.1)") {
    Sky sky;
    const Rules& r = sky.w.rules();
    GameState& s = sky.w.s;
    const ui::ColonizeTech tech = ui::colonizeTech(r, s.empire(kA));
    // A colony in a system without our sensors is listed as an uncolonized planet.
    const auto far = listed(r, s, kA, sky.far);
    REQUIRE(far);
    CHECK_FALSE(far->colonized);
    CHECK_FALSE(ui::matches(ui::PlanetFilter::AllColonies, *far));
    CHECK_FALSE(ui::matches(ui::PlanetFilter::EnemyColonies, *far));
    CHECK(ui::matches(ui::PlanetFilter::All, *far));
    CHECK(ui::matches(ui::PlanetFilter::ColonizableEmpty, *far) == ui::colonizableType(s, kA, sky.far, tech));
    // The map's colonize star and Send Colony Ship treat it as empty.
    CHECK(ui::seenColony(r, s, kA, sky.far) == nullptr);
    CHECK(ui::colonizeProblem(r, s, kA, sky.far, tech).empty() == ui::colonizableType(s, kA, sky.far, tech));
    // Seen with our colony's sensors: a colony.
    const auto near = listed(r, s, kA, sky.cloaker);
    REQUIRE(near);
    CHECK(near->colonized);
    CHECK(ui::matches(ui::PlanetFilter::EnemyColonies, *near));
    CHECK_FALSE(ui::colonizeProblem(r, s, kA, sky.cloaker, tech).empty());
    // Hidden by its cloak: in no tab.
    sky.cloak();
    CHECK_FALSE(listed(r, s, kA, sky.cloaker));
    CHECK(listed(r, s, kB, sky.cloaker));  // its owner lists it
    // The statistics count every planet of the explored systems with its real
    // owner: the hidden planet and the unseen colony too.
    const ui::PlanetStatistics st = ui::planetStatistics(r, s, kA, {});
    CHECK(st.systems == 2);
    CHECK(st.planets == 3);
    const int theirs = int(ui::colonizableType(s, kA, sky.cloaker, tech)) + int(ui::colonizableType(s, kA, sky.far, tech));
    CHECK(st.enemy == theirs);
    CHECK(st.free == 0);
}

TEST_CASE("hidden colonies: the galaxy map's presence colours follow the detection rule") {
    Sky sky;
    const Rules& r = sky.w.rules();
    GameState& s = sky.w.s;
    auto presence = ui::map_style::presence(r, s, kA);
    CHECK(marked(presence, sky.nearSys, kB));       // seen with our colony's sensors
    CHECK_FALSE(marked(presence, sky.farSys, kB));  // no sensor source of ours there
    CHECK(marked(presence, sky.nearSys, kA));
    sky.cloak();
    presence = ui::map_style::presence(r, s, kA);
    CHECK_FALSE(marked(presence, sky.nearSys, kB));
    CHECK(marked(ui::map_style::presence(r, s, kB), sky.nearSys, kB));
}
