// Engine checks against the player's installed classic data (opt-in: set
// OPENSE4_CLASSIC_DATA=auto or to a data directory).

#include "engine_fixture.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::game;

namespace {

const Rules* installRules() {
    static const std::unique_ptr<Rules> rules = []() -> std::unique_ptr<Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_unique<Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules.get();
}

} // namespace

TEST_CASE("installed data set: race presets, setup and starting assets (opt-in)") {
    const Rules* r = installRules();
    if (!r) return;
    CHECK(r->racePresets().size() >= 10);
    for (const auto& p : r->racePresets()) {
        CHECK_FALSE(p.name.empty());
        if (!p.neutral) CHECK_FALSE(p.tiers.empty());
    }

    GameSetup setup;
    setup.seed = 42;
    setup.options.systemCount = 30;
    for (size_t i = 0; i < 4; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[i * 3 % r->racePresets().size()].folder;
        e.kind = i == 0 ? PlayerKind::Human : PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    // The player as in a Quick Start: one Design minister run (spec 01 §2.1).
    StartExtras extras;
    extras.designMinisterRun.push_back(EmpireId{0u});
    auto game = createGame(*r, setup, extras);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    for (const Empire& e : s.empires) {
        INFO(e.name);
        CHECK(e.racialPointsSpent <= s.options.racialPoints);
        const Colony& home = opense4::test::homeworld(s, e.id);
        CHECK(home.totalPopulation() > 0);
        CHECK(colonyHasSpaceYard(*r, home));
        // Starting assets (spec 01 §3.6): only the Quick Start player has designs.
        if (e.id.index() == 0) CHECK(e.designs.size() >= 2);
        else CHECK(e.designs.empty());
        for (DesignId d : e.designs) {
            const DesignStats st = computeDesignStats(*r, &e, s.design(d));
            CHECK_MESSAGE(st.problems.empty(), s.design(d).name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        }
    }
    CHECK(s.vehicles.empty());
    std::vector<EmpireOrders> none;
    processTurn(*r, s, none);
    CHECK(s.turn == 1);
    for (const Empire& e : s.empires)
        if (e.kind == PlayerKind::Computer) CHECK_FALSE(e.designs.empty());  // designed in the first turn
}

TEST_CASE("installed data set: a Terran Quick Start gets the observed designs (opt-in)") {
    // Spec 07 session 3: eleven designs, one per design type, dated 2400.0,
    // all prototypes, and no ship.
    const Rules* r = installRules();
    if (!r || !findPreset(*r, "Terran")) return;
    GameSetup setup;
    setup.seed = 7;
    EmpireSetup me;
    me.preset = "Terran";
    setup.empires.push_back(me);
    StartExtras extras;
    extras.designMinisterRun.push_back(EmpireId{0u});
    auto game = createGame(*r, setup, extras);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    std::vector<std::string> types;
    for (DesignId d : game->empire(EmpireId{0u}).designs) {
        const Design& design = game->design(d);
        CHECK(design.createdTurn == 0);
        CHECK(design.built == 0);
        types.push_back(design.designType);
    }
    std::sort(types.begin(), types.end());
    const std::vector<std::string> observed{"Attack Ship",          "Base Space Yard", "Cargo Transport",   "Colony (Rock)",   "Defense Base",
                                            "Kamikaze Attack Ship", "Population Transport", "Satellite", "Satellite Layer", "Troop Transport",
                                            "Weapon Platform"};
    CHECK(types == observed);
    CHECK(game->vehicles.empty());
}
