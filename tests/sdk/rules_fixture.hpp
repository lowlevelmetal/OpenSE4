#pragma once

// Helpers for the rules tier's tests (tests/sdk/test_sdk_rules*.cpp): the
// rules fixture mod (tests/fixtures/mods/rules-fixture), the engine's test
// rules with that mod in their mod set (and its declared ability and
// intelligence project), the SDK installed with it, and the mod's data.

#include "players_fixture.hpp"

#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"
#include "mods/package.hpp"
#include "ruleset/mods.hpp"
#include "script/json.hpp"
#include "sdk/players.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdktest {

inline constexpr const char* kRulesMod = "test.rules-fixture";

inline const mods::Package& rulesFixtureMod() {
    static const mods::Package p = [] {
        auto opened = mods::openPackage(fixturePath() / "mods" / "rules-fixture");
        REQUIRE_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error()));
        return std::move(*opened);
    }();
    return p;
}

// The engine's test rules with these mods in their mod set: the rules
// fixture's declared ability ("Test Field Battery", summed) on the armor plate,
// and a project of the fixture's intelligence type.
inline ruleset::Ruleset rulesRuleset(const std::vector<const mods::Package*>& packages) {
    ruleset::Ruleset rs = test::buildEngineRuleset();
    for (const mods::Package* p : packages) rs.mods.push_back(p->record());
    rs.declaredAbilities.push_back({"Test Field Battery", ruleset::Combine::Sum, kRulesMod});
    for (ruleset::Component& c : rs.components)
        if (c.name == "Test Armor Plate") c.abilities.push_back({"Test Field Battery", "Recharges supply.", "5", "0"});
    rs.modIntelTypes.push_back("Mod - Test Theft");
    ruleset::IntelProject theft;
    theft.name = "Test Theft";
    theft.description = "Takes a little.";
    theft.group = "Espionage";
    theft.cost = 50;
    theft.type = "Mod - Test Theft";
    theft.effectAmount = 100;
    rs.intelProjects.push_back(theft);
    rs.reindex();
    return rs;
}

inline const game::Rules& rulesFixtureRules() {
    static const game::Rules r{rulesRuleset({&rulesFixtureMod()})};
    return r;
}

// The SDK installed for one test, with the rules fixture (and the AI fixture)
// among the mods its sessions find, and OpenSE4's own opense4 package.
struct InstalledRules {
    explicit InstalledRules(sdk::PlayerSetup setup = {}) {
        setup.mods.push_back(rulesFixtureMod());
        setup.mods.push_back(aiFixtureMod());
        sdk::installPlayers(std::move(setup));
    }
    ~InstalledRules() { sdk::uninstallPlayers(); }
    InstalledRules(const InstalledRules&) = delete;
    InstalledRules& operator=(const InstalledRules&) = delete;
};

inline game::GameState rulesGame(uint64_t seed, bool simultaneous, int empires = 4, int systems = 14,
                                 std::vector<game::Controller> controllers = {}) {
    std::vector<game::Controller> all(static_cast<size_t>(empires));
    for (size_t i = 0; i < controllers.size() && i < all.size(); ++i) all[i] = controllers[i];
    auto created = game::createGame(rulesFixtureRules(), playersSetup(seed, simultaneous, all, systems));
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    return std::move(*created);
}

// One mod's data on a list's thing, as a value (an empty map when none).
inline script::Value modDataIn(const std::vector<game::ModData>& list, std::string_view mod = kRulesMod) {
    for (const game::ModData& d : list)
        if (d.mod == mod) {
            auto v = script::parseJson(d.value);
            REQUIRE_MESSAGE(v.has_value(), d.value);
            return *v;
        }
    return script::Value::emptyMap();
}

inline int64_t callsOf(const game::GameState& s, std::string_view hook) {
    const script::Value data = modDataIn(s.modData);
    const script::Value* calls = data.find("calls");
    const script::Value* n = calls ? calls->find(hook) : nullptr;
    return n && n->isInt() ? n->asInt() : 0;
}

inline script::Value lastOf(const game::GameState& s, std::string_view hook) {
    const script::Value data = modDataIn(s.modData);
    const script::Value* last = data.find("last");
    const script::Value* v = last ? last->find(hook) : nullptr;
    return v ? *v : script::Value();
}

inline const game::ModRulesState* rulesStateOf(const game::GameState& s, std::string_view mod = kRulesMod) {
    for (const game::ModRulesState& m : s.modRules)
        if (m.mod == mod) return &m;
    return nullptr;
}

} // namespace opense4::sdktest
