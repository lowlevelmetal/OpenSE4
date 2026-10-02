#pragma once

// Test rules for research, intelligence, diplomacy, events and scores: the
// engine test rules plus one invented intelligence project and one invented
// event per effect type, a mood model with the treaty triggers, a few racial
// traits and the Settings keys these modules read. All names and texts are
// our own.

#include "engine_fixture.hpp"

#include "game/events.hpp"
#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <format>
#include <string>

namespace opense4::test {

inline ruleset::Ruleset buildPoliticsRuleset() {
    using game::effects::Effect;
    ruleset::Ruleset rs = buildEngineRuleset();

    // ---- Intelligence projects: one per effect type, no technology needed.
    rs.intelProjects.clear();
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        ruleset::IntelProject p;
        p.name = std::format("Probe {}", i);
        p.group = game::effects::isSabotage(e) ? "Test Sabotage" : "Test Espionage";
        p.cost = 1000;
        p.type = std::string(game::effects::identifier(e));
        p.effectAmount = 1;
        p.sourceMessages = {"Done to [%TargetEmpireName]: [%VehicleName][%PlanetName][%TechName][%DesignName] in [%SystemName]."};
        p.targetMessages = {{"Probe Hit", "They got [%VehicleName][%PlanetName][%TechName] near [%SystemName]."}};
        p.sourcePicture = "ProbeUs";
        p.targetPicture = "ProbeThem";
        rs.intelProjects.push_back(std::move(p));
    }
    for (int level = 1; level <= 3; ++level) {
        ruleset::IntelProject p;
        p.name = std::format("Shield Level {}", level);
        p.group = "Test Defense";
        p.cost = 100000 * level;
        p.type = "Intelligence Defense";
        p.effectAmount = level;
        p.sourceMessages = {"Stopped the [%TargetEmpireName]."};
        p.targetMessages = {{"Probe Stopped", "Our probe against the [%SourceEmpireName] was stopped."}};
        rs.intelProjects.push_back(std::move(p));
    }

    // ---- Events: one per effect type that events can use.
    rs.eventTypes.clear();
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        if (game::effects::needsSource(e) || e == Effect::IntelligenceDefense) continue;
        ruleset::EventType ev;
        ev.type = std::string(game::effects::identifier(e));
        ev.severity = "Low";
        ev.effectAmount = 1;
        ev.messageTo = "Owner";
        ev.messages = {{"Mishap", "Trouble at [%PlanetName][%VehicleName][%StarName][%WarpPointName]: [%ActualAmount]."}};
        ev.picture = "Mishap";
        rs.eventTypes.push_back(std::move(ev));
    }

    // ---- Mood model with the treaty triggers (values are arbitrary).
    ruleset::HappinessModel mood;
    mood.name = "Test Politics Mood";
    mood.maxPositiveChange = 50;
    mood.maxNegativeChange = -50;
    for (const char* t : {"New Treaty War", "New Treaty None", "New Treaty Trade", "New Treaty Subjugated (Sub)", "New Treaty Subjugated (Dom)"})
        mood.triggers.emplace_back(t, 10);
    rs.happinessModels.push_back(std::move(mood));

    // ---- Traits.
    auto trait = [&](std::string name, std::string type, std::string value) {
        ruleset::RacialTrait t;
        t.name = std::move(name);
        t.traitType = std::move(type);
        t.values = {std::move(value)};
        rs.racialTraits.push_back(std::move(t));
    };
    trait("Test Charmed", "Luck", "-100");
    trait("Test Machine Folk", "No Plagues", "0");
    trait("Test Stoics", "Population Emotionless", "0");
    trait("Test Half Luck", "Luck", "-50");

    // ---- A facility that protects its system from bad events and intelligence.
    ruleset::Facility guard;
    guard.name = "Test Security Center";
    guard.group = "Defense";
    guard.family = 90;
    guard.romanNumeral = 1;
    for (auto [kind, value] : {std::pair{game::AbilityKind::ChangeBadEventChanceSystem, -100}, {game::AbilityKind::ChangeBadIntelChanceSystem, -100}}) {
        ruleset::Ability a;
        a.type = std::string(game::identifier(kind));
        a.value1 = std::to_string(value);
        guard.abilities.push_back(a);
    }
    rs.facilities.push_back(guard);
    ruleset::Facility medic;
    medic.name = "Test Clinic";
    medic.group = "Medical";
    medic.family = 91;
    medic.romanNumeral = 1;
    ruleset::Ability heal;
    heal.type = std::string(game::identifier(game::AbilityKind::PlaguePreventionSystem));
    heal.value1 = "3";
    medic.abilities.push_back(heal);
    rs.facilities.push_back(medic);

    auto set = [&](std::string k, std::string v) { rs.settings.set(std::move(k), std::move(v)); };
    set("Maximum Trade Percentage", "20");
    set("Treaty Subjugated Resource Percentage", "40");
    set("Treaty Protectorate Resource Percentage", "20");
    set("Event Percent Chance Low", "5");
    set("Event Percent Chance Medium", "10");
    set("Event Percent Chance High", "100");
    set("Intelligence Defense Modifier Percent", "120");
    set("Minimum Planet Percent Value", "10");
    set("Maximum Planet Percent Value", "150");
    rs.reindex();
    return rs;
}

inline const game::Rules& politicsRules() {
    static const game::Rules rules{buildPoliticsRuleset()};
    return rules;
}

// The per-turn context for calling one phase directly (the only place the
// tests spell out TurnContext's members).
inline game::TurnContext turnContext(const game::Rules& r, game::GameState& s) { return game::TurnContext{r, s, {}, {}, {}}; }

inline game::GameState newPoliticsGame(uint64_t seed = 5, int empires = 3, int systems = 12) {
    game::GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    setup.options.eventFrequency = 0;  // tests turn events on where they want them
    setup.options.simultaneous = true;  // written for simultaneous turns
    for (int i = 0; i < empires; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Realm {}", i + 1);
        e.empireType = "Union";
        e.leaderTitle = "Speaker";
        e.leaderName = std::format("Leader {}", i + 1);
        setup.empires.push_back(std::move(e));
    }
    auto g = game::createGame(politicsRules(), setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    addHomeShips(*g, politicsRules());  // these tests were written with a few ships at home
    return std::move(*g);
}

inline uint32_t projectFor(game::effects::Effect e) {
    const auto& list = politicsRules().data().intelProjects;
    for (uint32_t i = 0; i < list.size(); ++i)
        if (game::effects::parseEffect(list[i].type) == e) return i;
    FAIL("no project");
    return 0;
}

inline uint32_t eventFor(game::effects::Effect e) {
    const auto& list = politicsRules().data().eventTypes;
    for (uint32_t i = 0; i < list.size(); ++i)
        if (game::effects::parseEffect(list[i].type) == e) return i;
    FAIL("no event");
    return 0;
}

inline uint32_t traitIndex(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().racialTraits.size(); ++i)
        if (r.data().racialTraits[i].name == name) return i;
    FAIL("no trait " << name);
    return 0;
}

inline void setContact(game::GameState& s, game::EmpireId a, game::EmpireId b) {
    s.empire(a).relation(b).contact = true;
    s.empire(b).relation(a).contact = true;
}

inline bool hasLog(const game::GameState& s, game::EmpireId e, std::string_view title) {
    for (const auto& l : s.empire(e).log)
        if (l.title == title) return true;
    return false;
}

inline const game::LogEntry* findLog(const game::GameState& s, game::EmpireId e, std::string_view title) {
    for (const auto& l : s.empire(e).log)
        if (l.title == title) return &l;
    return nullptr;
}

inline bool hasMood(const game::TurnContext& ctx, game::EmpireId e, std::string_view trigger) {
    for (const auto& m : ctx.moodEvents)
        if (m.empire == e && m.trigger == trigger) return true;
    return false;
}

} // namespace opense4::test
