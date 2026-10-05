// The rules view (src/sdk/rules_view.hpp) on the fixture data and, opt-in,
// on the player's installed data set; a view of a game played on the
// installed data; and, opt-in with OPENSE4_SDK_BENCH, the time a view of a
// long computer game takes to build.

#include "engine_fixture.hpp"
#include "sdk/sdk_test_util.hpp"

#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "sdk/queries.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using namespace opense4::sdktest;

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

void checkRulesView(const Rules& r) {
    const Value v = sdk::buildRulesView(r);
    const auto problems = validate(docsSchema(), v, "rules_view");
    CHECK_MESSAGE(problems.empty(), joined(problems));
    const auto& d = r.data();
    CHECK(at(v, "components").size() == d.components.size());
    CHECK(at(v, "facilities").size() == d.facilities.size());
    CHECK(at(v, "hulls").size() == d.vehicleSizes.size());
    CHECK(at(v, "mounts").size() == d.weaponMounts.size());
    CHECK(at(v, "techs").size() == d.techAreas.size());
    CHECK(at(v, "racial_traits").size() == d.racialTraits.size());
    CHECK(at(v, "planet_sizes").size() == d.planetSizes.size());
    CHECK(at(v, "system_types").size() == d.systemTypes.size());
    CHECK(at(v, "intel_projects").size() == d.intelProjects.size());
    CHECK(at(v, "formations").size() == d.formations.size());
    CHECK(at(v, "abilities").size() == static_cast<size_t>(AbilityKind::Unknown));
    CHECK(at(v, "races").size() == r.racePresets().size());
    // Each record is at its own index.
    for (std::string_view table : {"components", "facilities", "hulls", "mounts", "techs", "racial_traits", "planet_sizes", "system_types"}) {
        const auto& list = at(v, table).asList();
        for (size_t i = 0; i < list.size(); ++i) CHECK(intAt(list[i], "id") == static_cast<int64_t>(i));
    }
    for (size_t i = 0; i < d.components.size(); ++i) {
        const Value& c = at(v, "components").asList()[i];
        CHECK(at(c, "name").asString() == d.components[i].name);
        CHECK(at(c, "weapon").isNull() == !d.components[i].isWeapon());
        CHECK(at(c, "abilities").size() == d.components[i].abilities.size());
    }
}

} // namespace

TEST_CASE("sdk rules view: the fixture data, as the docs describe it") {
    const Rules& r = test::engineRules();
    checkRulesView(r);
    const Value v = sdk::buildRulesView(r);
    const Value& laser = at(v, "components").asList()[test::componentIndex(r, "Test Laser")];
    REQUIRE(at(laser, "weapon").isMap());
    CHECK(at(at(laser, "weapon"), "damage_at_range").size() == r.component(test::componentIndex(r, "Test Laser")).weapon.damageAtRange.size());
    CHECK(sdk::buildRulesView(r) == v);   // pure
}

TEST_CASE("sdk rules view: the installed data set (opt-in)") {
    const Rules* r = installRules();
    if (!r) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to test against an installed data set");
        return;
    }
    checkRulesView(*r);
}

TEST_CASE("sdk view: a game on the installed data set follows the docs (opt-in)") {
    const Rules* r = installRules();
    if (!r) return;
    GameSetup setup;
    setup.seed = 77;
    setup.options.systemCount = 24;
    setup.options.simultaneous = true;
    for (size_t i = 0; i < 4; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[i * 3 % r->racePresets().size()].folder;
        e.kind = PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    auto game = createGame(*r, setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    for (int t = 0; t < 15; ++t) processTurn(*r, s, {});
    const uint64_t before = stateChecksum(s);
    for (const bool whole : {false, true}) {
        for (const Empire& e : s.empires) {
            INFO(std::format("{} of {}", whole ? "whole view" : "view", e.name));
            const Value v = sdk::buildView(*r, s, e.id, {whole});
            References refs;
            const auto problems = validate(docsSchema(), v, "view", &refs);
            CHECK_MESSAGE(problems.empty(), joined(problems));
        }
    }
    CHECK(stateChecksum(s) == before);
}

TEST_CASE("sdk view: how long building views of a long computer game takes (opt-in: OPENSE4_SDK_BENCH)") {
    const char* bench = std::getenv("OPENSE4_SDK_BENCH");
    if (!bench) return;
    const int turns = std::max(1, std::atoi(bench));
    const Rules* installed = installRules();
    const Rules& r = installed ? *installed : test::engineRules();
    GameSetup setup;
    setup.seed = 2026;
    setup.options.systemCount = installed ? 40 : 20;
    setup.options.simultaneous = true;
    const size_t empires = installed ? 8 : 4;
    for (size_t i = 0; i < empires; ++i) {
        EmpireSetup e;
        if (installed) e.preset = r.racePresets()[i % r.racePresets().size()].folder;
        else e.name = std::format("Empire {}", i + 1);
        e.kind = PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    auto game = createGame(r, setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    GameState& s = *game;
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    const auto played = Clock::now();
    for (int t = 0; t < turns && !s.gameOver; ++t) processTurn(r, s, {});
    const double playMs = ms(Clock::now() - played);

    size_t colonies = 0;
    for (const auto& c : s.colonies) colonies += c.has_value();
    double redact = 0, fair = 0, whole = 0, worst = 0;
    size_t living = 0;
    for (const Empire& e : s.empires) {
        if (!e.alive) continue;
        ++living;
        auto t0 = Clock::now();
        const sdk::Perspective p(r, s, e.id);
        auto t1 = Clock::now();
        const Value v = sdk::buildView(p);
        auto t2 = Clock::now();
        const Value w = sdk::buildView(r, s, e.id, {.whole = true});
        auto t3 = Clock::now();
        redact += ms(t1 - t0);
        fair += ms(t2 - t1);
        whole += ms(t3 - t2);
        worst = std::max(worst, ms(t2 - t0));
        CHECK(v.size() > 0);
        CHECK(w.size() > 0);
    }
    const auto r0 = Clock::now();
    const Value rules = sdk::buildRulesView(r);
    const double rulesMs = ms(Clock::now() - r0);
    CHECK(rules.size() > 0);
    const double n = static_cast<double>(std::max<size_t>(1, living));
    MESSAGE(std::format("{} data, turn {} ({} played in {:.0f} ms): {} empires alive, {} colonies, {} vehicles, {} designs\n"
                        "per empire: redaction {:.2f} ms, fair view {:.2f} ms (worst redaction + view {:.2f} ms), whole view {:.2f} ms\n"
                        "rules view: {:.2f} ms",
                        installed ? "installed" : "fixture", s.turn, turns, playMs, living, colonies, s.vehicles.size(), s.designs.size(),
                        redact / n, fair / n, worst, whole / n, rulesMs));
}
