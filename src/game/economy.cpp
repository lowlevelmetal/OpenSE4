#include "game/economy.hpp"

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy_internal.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>
#include <map>

// Planet output, income, trade, maintenance and storage (docs/spec/02 §5,
// §7, §12). Construction queues live in
// economy_queue.cpp, population, happiness and plague in economy_population.cpp.
//
// Percentages that the original applies in floating point go through
// xmath::pctRound / pctTrunc, in the order each spec rule gives.

namespace opense4::game::economy {

using namespace detail;
using xmath::pctRound;
using xmath::pctTrunc;

// ---- Shared helpers ------------------------------------------------------------------------------

namespace detail {

std::vector<ParsedAbility> workingAbilities(const Rules& r, const GameState& s, const Colony& c) {
    std::vector<ParsedAbility> out;
    for (const auto& a : s.galaxy.object(c.planet).abilities) out.push_back(parseAbility(a));
    for (uint32_t f : c.facilities)
        for (const auto& a : r.facilityAbilities(f)) out.push_back(a);
    return out;
}

std::vector<const Colony*> coloniesInSystem(const GameState& s, EmpireId owner, SystemId sys) {
    std::vector<const Colony*> out;
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return out;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->owner == owner) out.push_back(c);
    return out;
}

int64_t bestOf(std::span<const ParsedAbility> list, AbilityKind k, bool* found) {
    int64_t best = 0;
    bool any = false;
    for (const auto& a : list)
        if (a.kind == k) {
            best = std::max(best, a.value1);
            any = true;
        }
    if (found) *found = any;
    return best;
}

namespace {

// Largest Val 1 of `k` on a vehicle's hull and intact components (0 if none).
int64_t vehicleBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    const Design& d = s.design(v.design);
    int64_t best = 0;
    for (const auto& a : r.hullAbilities(d.hull))
        if (a.kind == k) best = std::max(best, a.value1);
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const auto list = r.componentAbilities(d.entries[i].component);
        if (!hasAbility(list, k) || !entryIntact(r, s, v, i)) continue;
        best = std::max(best, bestValue1(list, k));
    }
    return best;
}

} // namespace

int64_t bestInSystem(const Rules& r, const GameState& s, EmpireId owner, SystemId sys, AbilityKind k, bool coloniesOnly) {
    int64_t best = 0;
    for (const Colony* c : coloniesInSystem(s, owner, sys))
        for (uint32_t f : c->facilities)
            for (const auto& a : r.facilityAbilities(f))
                if (a.kind == k) best = std::max(best, a.value1);
    if (!coloniesOnly)
        for (const Vehicle& v : s.vehicles)
            if (v.owner == owner && v.location.system == sys && v.count > 0 && v.status != VehicleStatus::Mothballed)
                best = std::max(best, vehicleBest(r, s, v, k));
    return best;
}

Resources designCost(const Rules& r, const Design& d) {
    Resources cost = Resources::from(r.hull(d.hull).cost);
    for (const DesignEntry& e : d.entries) cost += mounted(r, e).cost;
    return cost;
}

bool isShipOrBase(ruleset::VehicleType t) { return t == ruleset::VehicleType::Ship || t == ruleset::VehicleType::Base; }

SystemId homeSystem(const GameState& s, EmpireId e) {
    // Recorded when the game is created, and for a rebel empire when it is founded (Empire::homeSystem).
    if (e.valid() && e.index() < s.empires.size() && s.empire(e).homeSystem.index() < s.galaxy.systems.size()) return s.empire(e).homeSystem;
    for (const auto& c : s.colonies)  // not recorded (inferred fallback): a capital's system
        if (c && c->owner == e && c->homeworld) return s.galaxy.object(c->planet).system;
    return {};
}

int starCount(const GameState& s, SystemId sys) {
    int n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += isStarKind(s.galaxy.object(o).kind);  // destroyed stars count (spec 01 §5.4)
    return n;
}

int clampedValue(const Rules& r, const GameState& s, int64_t v) {
    const bool finite = s.options.finiteResources;
    const int64_t lo = r.setting(finite ? "Minimum Planet Resource Value" : "Minimum Planet Percent Value", 0);
    const int64_t hi = r.setting(finite ? "Maximum Planet Resource Value" : "Maximum Planet Percent Value", finite ? 999'000'000 : 250);
    return static_cast<int>(std::max<int64_t>(0, std::clamp(v, lo, std::max(lo, hi))));
}

} // namespace detail

// A turn-based game advances the date only after the last player's end-of-turn
// processing (spec 05 §8), so its players' processing sees the unadvanced date.
uint32_t processingTurn(const GameState& s) { return s.options.simultaneous ? s.turn + 1 : s.turn; }
uint32_t processingDate(const GameState& s) { return kStartDate + processingTurn(s); }

// ---- Racial effects --------------------------------------------------------------------------------

int racialEffect(const Rules& r, const Race& race, RacialEffect e) {
    const ruleset::Culture* culture = r.culture(race);
    const auto ch = [&](Characteristic c) { return race.characteristic(c) - 100; };
    const auto trait = [&](std::string_view type) { return static_cast<int>(r.traitValue(race, type)); };
    const auto field = [&](int ruleset::Culture::* f) { return culture ? culture->*f : 0; };
    switch (e) {
        case RacialEffect::Reproduction: return ch(Characteristic::Reproduction) + trait("Reproduction");
        case RacialEffect::MineralOutput:
            return ch(Characteristic::MiningAptitude) + field(&ruleset::Culture::production) + trait("Mineral Production") +
                   trait("Production");
        case RacialEffect::OrganicsOutput:
            return ch(Characteristic::FarmingAptitude) + field(&ruleset::Culture::production) + trait("Organics Production") +
                   trait("Production");
        case RacialEffect::RadioactivesOutput:
            return ch(Characteristic::RefiningAptitude) + field(&ruleset::Culture::production) + trait("Radioactives Production") +
                   trait("Production");
        case RacialEffect::Research:
            return ch(Characteristic::Intelligence) + field(&ruleset::Culture::research) + trait("Research Production");
        case RacialEffect::Intelligence:
            return ch(Characteristic::Cunning) + field(&ruleset::Culture::intelligence) + trait("Intelligence Production");
        case RacialEffect::ShipyardRate:
            return ch(Characteristic::ConstructionAptitude) + field(&ruleset::Culture::shipyardRate) + trait("SY Rate");
        case RacialEffect::Maintenance:
            return ch(Characteristic::MaintenanceAptitude) + field(&ruleset::Culture::maintenance) + trait("Maintenance Cost");
        case RacialEffect::Happiness:
            return ch(Characteristic::Happiness) + field(&ruleset::Culture::happiness) + trait("Population Happiness");
        case RacialEffect::EnvironmentalResistance: return ch(Characteristic::EnvironmentalResistance) + trait("Tollerance");
        case RacialEffect::Trade: return ch(Characteristic::PoliticalSavvy) + field(&ruleset::Culture::trade) + trait("Trade");
    }
    return 0;
}

int characteristicPointCost(const Rules& r, Characteristic c, int value) {
    const std::string name{displayName(c)};
    const int64_t cost = r.setting(std::format("Characteristic {} Pct Cost", name), 0);
    const int64_t threshold = r.setting(std::format("Characteristic {} Threshold", name), 0);
    const int64_t pos = r.setting(std::format("Characteristic {} Threshhold Pct Cost Pos", name), 0);
    const int64_t neg = r.setting(std::format("Characteristic {} Threshhold Pct Cost Neg", name), 0);
    // Costed as stored: Min/Max Pct limit only the race window's buttons, so a
    // value from a preset or file outside them costs what it says (spec 02 §8.1,
    // spec 01 §14 Q35, confirmed: binary).
    const int64_t d = int64_t{value} - 100;
    int64_t points = 0;
    if (threshold < 1 || (d <= threshold && -d <= threshold)) points = cost * d;
    else if (d > threshold) points = cost * threshold + pos * (d - threshold);
    else points = -cost * threshold - neg * (-d - threshold);
    return static_cast<int>(points);
}

// ---- Empire experience ----------------------------------------------------------------------------

void gainExperience(Empire& e, int64_t amount) {
    if (amount <= 0) return;
    e.experience = static_cast<int>(std::min<int64_t>(int64_t{e.experience} + amount, kMaxEmpireExperience));
}

std::string_view raceAge(int64_t experience) {
    // Each label covers experience up to its limit (confirmed: binary).
    static constexpr std::array<std::pair<int64_t, std::string_view>, 8> kAges{{{5'000, "Newborn"},
                                                                                {10'000, "Infantile"},
                                                                                {50'000, "Young"},
                                                                                {200'000, "Moderate"},
                                                                                {1'000'000, "Old"},
                                                                                {10'000'000, "Ancient"},
                                                                                {100'000'000, "God-like"},
                                                                                {400'000'000, "Stellar Ancients"}}};
    for (const auto& [limit, name] : kAges)
        if (experience <= limit) return name;
    return "First Ones";
}

// ---- Modifier tables -------------------------------------------------------------------------------

PopulationModifier populationModifier(const Rules& r, int64_t population) {
    for (const Rules::PopulationRow& row : r.populationRows()) {
        if (row.amount < population) continue;
        PopulationModifier m;
        m.production = row.production;
        m.shipyard = row.shipyard;
        return m;
    }
    return {};  // no row is large enough: 100 % (confirmed: binary)
}

int moodOutputPercent(const Rules& r, Mood m) {
    switch (m) {
        case Mood::Rioting: return static_cast<int>(r.setting("Mood Riot Modifier", 0));
        case Mood::Angry: return static_cast<int>(r.setting("Mood Angry Modifier", 80));
        case Mood::Unhappy: return static_cast<int>(r.setting("Mood Unhappy Modifier", 90));
        case Mood::Indifferent: return static_cast<int>(r.setting("Mood Indifferent Modifier", 100));
        case Mood::Happy: return static_cast<int>(r.setting("Mood Happy Modifier", 110));
        case Mood::Jubilant: return static_cast<int>(r.setting("Mood Jubilant Modifier", 120));
    }
    return 100;
}

int moodReproduction(Mood m) {
    switch (m) {  // (confirmed: binary)
        case Mood::Rioting: return 0;
        case Mood::Angry: return -5;
        case Mood::Unhappy: return -2;
        case Mood::Indifferent: return 0;
        case Mood::Happy: return 2;
        case Mood::Jubilant: return 5;
    }
    return 0;
}

// ---- Conditions -------------------------------------------------------------------------------------

ConditionsBand conditionsBand(Conditions conditions) {
    // Edges 0.3, 0.5, 1.0, 1.3 and 1.5 (confirmed: binary). The stored double is
    // compared with each edge as an x87 constant (inferred, spec 02 §13 Q51), so
    // a double just below an edge, such as the double nearest 0.3, is in the
    // band below it.
    const xmath::Ext c = conditions.value();
    const auto atLeast = [&](int tenths) { return c >= xmath::Ext(tenths) / xmath::Ext(10); };
    if (atLeast(15)) return ConditionsBand::Optimal;
    if (atLeast(13)) return ConditionsBand::Good;
    if (atLeast(10)) return ConditionsBand::Mild;
    if (atLeast(5)) return ConditionsBand::Unpleasant;
    if (atLeast(3)) return ConditionsBand::Harsh;
    return ConditionsBand::Deadly;
}

std::string_view conditionsName(ConditionsBand b) {
    switch (b) {
        case ConditionsBand::Optimal: return "Optimal";
        case ConditionsBand::Good: return "Good";
        case ConditionsBand::Mild: return "Mild";
        case ConditionsBand::Unpleasant: return "Unpleasant";
        case ConditionsBand::Harsh: return "Harsh";
        case ConditionsBand::Deadly: return "Deadly";
    }
    return "?";
}

int conditionsReproduction(ConditionsBand b) {
    switch (b) {  // (confirmed: binary)
        case ConditionsBand::Optimal: return 5;
        case ConditionsBand::Good: return 2;
        case ConditionsBand::Mild: return 0;
        case ConditionsBand::Unpleasant: return -2;
        case ConditionsBand::Harsh: return -5;
        case ConditionsBand::Deadly: return -20;
    }
    return 0;
}

// ---- Mood ---------------------------------------------------------------------------------------------

bool emotionless(const Rules& r, const GameState& s, EmpireId e) {
    return e.valid() && e.index() < s.empires.size() && r.hasTrait(s.empire(e).race, "Population Emotionless");
}

std::string_view moodName(const Rules& r, const GameState& s, const Colony& c) {
    return emotionless(r, s, c.owner) ? std::string_view("Emotionless") : displayName(moodFromAnger(c.anger));
}

void setAnger(const Rules& r, const GameState& s, Colony& c, int anger) {
    if (emotionless(r, s, c.owner)) c.anger = std::min(kEmotionlessAnger, c.maxAnger());
    else c.anger = std::clamp(anger, 0, c.maxAnger());
}

// ---- Spaceports, blockades, reproduction ------------------------------------------------------------

bool spaceportInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    if (r.hasTrait(s.empire(e).race, "No Spaceports")) return true;
    // Any Spaceport facility on any of the empire's colonies there; population does not matter (confirmed: binary).
    for (const Colony* c : coloniesInSystem(s, e, sys))
        for (uint32_t f : c->facilities)
            if (hasAbility(r.facilityAbilities(f), AbilityKind::Spaceport)) return true;
    return false;
}

bool colonyBlockaded(const Rules& r, const GameState& s, const Colony& c) {
    const Location where = locationOf(s.galaxy, c.planet);
    const std::vector<VehicleId>& seen = s.empire(c.owner).knowledge.visibleVehicles;
    for (const Vehicle& v : s.vehicles) {
        if (v.location != where || v.count <= 0 || !v.owner.valid() || !hostile(s, c.owner, v.owner)) continue;
        if (v.status == VehicleStatus::Mothballed) continue;
        // A cloaked ship blockades only while the owner can see it (confirmed: binary).
        if (v.status == VehicleStatus::Cloaked && !std::binary_search(seen.begin(), seen.end(), v.id)) continue;
        // Ships and bases only: fighters, satellites, mines, drones and troops never blockade.
        if (isShipOrBase(vehicleType(r, s, v))) return true;
    }
    // A hostile empire's planet in the same sector blockades too.
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* other = s.colony(o); other && other->owner != c.owner && hostile(s, c.owner, other->owner)) return true;
    return false;
}

int reproductionPercent(const Rules& r, const GameState& s, const Colony& c) {
    // One rate for the whole colony, from the owner's race (confirmed: binary, spec 02 §3).
    const bool calm = emotionless(r, s, c.owner);
    const Mood mood = moodFromAnger(c.anger);
    if (c.totalPopulation() <= 0 || c.plagueLevel > 0 || (mood == Mood::Rioting && !calm)) return 0;
    const Race& race = s.empire(c.owner).race;
    const SpaceObject& planet = s.galaxy.object(c.planet);
    int64_t rate = std::max<int64_t>(0, r.setting("Empire Starting Percent Reproduction", 10) + racialEffect(r, race, RacialEffect::Reproduction));
    if (!calm) rate += moodReproduction(mood);
    rate += conditionsReproduction(conditionsBand(planet.conditions));
    rate += racialEffect(r, race, RacialEffect::EnvironmentalResistance) / 5;  // truncated, on every planet
    rate += bestInSystem(r, s, c.owner, planet.system, AbilityKind::ModifyReproductionSystem);
    return static_cast<int>(std::clamp<int64_t>(rate, 0, 100));
}

// ---- Colony output -------------------------------------------------------------------------------------

namespace {

constexpr std::array<AbilityKind, 3> kResourceGen{AbilityKind::ResourceGenMinerals, AbilityKind::ResourceGenOrganics,
                                                  AbilityKind::ResourceGenRadioactives};
constexpr std::array<AbilityKind, 3> kSolarGen{AbilityKind::SolarResourceGenMinerals, AbilityKind::SolarResourceGenOrganics,
                                               AbilityKind::SolarResourceGenRadioactives};
constexpr std::array<AbilityKind, 3> kPlanetMod{AbilityKind::ResourceGenModPlanetMinerals, AbilityKind::ResourceGenModPlanetOrganics,
                                                AbilityKind::ResourceGenModPlanetRadioactives};
constexpr std::array<AbilityKind, 3> kSystemMod{AbilityKind::ResourceGenModSystemMinerals, AbilityKind::ResourceGenModSystemOrganics,
                                                AbilityKind::ResourceGenModSystemRadioactives};
constexpr std::array<RacialEffect, 3> kOutputEffect{RacialEffect::MineralOutput, RacialEffect::OrganicsOutput,
                                                    RacialEffect::RadioactivesOutput};

bool generates(std::span<const ParsedAbility> list) {
    for (const auto& a : list)
        switch (a.kind) {
            case AbilityKind::ResourceGenMinerals:
            case AbilityKind::ResourceGenOrganics:
            case AbilityKind::ResourceGenRadioactives:
            case AbilityKind::PointGenResearch:
            case AbilityKind::PointGenIntelligence:
            case AbilityKind::SolarResourceGenMinerals:
            case AbilityKind::SolarResourceGenOrganics:
            case AbilityKind::SolarResourceGenRadioactives: return true;
            default: break;
        }
    return false;
}

// Steps 2-4 of spec 02 §5.1 for one kind: planet value (resources in a normal
// game), the planet modifier as its own multiplication, then race, mood and
// population added into one percentage. Each step rounds or truncates on its own.
int64_t modifiedOutput(int64_t base, std::optional<int> value, int64_t planetModifier, int pct) {
    if (value) base = pctRound(base, std::max(0, *value));
    if (planetModifier > 0) base = pctRound(base, 100 + planetModifier);
    return pctTrunc(base, std::max(0, pct));
}

} // namespace

ColonyOutput colonyOutput(const Rules& r, const GameState& s, const Colony& c) {
    ColonyOutput out;
    const Race& race = s.empire(c.owner).race;
    const SpaceObject& planet = s.galaxy.object(c.planet);
    const int64_t population = c.totalPopulation();

    out.mood = moodFromAnger(c.anger);
    out.connected = spaceportInSystem(r, s, c.owner, planet.system);
    out.blockaded = colonyBlockaded(r, s, c);
    out.reproductionPercent = reproductionPercent(r, s, c);
    if (out.connected) out.deliveryPercent = 100;
    else if (planet.system == homeSystem(s, c.owner))
        out.deliveryPercent = static_cast<int>(std::clamp<int64_t>(r.setting("Home System Percentage Value With No Spaceport", 25), 0, 100));
    else out.deliveryPercent = 0;
    if (population <= 0) return out;

    bool depot = false;
    for (uint32_t f : c.facilities)
        for (const auto& a : r.facilityAbilities(f))
            if (a.kind == AbilityKind::SupplyGeneration) {
                depot = true;
                out.supply += a.value1;
            }
    if (depot && out.supply <= 0) out.supply = kUnlimitedSupply;

    // Step 1: every facility counts, however small the population (`Population
    // Required to Operate One Facility` is never used, confirmed: binary).
    std::array<int64_t, 3> base{}, solar{};
    int64_t baseResearch = 0, baseIntel = 0;
    for (uint32_t f : c.facilities) {
        const auto ab = r.facilityAbilities(f);
        if (!generates(ab)) continue;
        ++out.facilitiesOperating;
        for (size_t i = 0; i < 3; ++i) {
            base[i] += sumValue1(ab, kResourceGen[i]);
            solar[i] += sumValue1(ab, kSolarGen[i]);
        }
        baseResearch += sumValue1(ab, AbilityKind::PointGenResearch);
        baseIntel += sumValue1(ab, AbilityKind::PointGenIntelligence);
    }
    // Step 6: a rioting or blockaded colony produces nothing at all; the output is lost.
    if ((out.mood == Mood::Rioting && !emotionless(r, s, c.owner)) || out.blockaded) return out;

    const std::vector<ParsedAbility> own = workingAbilities(r, s, c);
    const int common = (moodOutputPercent(r, out.mood) - 100) + (populationModifier(r, population).production - 100);
    const bool finite = s.options.finiteResources;
    const int stars = starCount(s, planet.system);
    for (size_t i = 0; i < 3; ++i) {
        out.productionPercent[i] = std::max(0, 100 + racialEffect(r, race, kOutputEffect[i]) + common);
        int64_t made = modifiedOutput(base[i], finite ? std::nullopt : std::optional<int>{planet.value[i]}, bestOf(own, kPlanetMod[i]),
                                      out.productionPercent[i]);
        if (finite) {
            // Step 7: the value is the stock left; it caps the output and is drawn down by it.
            made = std::min<int64_t>(made, std::max(0, planet.value[i]));
            out.depletion.v[i] = made;
        }
        // Step 5: solar output, unmodified and not limited by the stock.
        out.solar.v[i] = solar[i] * stars;
        out.production.v[i] = made + out.solar.v[i];
    }
    out.researchPercent = std::max(0, 100 + racialEffect(r, race, RacialEffect::Research) + common);
    out.intelligencePercent = std::max(0, 100 + racialEffect(r, race, RacialEffect::Intelligence) + common);
    out.research = modifiedOutput(baseResearch, std::nullopt, bestOf(own, AbilityKind::PlanetPointGenModResearch), out.researchPercent);
    out.intelligence =
        modifiedOutput(baseIntel, std::nullopt, bestOf(own, AbilityKind::PlanetPointGenModIntelligence), out.intelligencePercent);
    return out;
}

namespace {

// Spec 02 §5.5: output is gathered per system; the system modifier and the
// spaceport rule work on each system's total. `drawn` collects the finite
// stock each colony uses (optional).
Production produce(const Rules& r, const GameState& s, EmpireId e, std::vector<std::pair<ObjectId, Resources>>* drawn) {
    Production p;
    struct Total {
        Resources resources;
        int64_t research = 0;
        int64_t intelligence = 0;
    };
    std::map<SystemId, Total> systems;  // in system id order
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        const ColonyOutput out = colonyOutput(r, s, *c);
        Total& t = systems[s.galaxy.object(c->planet).system];
        t.resources += out.production;
        t.research += out.research;
        t.intelligence += out.intelligence;
        p.depletion += out.depletion;
        if (drawn && !out.depletion.isZero()) drawn->emplace_back(c->planet, out.depletion);
    }
    const SystemId home = homeSystem(s, e);
    const int64_t homePct = std::clamp<int64_t>(r.setting("Home System Percentage Value With No Spaceport", 25), 0, 100);
    for (auto& [sys, t] : systems) {
        const auto boost = [&](int64_t amount, AbilityKind k) {
            const int64_t m = bestInSystem(r, s, e, sys, k, true);
            return m > 0 ? pctRound(amount, 100 + m) : amount;
        };
        for (size_t i = 0; i < 3; ++i) t.resources.v[i] = boost(t.resources.v[i], kSystemMod[i]);
        t.research = boost(t.research, AbilityKind::SystemPointGenModResearch);
        t.intelligence = boost(t.intelligence, AbilityKind::SystemPointGenModIntelligence);
        Total delivered = t;
        if (!spaceportInSystem(r, s, e, sys)) {
            // Only the home system still delivers a share without a spaceport (confirmed: binary).
            const int64_t pct = sys == home ? homePct : 0;
            for (size_t i = 0; i < 3; ++i) delivered.resources.v[i] = pctTrunc(t.resources.v[i], pct);
            delivered.research = pctTrunc(t.research, pct);
            delivered.intelligence = pctTrunc(t.intelligence, pct);
        }
        p.resources += delivered.resources;
        p.research += delivered.research;
        p.intelligence += delivered.intelligence;
        p.undelivered += t.resources - delivered.resources;
    }
    return p;
}

} // namespace

Production empireProduction(const Rules& r, const GameState& s, EmpireId e) { return produce(r, s, e, nullptr); }

// ---- Treasury --------------------------------------------------------------------------------------------

Resources storageCapacity(const Rules& r, const GameState& s, EmpireId e) {
    constexpr std::array<AbilityKind, 3> kStorage{AbilityKind::ResourceStorageMinerals, AbilityKind::ResourceStorageOrganics,
                                                  AbilityKind::ResourceStorageRadioactives};
    const Race& race = s.empire(e).race;
    const std::array<int64_t, 3> trait{r.traitValue(race, "Mineral Storage"), r.traitValue(race, "Organics Storage"),
                                       r.traitValue(race, "Radioactives Storage")};
    const int64_t floor = r.setting("Minimum Empire Point Storage", 50000);
    Resources cap{floor, floor, floor};
    // Colonies need no population for their storage (confirmed: binary).
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        for (size_t k = 0; k < 3; ++k) {
            int64_t sum = 0;
            for (uint32_t f : c->facilities) sum += sumValue1(r.facilityAbilities(f), kStorage[k]);
            if (sum != 0) cap.v[k] += pctRound(sum, 100 + trait[k]);
        }
    }
    for (int64_t& v : cap.v) v = std::clamp<int64_t>(v, 0, kTreasuryLimit);
    return cap;
}

int maintenancePercent(const Rules& r, const Empire& e) {
    const int64_t pct = r.setting("Empire Starting Percent Maint Cost", 25) - racialEffect(r, e.race, RacialEffect::Maintenance);
    return static_cast<int>(std::max<int64_t>(pct, 5));  // at least 5 % (confirmed: binary)
}

namespace {

// A design's own maintenance modifier: hull and component `Modified Maintenance Cost`, summed.
int64_t designMaintenanceModifier(const Rules& r, const Design& d) {
    int64_t mod = sumValue1(r.hullAbilities(d.hull), AbilityKind::ModifiedMaintenanceCost);
    for (const DesignEntry& e : d.entries) mod += sumValue1(r.componentAbilities(e.component), AbilityKind::ModifiedMaintenanceCost);
    return mod;
}

// Spec 02 §7: three truncating steps per resource. Units and mothballed ships pay nothing.
Resources maintenanceOf(const Rules& r, const GameState& s, const Vehicle& v, int pct, const Resources& cost) {
    if (v.status == VehicleStatus::Mothballed || v.count <= 0 || !isShipOrBase(vehicleType(r, s, v))) return {};
    const int64_t hull = 100 + designMaintenanceModifier(r, s.design(v.design));
    const int64_t reduced = 100 - bestInSystem(r, s, v.owner, v.location.system, AbilityKind::ReducedMaintenanceSystem);
    Resources out;
    for (size_t i = 0; i < 3; ++i) {
        const int64_t a = pctTrunc(cost.v[i], pct);
        const int64_t b = pctTrunc(a, hull);
        out.v[i] = std::max<int64_t>(0, pctTrunc(b, reduced) * v.count);
    }
    return out;
}

} // namespace

Resources vehicleMaintenance(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!v.owner.valid()) return {};
    return maintenanceOf(r, s, v, maintenancePercent(r, s.empire(v.owner)), designCost(r, s.design(v.design)));
}

Resources maintenanceCost(const Rules& r, const GameState& s, EmpireId e) {
    const int pct = maintenancePercent(r, s.empire(e));
    std::map<DesignId, Resources> costs;
    Resources total;
    for (const Vehicle& v : s.vehicles) {
        if (v.owner != e) continue;
        auto it = costs.find(v.design);
        if (it == costs.end()) it = costs.emplace(v.design, designCost(r, s.design(v.design))).first;
        total += maintenanceOf(r, s, v, pct, it->second);
    }
    return total;
}

// ---- Income ----------------------------------------------------------------------------------------------

namespace {

constexpr std::array<AbilityKind, 3> kRemoteGen{AbilityKind::RemoteResourceGenMinerals, AbilityKind::RemoteResourceGenOrganics,
                                                AbilityKind::RemoteResourceGenRadioactives};
constexpr std::array<AbilityKind, 3> kGeneratePoints{AbilityKind::GeneratePointsMinerals, AbilityKind::GeneratePointsOrganics,
                                                     AbilityKind::GeneratePointsRadioactives};

// One remote-mining extraction this turn (spec 02 §5.3).
struct Extraction {
    ObjectId source;
    size_t resource = 0;
    int64_t amount = 0;
};

// In each sector only the empire's first ship (object order) with a remote-mining
// component works; it mines every uncolonized planet and asteroid field there
// (confirmed: binary).
std::vector<Extraction> remoteMining(const Rules& r, const GameState& s, EmpireId e) {
    std::vector<Extraction> out;
    std::vector<Location> worked;
    for (const Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0 || v.status == VehicleStatus::Mothballed) continue;
        std::array<int64_t, 3> rate{};
        bool miner = false;
        const Design& d = s.design(v.design);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            const auto list = r.componentAbilities(d.entries[i].component);
            bool has = false;
            for (size_t k = 0; k < 3; ++k) has = has || hasAbility(list, kRemoteGen[k]);
            if (!has || !entryIntact(r, s, v, i)) continue;
            miner = true;
            for (size_t k = 0; k < 3; ++k) rate[k] += sumValue1(list, kRemoteGen[k]);
        }
        if (!miner || std::find(worked.begin(), worked.end(), v.location) != worked.end()) continue;
        worked.push_back(v.location);
        for (ObjectId o : planetsAt(s, v.location)) {
            if (s.colony(o)) continue;
            const SpaceObject& obj = s.galaxy.object(o);
            for (size_t k = 0; k < 3; ++k) {
                if (rate[k] <= 0) continue;
                const int64_t value = std::max(0, obj.value[k]);
                const int64_t amount = s.options.finiteResources ? std::min(rate[k], value) : pctRound(rate[k], value);
                if (amount > 0) out.push_back({o, k, amount});
            }
        }
    }
    return out;
}

void applyRemoteDepletion(const Rules& r, GameState& s, const std::vector<Extraction>& mined) {
    const bool decreases = r.settingFlag("Remote Mining Decreases Asteroid Value", true);
    for (const Extraction& x : mined) {
        int& value = s.galaxy.object(x.source).value[x.resource];
        if (s.options.finiteResources) value = clampedValue(r, s, int64_t{value} - x.amount);
        else if (decreases) value = clampedValue(r, s, int64_t{value} - 1);  // one point per turn mined
    }
}

// Everything an empire takes in at its income step, computed without changing the state.
struct Income {
    Production produced;
    std::vector<std::pair<ObjectId, Resources>> drawn;  // finite stock per colony
    std::vector<Extraction> mined;
    Resources remote, generated, floor, tariffsOut, bonus;
    int64_t research = 0;
    int64_t intelligence = 0;

    // What reaches the treasury.
    Resources net() const { return produced.resources + floor + remote + generated - tariffsOut + bonus; }
};

Income computeIncome(const Rules& r, const GameState& s, EmpireId e) {
    Income inc;
    inc.produced = produce(r, s, e, &inc.drawn);
    inc.mined = remoteMining(r, s, e);
    for (const Extraction& x : inc.mined) inc.remote.v[x.resource] += x.amount;

    // Generate Points: flat points from every object the empire owns, no modifiers (spec 02 §5.4).
    int64_t genResearch = 0, genIntel = 0;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        for (const ParsedAbility& a : workingAbilities(r, s, *c)) {
            for (size_t k = 0; k < 3; ++k)
                if (a.kind == kGeneratePoints[k]) inc.generated.v[k] += a.value1;
            if (a.kind == AbilityKind::GeneratePointsResearch) genResearch += a.value1;
            if (a.kind == AbilityKind::GeneratePointsIntelligence) genIntel += a.value1;
        }
    }
    for (const Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0 || v.status == VehicleStatus::Mothballed) continue;
        // A unit group adds every unit's (spec 03 §12).
        for (size_t k = 0; k < 3; ++k) inc.generated.v[k] += vehicleAbilityTotal(r, s, v, kGeneratePoints[k]);
        genResearch += vehicleAbilityTotal(r, s, v, AbilityKind::GeneratePointsResearch);
        genIntel += vehicleAbilityTotal(r, s, v, AbilityKind::GeneratePointsIntelligence);
    }

    // Not a floor: a resource the colonies deliver exactly 0 of is replaced by the
    // Settings amount, for every living empire, also one left with ships only
    // (confirmed: binary).
    if (s.empire(e).alive) {
        static constexpr std::array<std::string_view, 3> kMinimum{"Minimum Empire Minerals Generation", "Minimum Empire Organics Generation",
                                                                  "Minimum Empire Radioactives Generation"};
        for (size_t k = 0; k < 3; ++k)
            if (inc.produced.resources.v[k] == 0) inc.floor.v[k] = r.setting(kMinimum[k], 200);
    }

    // Tariffs paid to a master come off each of the five incomes, never more
    // than it (spec 02 §5.4, spec 05 §3.3): the master gets the resources, the
    // research and intelligence parts are lost. This is the only place a
    // tariff is taken. Then the computer player's bonus multiplies what is
    // left (spec 05 §8).
    const Resources gross = inc.produced.resources + inc.floor + inc.remote + inc.generated;
    const int64_t grossResearch = inc.produced.research + genResearch;
    const int64_t grossIntel = inc.produced.intelligence + genIntel;
    const diplomacy::Generated due = diplomacy::tariffDue(r, s, e);
    inc.tariffsOut = min(max(due.resources, Resources{}), max(gross, Resources{}));
    const int64_t researchTariff = std::clamp<int64_t>(due.research, 0, std::max<int64_t>(0, grossResearch));
    const int64_t intelTariff = std::clamp<int64_t>(due.intelligence, 0, std::max<int64_t>(0, grossIntel));
    const int64_t factor = ai::incomeBonusFactor(s, e);
    const Resources kept = gross - inc.tariffsOut;
    for (size_t k = 0; k < 3; ++k) inc.bonus.v[k] = kept.v[k] * (factor - 1);
    inc.research = (grossResearch - researchTariff) * factor;
    inc.intelligence = (grossIntel - intelTariff) * factor;
    return inc;
}

EconomyReport reportFrom(const Income& inc) {
    EconomyReport rep;
    rep.colonies = inc.produced.resources;
    rep.undelivered = inc.produced.undelivered;
    rep.remoteMining = inc.remote;
    rep.otherIncome = inc.generated + inc.floor + inc.bonus;
    rep.tariffsOut = inc.tariffsOut;
    rep.research = inc.research;
    rep.intelligence = inc.intelligence;
    return rep;
}

// Trade and tariffs received (spec 05 §3.3): diplomacy computes them.
struct TradeIncome {
    Resources trade, tariffsIn;
    int64_t research = 0;
    int64_t intelligence = 0;
};

TradeIncome computeTrade(const Rules& r, const GameState& s, EmpireId e) {
    TradeIncome t;
    const Resources fromDiplomacy = max(diplomacy::tradeIncome(r, s, e), Resources{});
    t.tariffsIn = min(max(diplomacy::tariffsReceived(r, s, e), Resources{}), fromDiplomacy);
    t.trade = fromDiplomacy - t.tariffsIn;
    t.research = std::max<int64_t>(0, diplomacy::researchTradeIncome(r, s, e));
    t.intelligence = std::max<int64_t>(0, diplomacy::intelTradeIncome(r, s, e));
    return t;
}

void deposit(Resources& bank, const Resources& amount) {
    for (size_t k = 0; k < 3; ++k) bank.v[k] = addCapped(bank.v[k], amount.v[k]);
}

// Unpaid maintenance: `unpaid div Maintenance Cost Amt Per Dead + 1` vehicles
// are abandoned, picked at random, ships out of supply first (spec 02 §7).
void abandonVehicles(TurnContext& ctx, EmpireId e, int64_t unpaid) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t perDead = std::max<int64_t>(1, r.setting("Maintenance Cost Amt Per Dead", 20000));
    int64_t victims = unpaid / perDead + 1;
    std::vector<VehicleId> candidates;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0 && v.supply <= 0 && v.status != VehicleStatus::Mothballed && isShipOrBase(vehicleType(r, s, v)))
            candidates.push_back(v.id);
    if (candidates.empty())
        for (const Vehicle& v : s.vehicles)
            if (v.owner == e && v.count > 0) candidates.push_back(v.id);
    while (victims-- > 0 && !candidates.empty()) {
        const size_t pick = static_cast<size_t>(s.rng.below(candidates.size()));
        Vehicle& v = *s.vehicle(candidates[pick]);
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
        const bool ship = isShipOrBase(vehicleType(r, s, v));
        ctx.log(e, LogCategory::Construction, std::format("{} {} abandoned", ship ? "Ship" : "Unit group", v.name),
                "The empire could not pay its maintenance.", v.location);
        for (const UnitStack& st : groupStacks(v)) s.design(st.design).lost += st.count;
        // No happiness event: only a ship destroyed by damage logs `Ship Lost in
        // System` and `Any Ship Lost` (confirmed: binary).
        v.count = 0;  // destroyed whole
    }
    s.removeDeadVehicles();
}

} // namespace

// ---- Per-empire steps ------------------------------------------------------------------------------------

void collectIncome(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    const Income inc = computeIncome(r, s, e);
    for (const auto& [planet, used] : inc.drawn) {
        SpaceObject& p = s.galaxy.object(planet);
        for (size_t k = 0; k < 3; ++k) p.value[k] = std::max(0, p.value[k] - static_cast<int>(used.v[k]));
    }
    applyRemoteDepletion(r, s, inc.mined);
    // A master receives the minerals, organics and radioactives of the tariff
    // at once, at its subject's income step (spec 05 §3.3, §8).
    if (const EmpireId master = diplomacy::masterOf(s, e); livingEmpire(s, master) && master != e)
        deposit(s.empire(master).stockpile, inc.tariffsOut);
    Empire& emp = s.empire(e);
    deposit(emp.stockpile, inc.net());
    // Research and intelligence income fill the pools the next turn's
    // research and intelligence steps spend (spec 05 §1.1, §8).
    research::addToPools(emp, std::max<int64_t>(0, inc.research), std::max<int64_t>(0, inc.intelligence));
    emp.economy = reportFrom(inc);
    emp.economy.research = std::min(emp.economy.research, kTreasuryLimit);
    emp.economy.intelligence = std::min(emp.economy.intelligence, kTreasuryLimit);
}

void collectTrade(TurnContext& ctx, EmpireId e) {
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    // Trade only: the tariffs of our subjects arrived at their own income steps.
    const TradeIncome t = computeTrade(ctx.rules, s, e);
    Empire& emp = s.empire(e);
    deposit(emp.stockpile, t.trade);
    research::addToPools(emp, t.research, t.intelligence);  // spent next turn (spec 05 §3.3)
    emp.economy.trade = t.trade;
    emp.economy.research = addCapped(emp.economy.research, t.research);
    emp.economy.intelligence = addCapped(emp.economy.intelligence, t.intelligence);
}

void payMaintenance(TurnContext& ctx, EmpireId e) {
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    const Resources charge = maintenanceCost(ctx.rules, s, e);
    Resources& bank = s.empire(e).stockpile;
    int64_t unpaid = 0;  // summed over the three resources (confirmed: binary)
    for (size_t k = 0; k < 3; ++k) {
        if (bank.v[k] >= charge.v[k]) {
            bank.v[k] -= charge.v[k];
        } else {
            unpaid += charge.v[k] - std::max<int64_t>(0, bank.v[k]);
            bank.v[k] = 0;
        }
    }
    s.empire(e).economy.maintenance = charge;
    if (unpaid > 0) abandonVehicles(ctx, e, unpaid);
}

void applyStorageCap(TurnContext& ctx, EmpireId e) {
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    Empire& emp = s.empire(e);
    emp.economy.storageCap = storageCapacity(ctx.rules, s, e);
    emp.economy.lostToStorage = max(emp.stockpile - emp.economy.storageCap, Resources{});
    emp.stockpile -= emp.economy.lostToStorage;
}

// ---- Reports -------------------------------------------------------------------------------------------

void updateReports(const Rules& r, GameState& s) {
    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        const Income inc = computeIncome(r, s, id);
        EconomyReport rep = reportFrom(inc);
        rep.storageCap = storageCapacity(r, s, id);
        if (s.empire(id).alive) {
            const TradeIncome t = computeTrade(r, s, id);
            rep.trade = t.trade;
            rep.tariffsIn = t.tariffsIn;
            rep.research += t.research;
            rep.intelligence += t.intelligence;
            Resources bank = s.empire(id).stockpile + inc.net() + t.trade + t.tariffsIn;
            rep.maintenance = maintenanceCost(r, s, id);
            bank = max(bank - rep.maintenance, Resources{});
            for (const QueueRef& q : empireQueues(r, s, id)) {
                const ConstructionQueue* queue = nullptr;
                if (q.target.vehicle.valid()) {
                    if (const Vehicle* v = s.vehicle(q.target.vehicle)) queue = &v->queue;
                } else if (const Colony* c = s.colony(q.target.planet)) {
                    queue = &c->queue;
                }
                if (queue) rep.construction += projectQueueUsage(r, s, id, q, *queue, bank);
            }
            rep.lostToStorage = max(bank - rep.storageCap, Resources{});
        }
        s.empire(id).economy = rep;
    }
}

} // namespace opense4::game::economy
