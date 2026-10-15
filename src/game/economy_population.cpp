#include "game/log_picture.hpp"
#include "game/economy.hpp"

#include "datafile/datafile.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/economy_internal.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <map>
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
    for (const ObjectRef& ref : objectOrder(s))
        if (const Colony* c = ref.object.valid() ? s.colony(ref.object) : nullptr; c && c->owner == e) out.push_back(ref.object);
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
        ctx.log(c.owner, LogCategory::Misc, std::format("{} has optimal conditions", p.name), {}, locationOf(ctx.state.galaxy, c.planet),
                "PlanetConditionsAtMax");
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
            locationOf(ctx.state.galaxy, c.planet), "PlagueCured");
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
                locationOf(s.galaxy, c.planet), "PlagueBreaksOut");
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
            locationOf(s.galaxy, c.planet), "PlagueBreaksOut");
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
    ctx.log(c.owner, LogCategory::Misc, std::format("{} now has a {} atmosphere", planet.name, target), {}, locationOf(s.galaxy, c.planet),
            logpicture::planet(c.planet));
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

// ---- Mood reports (spec 02 §4 "Mood reports", confirmed: binary) ----------------------------------

// The causes the Planetary Minister reports, in the order the reports go out:
// the happiness record's entries, a cause for each kind of change.
enum class Cause : uint8_t {
    HomeworldLost, PlanetLost, PlanetColonized, OurPlanetCaptured, EnemyPlanetCaptured,
    TreatyWar, TreatyNonIntercourse, TreatyNone, TreatyNonAggression, TreatySubjugatedSub, TreatyProtectorateSub,
    TreatySubjugatedDom, TreatyProtectorateDom, TreatyTrade, TreatyTradeResearch, TreatyMilitaryAlliance, TreatyPartnership,
    ShipLosses, ShipConstruction, BattleWin, BattleLoss, BattleStalemate, EnemyShips, OurShips, EnemyTroops, OurTroops,
    PopulationKilled, FacilityConstruction, Plague, Count
};
using CauseTotals = std::array<int64_t, static_cast<size_t>(Cause::Count)>;  // tenths of a percent

struct CauseName {
    std::string_view trigger;  // a Happiness.txt entry that counts for it
    Cause cause;
};
constexpr std::array<CauseName, 36> kCauseTriggers{{
    {"Homeworld Lost", Cause::HomeworldLost},
    {"Any Planet Lost", Cause::PlanetLost},
    {"Any Planet Colonized", Cause::PlanetColonized},
    {"Any Our Planet Captured", Cause::OurPlanetCaptured},
    {"Any Enemy Planet Captured", Cause::EnemyPlanetCaptured},
    {"New Treaty War", Cause::TreatyWar},
    {"New Treaty Non Intercourse", Cause::TreatyNonIntercourse},
    {"New Treaty None", Cause::TreatyNone},
    {"New Treaty Non Aggression", Cause::TreatyNonAggression},
    {"New Treaty Subjugated (Sub)", Cause::TreatySubjugatedSub},
    {"New Treaty Protectorate (Sub)", Cause::TreatyProtectorateSub},
    {"New Treaty Subjugated (Dom)", Cause::TreatySubjugatedDom},
    {"New Treaty Protectorate (Dom)", Cause::TreatyProtectorateDom},
    {"New Treaty Trade", Cause::TreatyTrade},
    {"New Treaty Trade and Research", Cause::TreatyTradeResearch},
    {"New Treaty Military Alliance", Cause::TreatyMilitaryAlliance},
    {"New Treaty Partnership", Cause::TreatyPartnership},
    {"Any Ship Lost", Cause::ShipLosses},
    {"Ship Lost in System", Cause::ShipLosses},
    {"Any Ship Constructed", Cause::ShipConstruction},
    {"Ship Constructed", Cause::ShipConstruction},
    {"Battle in System - Win", Cause::BattleWin},
    {"Battle in Sector - Win", Cause::BattleWin},
    {"Battle in System - Loss", Cause::BattleLoss},
    {"Battle in Sector - Loss", Cause::BattleLoss},
    {"Battle in System - Stalemate", Cause::BattleStalemate},
    {"Battle in Sector - Stalemate", Cause::BattleStalemate},
    {"Enemy Ship in System", Cause::EnemyShips},
    {"Enemy Ship in Sector", Cause::EnemyShips},
    {"Our Ship in System", Cause::OurShips},
    {"Our Ship in Sector", Cause::OurShips},
    {"Enemy Troops on Planet", Cause::EnemyTroops},
    {"Our Troops on Planet", Cause::OurTroops},
    {"1M Population Killed", Cause::PopulationKilled},
    {"Facility Constructed", Cause::FacilityConstruction},
    {"Planet Plagued", Cause::Plague},
}};

std::optional<Cause> causeOf(std::string_view trigger) {
    for (const CauseName& c : kCauseTriggers)
        if (datafile::keysEqual(c.trigger, trigger)) return c.cause;
    return std::nullopt;
}

// Our own words for each cause, as the reports end them.
std::string_view causePhrase(Cause c) {
    switch (c) {
        case Cause::HomeworldLost: return "the loss of our homeworld";
        case Cause::PlanetLost: return "the loss of our planets";
        case Cause::PlanetColonized: return "our new colonies";
        case Cause::OurPlanetCaptured: return "the capture of our planets";
        case Cause::EnemyPlanetCaptured: return "our capture of enemy planets";
        case Cause::TreatyWar: return "the new war";
        case Cause::TreatyNonIntercourse: return "the treaty of non-intercourse";
        case Cause::TreatyNone: return "the end of a treaty";
        case Cause::TreatyNonAggression: return "the non-aggression pact";
        case Cause::TreatySubjugatedSub: return "our subjugation";
        case Cause::TreatyProtectorateSub: return "our becoming a protectorate";
        case Cause::TreatySubjugatedDom: return "our subjugation of another empire";
        case Cause::TreatyProtectorateDom: return "our new protectorate";
        case Cause::TreatyTrade: return "the trade alliance";
        case Cause::TreatyTradeResearch: return "the trade and research alliance";
        case Cause::TreatyMilitaryAlliance: return "the military alliance";
        case Cause::TreatyPartnership: return "the partnership";
        case Cause::ShipLosses: return "the loss of our ships";
        case Cause::ShipConstruction: return "the building of ships";
        case Cause::BattleWin: return "victory in battle";
        case Cause::BattleLoss: return "defeat in battle";
        case Cause::BattleStalemate: return "an undecided battle";
        case Cause::EnemyShips: return "enemy ships nearby";
        case Cause::OurShips: return "our ships nearby";
        case Cause::EnemyTroops: return "enemy troops on our soil";
        case Cause::OurTroops: return "our troops among them";
        case Cause::PopulationKilled: return "the deaths of our people";
        case Cause::FacilityConstruction: return "the building of facilities";
        case Cause::Plague: return "plague";
        case Cause::Count: break;
    }
    return {};
}

void addCause(CauseTotals& totals, std::optional<Cause> cause, int64_t tenths) {
    if (cause) totals[static_cast<size_t>(*cause)] += tenths;
}

// One report per cause whose total, truncated to whole percent, is above 20
// (growing unhappy) or below −20 (growing happier): Politics entries that
// Goto `where` (spec 02 §4, confirmed: binary).
void reportMoods(TurnContext& ctx, EmpireId e, const CauseTotals& totals, std::optional<SystemId> system) {
    GameState& s = ctx.state;
    for (size_t i = 0; i < totals.size(); ++i) {
        const int64_t pct = totals[i] / 10;
        if (pct >= -20 && pct <= 20) continue;
        const bool unhappy = pct > 20;
        const std::string_view phrase = causePhrase(static_cast<Cause>(i));
        const std::string people = system ? std::format("the people of the {} system", s.galaxy.system(*system).name) : std::string("our people");
        std::string text = std::format("The Planetary Minister reports that {} are growing {} because of {}.", people,
                                       unhappy ? "unhappy" : "happier", phrase);
        std::optional<Location> where;
        if (system) where = Location{*system, Sector{kSystemCenter, kSystemCenter}};
        LogEntry* entry = ctx.log(e, LogCategory::Politics, unhappy ? "Population Growing Unhappy" : "Population Growing Happier", std::move(text),
                                  where, unhappy ? "PopulationAngry" : "PopulationHappy");
        if (!system) logGoto(entry, LogGoto::None);
    }
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

// Updates one colony's anger; adds what each cause gave it (its sector and
// system events, ships present, troops and plague) to `causes`, and returns
// whether its anger changed.
bool updateColonyAnger(TurnContext& ctx, Colony& c, int64_t empireWide, std::span<const MoodEvent> events, CauseTotals& causes) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const ruleset::HappinessModel* model = modelOf(r, s.empire(c.owner).race);
    const auto v = [&](std::string_view key) { return triggerValue(model, key); };
    const Location where = locationOf(s.galaxy, c.planet);
    int64_t total = empireWide;  // tenths of a percent
    const int before = c.anger;

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
        bool counts = false;
        if (scope == Scope::System) {
            SystemId sys = ev.system;
            if (!sys.valid() && ev.planet.valid() && ev.planet.index() < s.galaxy.objects.size()) sys = s.galaxy.object(ev.planet).system;
            counts = sys == where.system;
        } else if (scope == Scope::Location && ev.planet.valid() && ev.planet.index() < s.galaxy.objects.size()) {
            counts = locationOf(s.galaxy, ev.planet) == where;
        }
        if (!counts) continue;
        total += d;
        addCause(causes, causeOf(ev.trigger), d);
    }

    // 4. Ships present, counted per ship; the sector value replaces the system one.
    const Presence p = presenceAt(r, s, c);
    const int64_t enemies = p.enemySector > 0 ? v("Enemy Ship in Sector") * p.enemySector : v("Enemy Ship in System") * p.enemySystem;
    const int64_t friends = p.ourSector > 0 ? v("Our Ship in Sector") * p.ourSector : v("Our Ship in System") * p.ourSystem;
    total += enemies + friends;
    addCause(causes, Cause::EnemyShips, enemies);
    addCause(causes, Cause::OurShips, friends);

    // 5. Troops (confirmed: binary): troops another empire landed that still
    //    fight for the planet count once as enemies, whatever the treaty; they
    //    are kept apart from the cargo (Colony::landedTroops, spec 04 §13),
    //    and combat::invaders names their empire. Every troop unit in the
    //    colony's cargo counts as ours, whoever owns it.
    int64_t ourTroops = 0;
    for (const UnitStack& u : c.cargo.units)
        if (u.count > 0 && combat::isTroopDesign(r, s, u.design)) ourTroops += u.count;
    const int64_t invaded = combat::invaders(r, s, c).empty() ? 0 : v("Enemy Troops on Planet");
    const int64_t garrison = v("Our Troops on Planet") * ourTroops;
    total += invaded + garrison;
    addCause(causes, Cause::EnemyTroops, invaded);
    addCause(causes, Cause::OurTroops, garrison);

    // 6. Plague.
    if (c.plagueLevel > 0) {
        total += v("Planet Plagued");
        addCause(causes, Cause::Plague, v("Planet Plagued"));
    }

    // Whole percent (truncated), plus the colony's own happiness facilities (positive angers).
    int64_t change = total / 10 + sumValue1(workingAbilities(r, s, c), AbilityKind::PlanetChangePopulationHappiness);
    if (model) {
        // A negative change below the stored negative limit ÷ 10 becomes that
        // limit, with its sign as stored: a positive limit turns every calming
        // change into that many points of anger (spec 02 §4, confirmed: binary).
        const int64_t calmest = model->maxNegativeChange / 10;
        if (change < 0) {
            if (change < calmest) change = calmest;
        } else {
            change = std::min<int64_t>(change, model->maxPositiveChange / 10);
        }
    }
    setAnger(r, s, c, static_cast<int>(std::clamp<int64_t>(c.anger + change, -kMaxAnger, 2 * kMaxAnger)));
    // A rioting colony with people logs it (spec 02 §4, confirmed: binary).
    if (moodFromAnger(c.anger) == Mood::Rioting && c.totalPopulation() > 0)
        ctx.log(c.owner, LogCategory::Misc, std::format("Riots on {}", s.galaxy.object(c.planet).name),
                "The population produces nothing, builds nothing and does not grow until calm returns.", where, "PlanetRiots");
    return c.anger != before;
}

} // namespace

// ---- Cargo over capacity ------------------------------------------------------------------------

void addDestroyedFacilities(Colony& c, uint32_t facility, int count) {
    auto it = std::lower_bound(c.destroyedFacilities.begin(), c.destroyedFacilities.end(), facility,
                               [](const DestroyedFacilities& d, uint32_t f) { return d.facility < f; });
    if (it == c.destroyedFacilities.end() || it->facility != facility) it = c.destroyedFacilities.insert(it, DestroyedFacilities{facility, 0});
    it->count += count;
}

void removeDestroyedFacilities(Colony& c) {
    for (const DestroyedFacilities& d : c.destroyedFacilities)
        for (int k = 0; k < d.count; ++k) {
            const auto it = std::find(c.facilities.begin(), c.facilities.end(), d.facility);
            if (it == c.facilities.end()) break;
            c.facilities.erase(it);
        }
    std::erase_if(c.destroyedFacilities, [&](const DestroyedFacilities& d) {
        return d.count <= 0 || std::find(c.facilities.begin(), c.facilities.end(), d.facility) == c.facilities.end();
    });
}

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
    // `Homeworld Lost` when the colony type is the game's own built-in name
    // "Homeworld", as written, whatever DefaultColonyTypes.txt holds; this is
    // the only place either loss event is logged (spec 02 §2, confirmed: binary).
    const bool home = c->colonyType == kHomeworldColonyType;
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
    CauseTotals wide{};
    for (const MoodEvent& ev : events)
        if (scopeOf(ev.trigger) == Scope::Empire) {
            const int64_t d = triggerValue(model, ev.trigger) * std::max(1, ev.count);
            empireWide += d;
            addCause(wide, causeOf(ev.trigger), d);
        }
    empireWide -= racialEffect(r, race, RacialEffect::Happiness) / 5;
    std::erase_if(events, [](const MoodEvent& ev) { return scopeOf(ev.trigger) == Scope::Empire; });

    // The reports (spec 02 §4, confirmed: binary): the empire-wide events'
    // causes first, then each colony's riot entry as it is updated, then, in
    // system order, each system where one of our colonies changed its anger,
    // by what each cause gave that system's colonies.
    reportMoods(ctx, e, wide, std::nullopt);
    std::map<SystemId, std::pair<CauseTotals, bool>> systems;
    for (ObjectId planet : coloniesOf(s, e)) {
        auto& [causes, changed] = systems[s.galaxy.object(planet).system];
        if (updateColonyAnger(ctx, *s.colony(planet), empireWide, events, causes)) changed = true;
    }
    for (const auto& [sys, report] : systems)
        if (report.second) reportMoods(ctx, e, report.first, sys);
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
