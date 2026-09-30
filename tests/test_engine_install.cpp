// Engine checks against the player's installed classic data (opt-in: set
// OPENSE4_CLASSIC_DATA=auto or to a data directory).

#include "engine_fixture.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

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

TEST_CASE("installed data set: race presets, setup and starting designs (opt-in)") {
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
    auto game = createGame(*r, setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    for (const Empire& e : s.empires) {
        INFO(e.name);
        CHECK(e.racialPointsSpent <= s.options.racialPoints);
        const Colony& home = opense4::test::homeworld(s, e.id);
        CHECK(home.totalPopulation() > 0);
        CHECK(colonyHasSpaceYard(*r, home));
        CHECK(e.designs.size() >= 2);
        for (DesignId d : e.designs) {
            const DesignStats st = computeDesignStats(*r, &e, s.design(d));
            CHECK_MESSAGE(st.problems.empty(), s.design(d).name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        }
    }
    for (const Vehicle& v : s.vehicles) CHECK(vehicleMaxMovement(*r, s, v) > 0);
    std::vector<EmpireOrders> none;
    processTurn(*r, s, none);
    CHECK(s.turn == 1);
}
