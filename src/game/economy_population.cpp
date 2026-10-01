#include "game/economy.hpp"

#include "datafile/datafile.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/economy_internal.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <optional>

// Population (docs/spec/02 §2-4): growth, replicants, domes, plague,
// atmosphere converters, planet value and conditions changes, happiness, and
// the colony's end when its people die out.

namespace opense4::game::economy {

using namespace detail;
using xmath::Ext;
using xmath::pctTrunc;

namespace {

// The empire's colonies in object order (stable), as planet ids: steps may remove colonies.
std::vector<ObjectId> coloniesOf(const GameState& s, EmpireId e) {
    std::vector<ObjectId> out;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) out.push_back(c->planet);
    return out;
}

// ---- Population arithmetic ----------------------------------------------------------------------

// Index of the largest group (the first one on ties), or -1.
int largestGroup(const Colony& c) {
    int best = -1;
    for (size_t i = 0; i < c.population.size(); ++i)
        if (best < 0 || c.population[i].millions > c.population[static_cast<size_t>(best)].millions) best = static_cast<int>(i);
    return best;
}

EmpireId majorityRace(const Colony& c) {
    const int i = largestGroup(c);
    return i >= 0 && c.population[static_cast<size_t>(i)].millions > 0 ? c.population[static_cast<size_t>(i)].race : c.owner;
}

// ---- Growth (spec 02 §3) ---------------------------------------------------------------------------

void growColony(const Rules& r, GameState& s, Colony& c) {
    const int rate = reproductionPercent(r, s, c);
    int64_t room = std::max<int64_t>(0, maxPopulation(r, s, c) - c.totalPopulation());
    // Earlier races fill the free room first (confirmed: binary).
    const Ext perTurn = xmath::percent(rate) / Ext(10);  // % per year, 10 turns a year
    for (PopulationGroup& g : c.population) {
        if (room <= 0) break;
        if (g.millions <= 0) continue;
        int64_t grown = (Ext(g.millions) * perTurn).round();
        if (rate > 0 && grown == 0) grown = 1;
        grown = std::min(grown, room);
        g.millions += grown;
        room -= grown;
    }
}

// ---- Planet value and conditions (spec 02 §1.5, §2) ------------------------------------------------------

// Stores a facility's product as the planet's new conditions (spec 02 §1.5, §2,
// confirmed: binary): never above 1.5, and the owner is told when they reach
// it; a product of exactly 0 gives 0.1. The product is the real number itself,
// not rounded (spec 02 §13 Q46).
void setMultipliedConditions(TurnContext& ctx, const Colony& c, Ext product) {
    SpaceObject& p = ctx.state.galaxy.object(c.planet);
    const Conditions before = p.conditions;
    if (product > kOptimalConditions.value()) p.conditions = kOptimalConditions;
    else if (product.isZero()) p.conditions = Conditions::of(Ext(1) / Ext(10));
    else p.conditions = Conditions::of(product);
    if (p.conditions == kOptimalConditions && before < kOptimalConditions)
        ctx.log(c.owner, LogCategory::Misc, std::format("{} has optimal conditions", p.name), {}, locationOf(ctx.state.galaxy, c.planet));
}

// A value change: normal games add points, finite games a percentage of the stock (truncated).
void changeValue(const Rules& r, GameState& s, SpaceObject& p, size_t k, int64_t amount, bool systemWide) {
    if (!s.options.finiteResources) {
        p.value[k] = clampedValue(r, s, int64_t{p.value[k]} + amount);
        return;
    }
    const int64_t stock = std::max(0, p.value[k]);
    p.value[k] = clampedValue(r, s, systemWide ? pctTrunc(stock, 100 + amount) : stock + pctTrunc(stock, amount));
}

constexpr std::array<AbilityKind, 3> kValueChange{AbilityKind::PlanetChangeMineralsValue, AbilityKind::PlanetChangeOrganicsValue,
                                                  AbilityKind::PlanetChangeRadioactivesValue};

// Every 10th turn: the colony's own `Planet - Change ... Value` (summed) and
// `Planet - Change Conditions` (summed; only a positive sum acts).
void planetChanges(TurnContext& ctx, Colony& c) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const std::vector<ParsedAbility> own = workingAbilities(r, s, c);
    SpaceObject& p = s.galaxy.object(c.planet);
    for (size_t k = 0; k < 3; ++k)
        if (const int64_t v = sumValue1(own, kValueChange[k]); v != 0) changeValue(r, s, p, k, v, false);
    // conditions × (1 + Val1 / 100), Val1 being the colony's sum.
    if (const int64_t v = sumValue1(own, AbilityKind::PlanetChangeConditions); v > 0)
        setMultipliedConditions(ctx, c, p.conditions.value() * (Ext(1) + Ext(v) / Ext(100)));
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

// Millions a plague of this level kills at least each turn (confirmed: binary).
int64_t plagueBase(int level) {
    static constexpr std::array<int64_t, 6> kBase{10, 50, 100, 150, 300, 500};
    return kBase[static_cast<size_t>(std::clamp(level, 1, 6) - 1)];
}

void cure(TurnContext& ctx, Colony& c) {
    c.plagueLevel = 0;
    ctx.log(c.owner, LogCategory::Events, std::format("Plague on {} cured", ctx.state.galaxy.object(c.planet).name), {},
            locationOf(ctx.state.galaxy, c.planet));
}

// Returns false when the colony died out and was removed.
bool plague(TurnContext& ctx, Colony& c) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (c.plagueLevel <= 0) return true;
    if (c.totalPopulation() <= 0) {
        c.plagueLevel = 0;
        return true;
    }
    // No Plagues cures it the next time it would strike, with no loss.
    if (r.hasTrait(s.empire(c.owner).race, "No Plagues") || medicalBayAt(r, s, c) >= c.plagueLevel) {
        cure(ctx, c);
        return true;
    }
    const int64_t base = plagueBase(c.plagueLevel);
    const int64_t dead = s.rng.range(base, base + base / 5);
    const std::string name = s.galaxy.object(c.planet).name;
    if (dead >= c.totalPopulation()) {
        ctx.log(c.owner, LogCategory::Events, std::format("Plague wiped out {}", name), "Every inhabitant died of the plague.",
                locationOf(s.galaxy, c.planet));
        colonyDiesOut(ctx, c.planet, "plague");
        return false;
    }
    int64_t left = dead;  // taken from the races in their stored order
    for (PopulationGroup& g : c.population) {
        const int64_t n = std::min(left, g.millions);
        g.millions -= n;
        left -= n;
    }
    trimCargoToCapacity(r, s, c);  // cargo above the capacity goes now (spec 02 §2)
    ctx.log(c.owner, LogCategory::Events, std::format("Plague on {}", name), std::format("{}M died of the plague this turn.", dead),
            locationOf(s.galaxy, c.planet));
    return true;
}

// ---- Atmosphere converters (spec 02 §2) ------------------------------------------------------------------

// The atmosphere counter goes no higher (confirmed: binary).
constexpr int kMaxAtmosphereTurns = 200;

void convertAtmosphere(TurnContext& ctx, Colony& c) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    bool converter = false;
    const int64_t turns = bestOf(workingAbilities(r, s, c), AbilityKind::PlanetChangeAtmosphere, &converter);
    SpaceObject& planet = s.galaxy.object(c.planet);
    const std::string& target = s.empire(majorityRace(c)).race.atmosphere;
    // The counter moves only while a converter works on the wrong atmosphere; on
    // other turns it keeps its value, and the count resumes later (confirmed: binary).
    if (!converter || turns <= 0 || datafile::keysEqual(planet.atmosphere, target)) return;
    c.atmosphereTurns = std::min(c.atmosphereTurns + 1, kMaxAtmosphereTurns);
    if (c.atmosphereTurns <= turns) return;  // more than Val1 turns: Val1 + 1 in all (confirmed: binary)
    c.atmosphereTurns = 0;
    ctx.log(c.owner, LogCategory::Misc, std::format("{} now has a {} atmosphere", planet.name, target), {}, locationOf(s.galaxy, c.planet));
    planet.atmosphere = target;
}

// ---- Happiness (spec 02 §4) ---------------------------------------------------------------------------

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

int64_t triggerValue(const ruleset::HappinessModel* m, std::string_view trigger) {
    if (!m) return 0;
    for (const auto& [key, value] : m->triggers)
        if (datafile::keysEqual(key, trigger)) return value;
    return 0;
}

// Ships and bases in a system that count for happiness: not destroyed, cloaked or mothballed.
struct Presence {
    int ourSector = 0, ourSystem = 0, enemySector = 0, enemySystem = 0;
};

Presence presenceAt(const Rules& r, const GameState& s, const Colony& c) {
    Presence p;
    const Location where = locationOf(s.galaxy, c.planet);
    for (const Vehicle& v : s.vehicles) {
        if (v.location.system != where.system || v.count <= 0 || v.status != VehicleStatus::Normal) continue;
        if (!isShipOrBase(vehicleType(r, s, v))) continue;
        // Ships without an owner never count; Non-Aggression or better counts as ours (confirmed: binary).
        if (!v.owner.valid()) continue;
        const bool ours = v.owner == c.owner || !hostile(s, c.owner, v.owner);
        const bool here = v.location.sector == where.sector;
        (ours ? p.ourSystem : p.enemySystem) += 1;
        if (here) (ours ? p.ourSector : p.enemySector) += 1;
    }
    return p;
}

void updateColonyAnger(TurnContext& ctx, Colony& c, int64_t empireWide, std::span<const MoodEvent> events) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const ruleset::HappinessModel* model = modelOf(r, s.empire(c.owner).race);
    const auto v = [&](std::string_view key) { return triggerValue(model, key); };
    const Location where = locationOf(s.galaxy, c.planet);
    int64_t total = empireWide;  // tenths of a percent

    // 1. Drift towards Indifferent; other races resent their rulers.
    int64_t own = 0;
    for (const PopulationGroup& g : c.population)
        if (g.race == c.owner) own += g.millions;
    if (own < c.totalPopulation() / 2) {
        total += v("Natural Decrease for Other Races");
    } else {
        switch (moodFromAnger(c.anger)) {
            case Mood::Rioting:
            case Mood::Angry:
            case Mood::Unhappy: total += v("Natural Decrease"); break;
            case Mood::Happy:
            case Mood::Jubilant: total -= v("Natural Decrease"); break;
            case Mood::Indifferent: break;
        }
    }

    // 2-3. Events in the colony's sector and anywhere in its system.
    for (const MoodEvent& ev : events) {
        const int64_t d = v(ev.trigger) * std::max(1, ev.count);
        if (d == 0) continue;
        const Scope scope = scopeOf(ev.trigger);
        if (scope == Scope::System) {
            SystemId sys = ev.system;
            if (!sys.valid() && ev.planet.valid() && ev.planet.index() < s.galaxy.objects.size()) sys = s.galaxy.object(ev.planet).system;
            if (sys == where.system) total += d;
        } else if (scope == Scope::Location && ev.planet.valid() && ev.planet.index() < s.galaxy.objects.size() &&
                   locationOf(s.galaxy, ev.planet) == where) {
            total += d;
        }
    }

    // 4. Ships present, counted per ship; the sector value replaces the system one.
    const Presence p = presenceAt(r, s, c);
    if (p.enemySector > 0) total += v("Enemy Ship in Sector") * p.enemySector;
    else total += v("Enemy Ship in System") * p.enemySystem;
    if (p.ourSector > 0) total += v("Our Ship in Sector") * p.ourSector;
    else total += v("Our Ship in System") * p.ourSystem;

    // 5. Troops (confirmed: binary): troops another empire landed that still
    //    fight for the planet count once as enemies, whatever the treaty; they
    //    are kept apart from the cargo (Colony::landedTroops, spec 04 §13),
    //    and combat::invaders names their empire. Every troop unit in the
    //    colony's cargo counts as ours, whoever owns it.
    int64_t ourTroops = 0;
    for (const UnitStack& u : c.cargo.units)
        if (u.count > 0 && combat::isTroopDesign(r, s, u.design)) ourTroops += u.count;
    if (!combat::invaders(r, s, c).empty()) total += v("Enemy Troops on Planet");
    total += v("Our Troops on Planet") * ourTroops;

    // 6. Plague.
    if (c.plagueLevel > 0) total += v("Planet Plagued");

    // Whole percent (truncated), plus the colony's own happiness facilities (positive angers).
    int64_t change = total / 10 + sumValue1(workingAbilities(r, s, c), AbilityKind::PlanetChangePopulationHappiness);
    if (model) {
        // The data gives the negative limit as a negative number; a positive one is read the same (inferred).
        if (change < 0) change = std::max<int64_t>(change, -std::abs(model->maxNegativeChange) / 10);
        else change = std::min<int64_t>(change, model->maxPositiveChange / 10);
    }
    setAnger(r, s, c, static_cast<int>(std::clamp<int64_t>(c.anger + change, -kMaxAnger, 2 * kMaxAnger)));
    if (moodFromAnger(c.anger) == Mood::Rioting)
        ctx.log(c.owner, LogCategory::Misc, std::format("Riots on {}", s.galaxy.object(c.planet).name),
                "The population produces nothing, builds nothing and does not grow until calm returns.", where);
}

} // namespace

// ---- Cargo over capacity ------------------------------------------------------------------------

void trimCargoToCapacity(const Rules& r, const GameState& s, Colony& c, int64_t deadSpace, bool keepEmptyStacks) {
    const int64_t capacity = colonyCargoCapacity(r, s, c);
    auto over = [&] { return cargoSpaceUsed(r, s, c.cargo) + deadSpace > capacity; };
    if (!over()) return;
    // With no population left, every troop unit goes first (confirmed: binary).
    if (c.totalPopulation() <= 0)
        for (UnitStack& u : c.cargo.units)
            if (combat::isTroopDesign(r, s, u.design)) u.count = 0;
    while (over()) {
        // Then population, 1M at a time, from the first group (confirmed: binary).
        if (auto g = std::find_if(c.cargo.population.begin(), c.cargo.population.end(), [](const PopulationGroup& p) { return p.millions > 0; });
            g != c.cargo.population.end()) {
            if (--g->millions <= 0) c.cargo.population.erase(g);
            continue;
        }
        // Then units one at a time from the first stack.
        auto u = std::find_if(c.cargo.units.begin(), c.cargo.units.end(), [](const UnitStack& x) { return x.count > 0; });
        if (u == c.cargo.units.end()) break;
        --u->count;
    }
    if (!keepEmptyStacks) std::erase_if(c.cargo.units, [](const UnitStack& x) { return x.count <= 0; });
}

// ---- Colonies ending ------------------------------------------------------------------------------

void colonyDiesOut(TurnContext& ctx, ObjectId planet, std::string_view cause) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Colony* c = s.colony(planet);
    if (!c) return;
    const EmpireId owner = c->owner;
    // `Homeworld Lost` when the colony had the first default colony type (confirmed: binary).
    const auto& types = r.data().names.colonyTypes;
    const bool home = types.empty() ? c->homeworld : datafile::keysEqual(c->colonyType, types.front());
    SpaceObject& p = s.galaxy.object(planet);
    const int64_t loss = r.setting("Planet Value Percent Loss After Owner Death", 10);
    for (size_t k = 0; k < 3; ++k)
        p.value[k] = clampedValue(r, s, p.value[k] - (s.options.finiteResources ? pctTrunc(std::max(0, p.value[k]), loss) : loss));
    s.colonies[planet.index()].reset();
    ctx.mood(owner, home ? "Homeworld Lost" : "Any Planet Lost", p.system, planet);
    std::string line = std::format("The colony on {} died out", p.name);
    if (!cause.empty()) line += std::format(": {}", cause);
    addHistory(s, owner, owner, std::move(line), locationOf(s.galaxy, planet));
}

// ---- Per-empire steps -------------------------------------------------------------------------------

void processPlanets(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    // Both tests use the date in tenths of a year (spec 02 §13 Q48, confirmed: binary).
    const auto freq = static_cast<uint32_t>(std::max<int64_t>(1, r.setting("Reproduction Check Frequency", 1)));
    const bool reproduce = processingDate(s) % freq == 0;  // the amount is not scaled by the frequency
    const bool yearly = processingDate(s) % 10 == 0;
    for (ObjectId planet : coloniesOf(s, e)) {
        Colony& c = *s.colony(planet);
        // Population above the maximum (a dome went up, a capture) is never
        // removed: such a colony just has no room to grow (spec 02 §2, §13 Q33).
        if (reproduce) growColony(r, s, c);
        if (yearly) planetChanges(ctx, c);
        if (!plague(ctx, c)) continue;
        convertAtmosphere(ctx, c);
    }
}

void updateHappiness(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // This empire's events since the last update; they are used up here.
    std::vector<MoodEvent> events;
    for (const MoodEvent& ev : ctx.moodEvents)
        if (ev.empire == e) events.push_back(ev);
    std::erase_if(ctx.moodEvents, [&](const MoodEvent& ev) { return ev.empire == e; });
    if (!livingEmpire(s, e) || emotionless(r, s, e)) return;  // Emotionless: the whole update is skipped

    // The empire-wide part: events that hit every colony, and the race's calm.
    const Race& race = s.empire(e).race;
    const ruleset::HappinessModel* model = modelOf(r, race);
    int64_t empireWide = 0;
    for (const MoodEvent& ev : events)
        if (scopeOf(ev.trigger) == Scope::Empire) empireWide += triggerValue(model, ev.trigger) * std::max(1, ev.count);
    empireWide -= racialEffect(r, race, RacialEffect::Happiness) / 5;
    std::erase_if(events, [](const MoodEvent& ev) { return scopeOf(ev.trigger) == Scope::Empire; });

    for (ObjectId planet : coloniesOf(s, e)) updateColonyAnger(ctx, *s.colony(planet), empireWide, events);
}

void applySystemAbilities(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    const bool yearly = processingDate(s) % 10 == 0;
    for (ObjectId planet : coloniesOf(s, e)) {
        Colony& c = *s.colony(planet);
        const SystemId sys = s.galaxy.object(planet).system;
        // Happiness facilities calm by whole percent, outside the per-turn clamp.
        if (const int64_t calm = bestInSystem(r, s, e, sys, AbilityKind::ChangePopulationHappinessSystem); calm > 0)
            setAnger(r, s, c, static_cast<int>(std::max<int64_t>(-kMaxAnger, c.anger - calm)));
        // Replicants (confirmed: binary): each race in list order gets round(P × q),
        // ties to even, where q = its population ÷ the colony's, stored as a
        // double first; each share is capped by the room left at that moment.
        if (const int64_t added = bestInSystem(r, s, e, sys, AbilityKind::ChangePopulationSystem); added > 0) {
            const int64_t total = c.totalPopulation();
            const int64_t maxPop = maxPopulation(r, s, c);
            for (PopulationGroup& g : c.population) {
                const int64_t room = maxPop - c.totalPopulation();
                if (total <= 0 || room <= 0) break;
                const Ext q = (Ext(g.millions) / Ext(total)).roundedTo(xmath::kDoubleBits);
                const int64_t share = std::min(room, std::max<int64_t>(0, (Ext(added) * q).round()));
                g.millions += share;
            }
        }
        if (c.plagueLevel > 0 && bestInSystem(r, s, e, sys, AbilityKind::PlaguePreventionSystem) >= c.plagueLevel) cure(ctx, c);
        if (yearly) {
            SpaceObject& p = s.galaxy.object(planet);
            if (const int64_t v = bestInSystem(r, s, e, sys, AbilityKind::PlanetValueChangeSystem); v > 0)
                for (size_t k = 0; k < 3; ++k) changeValue(r, s, p, k, v, true);
            // conditions × (100 + Val1) / 100.
            if (const int64_t v = bestInSystem(r, s, e, sys, AbilityKind::PlanetConditionsChangeSystem); v > 0)
                setMultipliedConditions(ctx, c, p.conditions.value() * Ext(100 + v) / Ext(100));
        }
    }
}

} // namespace opense4::game::economy
