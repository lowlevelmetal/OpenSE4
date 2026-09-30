#include "game/economy.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/economy_internal.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>
#include <optional>

// Population (docs/spec/02 §3-4): growth, replicants, domes, plague, riots,
// rebellion, atmosphere converters and the per-turn mood update.

namespace opense4::game::economy {

using namespace detail;

namespace {

// ---- Population arithmetic ----------------------------------------------------------------------

// Index of the largest group (the first one on ties), or -1.
int largestGroup(const Colony& c) {
    int best = -1;
    for (size_t i = 0; i < c.population.size(); ++i)
        if (best < 0 || c.population[i].millions > c.population[static_cast<size_t>(best)].millions) best = static_cast<int>(i);
    return best;
}

// Adds `amount` M split across the races present in proportion to their size;
// a colony without people receives the owner's race.
void addPopulation(Colony& c, int64_t amount) {
    if (amount <= 0) return;
    const int64_t total = c.totalPopulation();
    if (total <= 0) {
        for (PopulationGroup& g : c.population)
            if (g.race == c.owner) {
                g.millions += amount;
                return;
            }
        c.population.push_back({c.owner, amount});
        return;
    }
    int64_t given = 0;
    for (PopulationGroup& g : c.population) {
        const int64_t share = amount * g.millions / total;
        g.millions += share;
        given += share;
    }
    c.population[static_cast<size_t>(largestGroup(c))].millions += amount - given;
}

// Removes `amount` M in proportion to each race's size.
void removePopulation(Colony& c, int64_t amount) {
    const int64_t total = c.totalPopulation();
    if (amount <= 0 || total <= 0) return;
    amount = std::min(amount, total);
    int64_t taken = 0;
    for (PopulationGroup& g : c.population) {
        const int64_t share = amount * g.millions / total;
        g.millions -= share;
        taken += share;
    }
    for (int64_t left = amount - taken; left > 0;) {
        PopulationGroup& g = c.population[static_cast<size_t>(largestGroup(c))];
        const int64_t n = std::min(left, g.millions);
        if (n <= 0) break;
        g.millions -= n;
        left -= n;
    }
}

EmpireId majorityRace(const Colony& c) {
    const int i = largestGroup(c);
    return i >= 0 && c.population[static_cast<size_t>(i)].millions > 0 ? c.population[static_cast<size_t>(i)].race : c.owner;
}

// ---- Growth (spec 02 §3) ---------------------------------------------------------------------------

void growColony(const Rules& r, GameState& s, Colony& c, bool reproduce, int freq) {
    const int64_t maxPop = maxPopulation(r, s, c);
    if (reproduce && c.totalPopulation() > 0) {
        std::vector<int64_t> gain(c.population.size(), 0);
        int64_t totalGain = 0;
        for (size_t i = 0; i < c.population.size(); ++i) {
            PopulationGroup& g = c.population[i];
            if (g.millions <= 0) continue;
            const int rate = reproductionPercent(r, s, c, g.race);
            const int64_t change = g.millions * rate * freq / 1000;  // % per year, 10 turns a year
            if (rate > 0) {
                gain[i] = std::max<int64_t>(1, change);  // small colonies still grow (inferred)
                totalGain += gain[i];
            } else if (rate < 0) {
                g.millions = std::max<int64_t>(1, g.millions + change);  // decline never empties a colony (inferred)
            }
        }
        const int64_t room = std::max<int64_t>(0, maxPop - c.totalPopulation());
        if (totalGain > room) {
            // Not enough room: share it in proportion to each race's growth.
            int64_t given = 0, bestIdx = -1;
            for (size_t i = 0; i < gain.size(); ++i) {
                const int64_t g = totalGain > 0 ? gain[i] * room / totalGain : 0;
                if (bestIdx < 0 || gain[i] > gain[static_cast<size_t>(bestIdx)]) bestIdx = static_cast<int64_t>(i);
                gain[i] = g;
                given += g;
            }
            if (bestIdx >= 0) gain[static_cast<size_t>(bestIdx)] += room - given;
        }
        for (size_t i = 0; i < gain.size(); ++i) c.population[i].millions += gain[i];
    }

    // Replicant facilities: every turn, the best in the system (spec 02 §3).
    const SystemId sys = s.galaxy.object(c.planet).system;
    const int64_t replicants = bestInSystem(r, s, c.owner, sys, AbilityKind::ChangePopulationSystem);
    if (replicants > 0) addPopulation(c, std::min(replicants, std::max<int64_t>(0, maxPop - c.totalPopulation())));

    // A colony over its capacity (a dome went up) loses the surplus (inferred, spec 02 §13).
    if (const int64_t excess = c.totalPopulation() - maxPop; excess > 0 && maxPop >= 0) {
        removePopulation(c, excess);
        addLog(s, c.owner, LogCategory::Misc, std::format("{} is overcrowded", s.galaxy.object(c.planet).name),
               std::format("{}M could not be housed and were lost.", excess), locationOf(s.galaxy, c.planet));
    }
}

// ---- Plague (spec 02 §3) ------------------------------------------------------------------------------

int64_t medicalBayAt(const Rules& r, const GameState& s, const Colony& c) {
    const Location where = locationOf(s.galaxy, c.planet);
    int64_t best = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.location == where && v.count > 0 && allied(s, c.owner, v.owner))
            best = std::max(best, bestValue1(vehicleAbilities(r, s, v), AbilityKind::MedicalBay));
    return best;
}

void plague(TurnContext& ctx, Colony& c) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (c.plagueLevel <= 0) return;
    const std::string name = s.galaxy.object(c.planet).name;
    const Location where = locationOf(s.galaxy, c.planet);
    if (c.totalPopulation() <= 0) {
        c.plagueLevel = 0;
        return;
    }
    if (plagueProtection(r, s, c) >= c.plagueLevel || medicalBayAt(r, s, c) >= c.plagueLevel) {
        c.plagueLevel = 0;
        ctx.log(c.owner, LogCategory::Events, std::format("Plague on {} cured", name), {}, where);
        return;
    }
    // Each level kills 1 % of every race per turn, at least 1M (inferred, spec 02 §13 Q20).
    int64_t dead = 0;
    for (PopulationGroup& g : c.population) {
        if (g.millions <= 0) continue;
        const int64_t loss = std::min(g.millions, std::max<int64_t>(1, g.millions * c.plagueLevel / 100));
        g.millions -= loss;
        dead += loss;
    }
    ctx.log(c.owner, LogCategory::Events, std::format("Plague on {}", name), std::format("{}M died of the plague this turn.", dead), where);
}

// ---- Riots and rebellion (spec 02 §4) -----------------------------------------------------------------

constexpr int kRebellionRiotTurns = 10;  // (inferred) spec 02 §13 Q10
constexpr int kRebellionPercent = 10;    // chance per turn once rioting that long (inferred)

void foundRebelEmpire(TurnContext& ctx, ObjectId planet) {
    GameState& s = ctx.state;
    const EmpireId old = s.colony(planet)->owner;
    const EmpireId people = majorityRace(*s.colony(planet));
    const EmpireId id{s.empires.size()};
    const std::string planetName = s.galaxy.object(planet).name;

    Empire rebel;
    {
        const Empire& from = s.empire(old);
        rebel.id = id;
        rebel.name = std::format("Free {}", planetName);
        rebel.empireType = "Rebellion";
        rebel.race = s.empire(people).race;
        rebel.color = defaultEmpireColor(id.index());
        rebel.kind = PlayerKind::Computer;
        rebel.racialPointsSpent = s.empire(people).racialPointsSpent;
        rebel.techLevels = from.techLevels;
        rebel.strategies = from.strategies;
        rebel.designTypes = from.designTypes;
        rebel.colonyTypes = from.colonyTypes;
        rebel.repairPriorities = from.repairPriorities;
        rebel.relations.assign(s.empires.size() + 1, Relation{});
        const size_t systems = s.galaxy.systems.size();
        rebel.knowledge.explored.assign(systems, s.options.allSystemsSeen ? 1 : 0);
        rebel.knowledge.present.assign(systems, 0);
        rebel.knowledge.lastSeen.assign(systems, 0);
        rebel.knowledge.notes.assign(systems, {});
        rebel.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), s.options.allSystemsSeen ? 1 : 0);
        rebel.knowledge.explored[s.galaxy.object(planet).system.index()] = 1;
    }
    for (Empire& e : s.empires) e.relations.resize(id.index() + 1);
    s.empires.push_back(std::move(rebel));  // invalidates Empire references

    Colony& c = *s.colony(planet);
    const bool wasHome = c.homeworld;
    c.owner = id;
    for (PopulationGroup& g : c.population)
        if (g.race == old) g.race = id;  // the rebels are the new nation's own people
    c.anger = 200;
    c.riotTurns = 0;
    c.homeworld = false;
    c.minister = false;
    c.queue = ConstructionQueue{};
    s.empire(id).claimedSystems.push_back(s.galaxy.object(planet).system);

    const Location where = locationOf(s.galaxy, planet);
    ctx.log(old, LogCategory::Misc, std::format("{} has rebelled", planetName), "Years of rioting ended in open revolt; the planet is lost.",
            where);
    ctx.log(id, LogCategory::Misc, std::format("{} declares independence", planetName), {}, where);
    ctx.mood(old, "Any Planet Lost");
    if (wasHome) ctx.mood(old, "Homeworld Lost");
}

void riots(TurnContext& ctx) {
    GameState& s = ctx.state;
    std::vector<ObjectId> rebels;
    for (auto& c : s.colonies) {
        if (!c) continue;
        if (c->totalPopulation() <= 0 || moodFromAnger(c->anger) != Mood::Rioting ||
            ctx.rules.hasTrait(s.empire(c->owner).race, "Population Emotionless")) {
            c->riotTurns = 0;
            continue;
        }
        if (++c->riotTurns == 1)
            ctx.log(c->owner, LogCategory::Misc, std::format("Riots on {}", s.galaxy.object(c->planet).name),
                    "The population produces nothing and builds nothing until calm returns.", locationOf(s.galaxy, c->planet));
        if (c->riotTurns >= kRebellionRiotTurns && s.rng.percent(kRebellionPercent)) rebels.push_back(c->planet);
    }
    for (ObjectId p : rebels) foundRebelEmpire(ctx, p);
}

// ---- Atmosphere converters ------------------------------------------------------------------------------

void convertAtmosphere(TurnContext& ctx, Colony& c) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    int64_t turns = 0;
    if (facilitiesWork(c))
        for (uint32_t f : c.facilities)
            for (const auto& a : r.facilityAbilities(f))
                if (a.kind == AbilityKind::PlanetChangeAtmosphere && a.value1 > 0 && (turns == 0 || a.value1 < turns)) turns = a.value1;
    SpaceObject& planet = s.galaxy.object(c.planet);
    const std::string& target = s.empire(majorityRace(c)).race.atmosphere;
    if (turns == 0 || datafile::keysEqual(planet.atmosphere, target)) {
        c.atmosphereCountdown = -1;
        return;
    }
    if (c.atmosphereCountdown < 0) c.atmosphereCountdown = static_cast<int>(turns);
    if (--c.atmosphereCountdown > 0) return;
    ctx.log(c.owner, LogCategory::Misc, std::format("{} now has a {} atmosphere", planet.name, target), {}, locationOf(s.galaxy, c.planet));
    planet.atmosphere = target;
    c.atmosphereCountdown = -1;
}

// ---- Mood (spec 02 §4) ---------------------------------------------------------------------------------

enum class Scope { Empire, System, Location };

// Happiness.txt names its triggers after their scope: "Any ..."/"New Treaty ..."
// hit every colony, "... in System" the system, the rest one sector.
Scope scopeOf(std::string_view trigger) {
    const std::string key = datafile::normalizeKey(trigger);
    if (key.starts_with("any ") || key.starts_with("new treaty") || key.starts_with("homeworld")) return Scope::Empire;
    if (key.find(" in system") != std::string::npos) return Scope::System;
    return Scope::Location;
}

const ruleset::HappinessModel* modelOf(const Rules& r, const Race& race) {
    const auto& models = r.data().happinessModels;
    return race.happinessModel < models.size() ? &models[race.happinessModel] : nullptr;
}

std::optional<int> triggerValue(const ruleset::HappinessModel* m, std::string_view trigger) {
    if (!m) return std::nullopt;
    for (const auto& [key, value] : m->triggers)
        if (datafile::keysEqual(key, trigger)) return value;
    return std::nullopt;
}

// A mood change split into its angering and calming parts.
struct Delta {
    int64_t angrier = 0;
    int64_t calmer = 0;  // <= 0
    void add(int64_t d) { (d > 0 ? angrier : calmer) += d; }
};

void updateMood(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    std::vector<Delta> deltas(s.colonies.size());

    // Reported events.
    for (const MoodEvent& ev : ctx.moodEvents) {
        if (!ev.empire.valid() || ev.empire.index() >= s.empires.size()) continue;
        const auto value = triggerValue(modelOf(r, s.empire(ev.empire).race), ev.trigger);
        if (!value || *value == 0) continue;
        const int64_t d = int64_t{*value} * std::max(1, ev.count);
        switch (scopeOf(ev.trigger)) {
            case Scope::Empire:
                for (size_t i = 0; i < s.colonies.size(); ++i)
                    if (s.colonies[i] && s.colonies[i]->owner == ev.empire) deltas[i].add(d);
                break;
            case Scope::System: {
                SystemId sys = ev.system;
                if (!sys.valid() && ev.planet.valid() && ev.planet.index() < s.galaxy.objects.size()) sys = s.galaxy.object(ev.planet).system;
                for (const Colony* c : coloniesInSystem(s, ev.empire, sys)) deltas[c->planet.index()].add(d);
                break;
            }
            case Scope::Location:
                if (!ev.planet.valid() || ev.planet.index() >= s.galaxy.objects.size()) break;
                for (ObjectId o : planetsAt(s, locationOf(s.galaxy, ev.planet)))
                    if (const Colony* c = s.colony(o); c && c->owner == ev.empire) deltas[o.index()].add(d);
                break;
        }
    }

    // Presence of ships and troops, computed from the state (spec 02 §4).
    std::vector<std::vector<const Vehicle*>> bySystem(s.galaxy.systems.size());
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.status != VehicleStatus::Mothballed && v.location.system.index() < bySystem.size())
            bySystem[v.location.system.index()].push_back(&v);

    for (size_t i = 0; i < s.colonies.size(); ++i) {
        if (!s.colonies[i]) continue;
        Colony& c = *s.colonies[i];
        const int64_t population = c.totalPopulation();
        const Race& race = s.empire(c.owner).race;
        if (population <= 0 || r.hasTrait(race, "Population Emotionless")) continue;  // emotionless: anger never changes
        const ruleset::HappinessModel* model = modelOf(r, race);
        auto trigger = [&](std::string_view key) -> int64_t { return triggerValue(model, key).value_or(0); };
        Delta& d = deltas[i];
        const Location where = locationOf(s.galaxy, c.planet);

        // Our ships calm, enemy ships anger; the sector effect replaces the system one (inferred: once per colony).
        bool ourSector = false, ourSystem = false, enemySector = false, enemySystem = false;
        for (const Vehicle* v : bySystem[where.system.index()]) {
            const ruleset::VehicleType t = vehicleType(r, s, *v);
            const bool here = v->location.sector == where.sector;
            if (v->owner == c.owner) {
                if (isShipOrBase(t)) (here ? ourSector : ourSystem) = true;
            } else if (hostile(s, c.owner, v->owner) && v->status != VehicleStatus::Cloaked && t != ruleset::VehicleType::Mine) {
                (here ? enemySector : enemySystem) = true;
            }
        }
        if (ourSector) d.add(trigger("Our Ship in Sector"));
        else if (ourSystem) d.add(trigger("Our Ship in System"));
        if (enemySector) d.add(trigger("Enemy Ship in Sector"));
        else if (enemySystem) d.add(trigger("Enemy Ship in System"));
        int troops = 0;
        for (const UnitStack& u : c.cargo.units)
            if (r.hull(s.design(u.design).hull).type == ruleset::VehicleType::Troop) troops += u.count;
        if (troops > 0) d.add(trigger("Our Troops on Planet") * troops);

        // Natural drift: the owner's race calms, other races resent their rulers.
        int64_t own = 0;
        for (const PopulationGroup& g : c.population)
            if (g.race == c.owner) own += g.millions;
        d.add((trigger("Natural Decrease") * own + trigger("Natural Decrease for Other Races") * (population - own)) / population);

        // Conditions, then happiness facilities: 1 % = 10 tenths (inferred).
        d.add(conditionsAnger(conditionsBand(s.galaxy.object(c.planet).conditions), race.characteristic(Characteristic::EnvironmentalResistance)));
        const int64_t calming = std::max(bestOf(workingAbilities(r, s, c), AbilityKind::PlanetChangePopulationHappiness),
                                         bestInSystem(r, s, c.owner, where.system, AbilityKind::ChangePopulationHappinessSystem));
        if (calming > 0) d.add(-calming * 10);

        // Happiness characteristic and culture speed up calming (inferred).
        const ruleset::Culture* culture = r.culture(race);
        const int64_t calmPct = std::max(0, 100 + charBonus(race, Characteristic::Happiness) + (culture ? culture->happiness : 0));
        int64_t change = d.angrier + d.calmer * calmPct / 100;
        if (model && (model->maxPositiveChange != 0 || model->maxNegativeChange != 0))
            change = std::clamp<int64_t>(change, std::min(model->maxNegativeChange, 0), std::max(model->maxPositiveChange, 0));
        c.anger = static_cast<int>(std::clamp<int64_t>(c.anger + change, 0, 1000));
    }
}

} // namespace

void runPopulation(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int freq = static_cast<int>(std::max<int64_t>(1, r.setting("Reproduction Check Frequency", 1)));
    const bool reproduce = s.turn % static_cast<uint32_t>(freq) == 0;

    // Growth under this turn's mood, replicants, domes; then plague.
    for (auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < s.empires.size()) {
            growColony(r, s, *c, reproduce, freq);
            plague(ctx, *c);
        }
    // Riots and rebellion follow the mood the colony had this turn.
    riots(ctx);
    for (auto& c : s.colonies)
        if (c) convertAtmosphere(ctx, *c);
    // Last, the mood for next turn, with every event reported during this turn.
    updateMood(ctx);
}

} // namespace opense4::game::economy
