#include "game/economy.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy_internal.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <tuple>

// Planet output, income, maintenance, storage and the phase-5 driver
// (docs/spec/02 §5, §7, §12). Construction queues live in economy_queue.cpp,
// population and mood in economy_population.cpp.

namespace opense4::game::economy {

using namespace detail;

// ---- Shared helpers ------------------------------------------------------------------------------

namespace detail {

std::vector<ParsedAbility> workingAbilities(const Rules& r, const GameState& s, const Colony& c) {
    std::vector<ParsedAbility> out;
    for (const auto& a : s.galaxy.object(c.planet).abilities) out.push_back(parseAbility(a));
    if (facilitiesWork(c))
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
        if (a.kind == k && (!any || a.value1 > best)) {
            best = a.value1;
            any = true;
        }
    if (found) *found = any;
    return best;
}

int64_t bestInSystem(const Rules& r, const GameState& s, EmpireId owner, SystemId sys, AbilityKind k, bool* found) {
    int64_t best = 0;
    bool any = false;
    for (const Colony* c : coloniesInSystem(s, owner, sys)) {
        if (!facilitiesWork(*c)) continue;
        for (uint32_t f : c->facilities)
            for (const auto& a : r.facilityAbilities(f))
                if (a.kind == k && (!any || a.value1 > best)) {
                    best = a.value1;
                    any = true;
                }
    }
    if (found) *found = any;
    return best;
}

Resources designCost(const Rules& r, const Design& d) {
    Resources cost = Resources::from(r.hull(d.hull).cost);
    for (const DesignEntry& e : d.entries) cost += mounted(r, e).cost;
    return cost;
}

bool isShipOrBase(ruleset::VehicleType t) { return t == ruleset::VehicleType::Ship || t == ruleset::VehicleType::Base; }

SystemId homeSystem(const GameState& s, EmpireId e) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e && c->homeworld) return s.galaxy.object(c->planet).system;
    return {};
}

int starCount(const GameState& s, SystemId sys) {
    int n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == ObjectKind::Star;
    return n;
}

} // namespace detail

// ---- Modifier tables -------------------------------------------------------------------------------

PopulationModifier populationModifier(const Rules& r, int64_t population) {
    const int64_t rows = r.setting("Number Of Population Modifiers", 0);
    if (rows <= 0) return {};
    auto amount = [&](int64_t row) { return r.setting(std::format("Pop Modifier {} Population Amount", row), 0); };
    // Rows ascend by population: find the first one that covers the colony (the last row caps it).
    int64_t lo = 1, hi = rows;
    while (lo < hi) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (amount(mid) >= population) hi = mid;
        else lo = mid + 1;
    }
    PopulationModifier m;
    m.production = static_cast<int>(r.setting(std::format("Pop Modifier {} Production Modifier Percent", lo), 100));
    m.shipyard = static_cast<int>(r.setting(std::format("Pop Modifier {} SY Rate Modifier Percent", lo), 100));
    return m;
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
    // The release notes give the range (-5 .. +5); the steps between are ours (inferred).
    switch (m) {
        case Mood::Rioting: return -5;
        case Mood::Angry: return -4;
        case Mood::Unhappy: return -2;
        case Mood::Indifferent: return 0;
        case Mood::Happy: return 2;
        case Mood::Jubilant: return 5;
    }
    return 0;
}

// ---- Conditions -------------------------------------------------------------------------------------

ConditionsBand conditionsBand(int conditions) {
    // Five 20-point bands, higher is better (inferred; spec 02 §13 Q8).
    if (conditions >= 80) return ConditionsBand::Pleasant;
    if (conditions >= 60) return ConditionsBand::Mild;
    if (conditions >= 40) return ConditionsBand::Unpleasant;
    if (conditions >= 20) return ConditionsBand::Harsh;
    return ConditionsBand::Deadly;
}

std::string_view conditionsName(ConditionsBand b) {
    switch (b) {
        case ConditionsBand::Pleasant: return "Pleasant";
        case ConditionsBand::Mild: return "Mild";
        case ConditionsBand::Unpleasant: return "Unpleasant";
        case ConditionsBand::Harsh: return "Harsh";
        case ConditionsBand::Deadly: return "Deadly";
    }
    return "?";
}

namespace {

// Environmental Resistance 100 takes the full effect, 200 none (inferred).
int resisted(int amount, int environmentalResistance) { return amount * std::max(0, 200 - environmentalResistance) / 100; }

} // namespace

int conditionsReproductionPenalty(ConditionsBand b, int environmentalResistance) {
    static constexpr std::array<int, 5> kPenalty{0, 1, 2, 3, 5};  // (inferred)
    return resisted(kPenalty[static_cast<size_t>(b)], environmentalResistance);
}

int conditionsAnger(ConditionsBand b, int environmentalResistance) {
    static constexpr std::array<int, 5> kAnger{0, 5, 10, 15, 25};  // tenths of a percent per turn (inferred)
    return resisted(kAnger[static_cast<size_t>(b)], environmentalResistance);
}

// ---- Spaceports, blockades, reproduction ------------------------------------------------------------

bool spaceportInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    if (r.hasTrait(s.empire(e).race, "No Spaceports")) return true;
    for (const Colony* c : coloniesInSystem(s, e, sys)) {
        if (!facilitiesWork(*c)) continue;
        for (uint32_t f : c->facilities)
            if (hasAbility(r.facilityAbilities(f), AbilityKind::Spaceport)) return true;
    }
    return false;
}

bool colonyBlockaded(const Rules& r, const GameState& s, const Colony& c) {
    const Location where = locationOf(s.galaxy, c.planet);
    for (const Vehicle& v : s.vehicles) {
        if (v.location != where || v.count <= 0 || v.owner == c.owner || !hostile(s, c.owner, v.owner)) continue;
        // Mothballed ships never blockade; cloaked ones most likely not (inferred); mines are hidden.
        if (v.status != VehicleStatus::Normal) continue;
        const ruleset::VehicleType t = vehicleType(r, s, v);
        if (t == ruleset::VehicleType::Mine || t == ruleset::VehicleType::Troop) continue;
        return true;
    }
    return false;
}

int reproductionPercent(const Rules& r, const GameState& s, const Colony& c, EmpireId raceOf) {
    const EmpireId who = raceOf.valid() && raceOf.index() < s.empires.size() ? raceOf : c.owner;
    const Race& race = s.empire(who).race;
    const SpaceObject& planet = s.galaxy.object(c.planet);
    int rate = static_cast<int>(r.setting("Empire Starting Percent Reproduction", 10)) + charBonus(race, Characteristic::Reproduction);
    rate += static_cast<int>(bestInSystem(r, s, c.owner, planet.system, AbilityKind::ModifyReproductionSystem));
    rate += moodReproduction(moodFromAnger(c.anger));
    rate -= conditionsReproductionPenalty(conditionsBand(planet.conditions), race.characteristic(Characteristic::EnvironmentalResistance));
    return rate;
}

int plagueProtection(const Rules& r, const GameState& s, const Colony& c) {
    if (r.hasTrait(s.empire(c.owner).race, "No Plagues")) return 1'000'000;
    return static_cast<int>(bestInSystem(r, s, c.owner, s.galaxy.object(c.planet).system, AbilityKind::PlaguePreventionSystem));
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
constexpr std::array<Characteristic, 3> kAptitude{Characteristic::MiningAptitude, Characteristic::FarmingAptitude,
                                                  Characteristic::RefiningAptitude};

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

// (100 + modifier sum) with no negative totals (spec 02 §5.1).
int combinedPercent(int modifierSum) { return 100 + std::max(modifierSum, -100); }

} // namespace

ColonyOutput colonyOutput(const Rules& r, const GameState& s, const Colony& c) {
    ColonyOutput out;
    const Empire& owner = s.empire(c.owner);
    const Race& race = owner.race;
    const ruleset::Culture* culture = r.culture(race);
    const SpaceObject& planet = s.galaxy.object(c.planet);
    const int64_t population = c.totalPopulation();

    out.mood = moodFromAnger(c.anger);
    out.connected = spaceportInSystem(r, s, c.owner, planet.system);
    out.blockaded = colonyBlockaded(r, s, c);
    out.reproductionPercent = reproductionPercent(r, s, c, c.owner);
    if (out.blockaded || out.mood == Mood::Rioting) out.deliveryPercent = 0;
    else if (out.connected) out.deliveryPercent = 100;
    else if (planet.system == homeSystem(s, c.owner))  // spec 02 §13 Q12 (inferred)
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

    // Every facility works however small the population: the stock tutorial has a
    // new 4M colony produce once it has miners. `Population Required to Operate
    // One Facility` is therefore not applied to output (spec 02 §13 Q3).
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
    if (out.mood == Mood::Rioting) return out;  // no points at all while rioting

    const std::vector<ParsedAbility> own = workingAbilities(r, s, c);
    const int popPct = populationModifier(r, population).production;
    const int moodPct = moodOutputPercent(r, out.mood);
    const int common = (popPct - 100) + (moodPct - 100);
    const bool finite = s.options.finiteResources;
    const int stars = starCount(s, planet.system);
    for (size_t i = 0; i < 3; ++i) {
        const int mod = common + charBonus(race, kAptitude[i]) + (culture ? culture->production : 0) +
                        static_cast<int>(bestOf(own, kPlanetMod[i])) +
                        static_cast<int>(bestInSystem(r, s, c.owner, planet.system, kSystemMod[i]));
        out.productionPercent[i] = combinedPercent(mod);
        int64_t made = 0;
        if (finite) {
            // The value is the stock left: it caps and is drawn down by production (inferred).
            made = std::min<int64_t>(base[i] * out.productionPercent[i] / 100, std::max(0, planet.value[i]));
            out.depletion.v[i] = made;
        } else {
            made = base[i] * planet.value[i] / 100 * out.productionPercent[i] / 100;
        }
        out.solar.v[i] = solar[i] * stars;  // no modifiers, no depletion (spec 02 §1.5)
        out.production.v[i] = made + out.solar.v[i];
    }

    // Research and intelligence: the same terms without planet value (spec 05 §1.1, §2.1).
    auto pointPercent = [&](Characteristic trait, int cultureBonus, AbilityKind planetMod, AbilityKind systemMod) {
        return combinedPercent(common + charBonus(race, trait) + cultureBonus + static_cast<int>(bestOf(own, planetMod)) +
                               static_cast<int>(bestInSystem(r, s, c.owner, planet.system, systemMod)));
    };
    out.researchPercent = pointPercent(Characteristic::Intelligence, culture ? culture->research : 0, AbilityKind::PlanetPointGenModResearch,
                                       AbilityKind::SystemPointGenModResearch);
    out.intelligencePercent = pointPercent(Characteristic::Cunning, culture ? culture->intelligence : 0,
                                           AbilityKind::PlanetPointGenModIntelligence, AbilityKind::SystemPointGenModIntelligence);
    out.research = baseResearch * out.researchPercent / 100;
    out.intelligence = baseIntel * out.intelligencePercent / 100;
    return out;
}

// ---- Treasury --------------------------------------------------------------------------------------------

Resources storageCapacity(const Rules& r, const GameState& s, EmpireId e) {
    const int64_t floor = r.setting("Minimum Empire Point Storage", 50000);
    Resources cap{floor, floor, floor};
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e || !facilitiesWork(*c)) continue;
        for (uint32_t f : c->facilities) {
            const auto ab = r.facilityAbilities(f);
            cap[Resource::Minerals] += sumValue1(ab, AbilityKind::ResourceStorageMinerals);
            cap[Resource::Organics] += sumValue1(ab, AbilityKind::ResourceStorageOrganics);
            cap[Resource::Radioactives] += sumValue1(ab, AbilityKind::ResourceStorageRadioactives);
        }
    }
    return cap;
}

int maintenancePercent(const Rules& r, const Empire& e) {
    const ruleset::Culture* culture = r.culture(e.race);
    const int64_t pct = r.setting("Empire Starting Percent Maint Cost", 25) - charBonus(e.race, Characteristic::MaintenanceAptitude) -
                        (culture ? culture->maintenance : 0);
    return static_cast<int>(std::max<int64_t>(pct, 5));  // "at least 5 %" [H]
}

namespace {

// A design's own maintenance modifier: hull and component `Modified Maintenance Cost` (spec 03 §16).
int64_t designMaintenanceModifier(const Rules& r, const Design& d) {
    int64_t mod = sumValue1(r.hullAbilities(d.hull), AbilityKind::ModifiedMaintenanceCost);
    for (const DesignEntry& e : d.entries) mod += sumValue1(r.componentAbilities(e.component), AbilityKind::ModifiedMaintenanceCost);
    return mod;
}

Resources maintenanceOf(const Rules& r, const GameState& s, const Vehicle& v, int pct, const Resources& cost) {
    if (v.status == VehicleStatus::Mothballed || v.count <= 0) return {};
    const Design& d = s.design(v.design);
    const int64_t hullFactor = std::max<int64_t>(0, 100 + designMaintenanceModifier(r, d));
    const int64_t reduction = bestInSystem(r, s, v.owner, v.location.system, AbilityKind::ReducedMaintenanceSystem);
    const int64_t sysFactor = std::max<int64_t>(0, 100 - reduction);
    Resources out;
    for (size_t i = 0; i < 3; ++i) out.v[i] = cost.v[i] * pct * hullFactor * sysFactor / 1'000'000 * v.count;
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

int64_t openingResearchPool(const GameState& s) {
    // Turn 1 brings a one-off pool; we assume it equals the starting resources (inferred, spec 05 §1.1).
    return s.turn == 0 ? std::max<int64_t>(0, s.options.startingResources[Resource::Minerals]) : 0;
}

// ---- Income ----------------------------------------------------------------------------------------------

namespace {

constexpr std::array<AbilityKind, 3> kRemoteGen{AbilityKind::RemoteResourceGenMinerals, AbilityKind::RemoteResourceGenOrganics,
                                                AbilityKind::RemoteResourceGenRadioactives};
constexpr std::array<AbilityKind, 3> kGeneratePoints{AbilityKind::GeneratePointsMinerals, AbilityKind::GeneratePointsOrganics,
                                                     AbilityKind::GeneratePointsRadioactives};

// One remote-mining extraction this turn (spec 03 §3.3).
struct Extraction {
    EmpireId empire;
    ObjectId source;
    size_t resource = 0;
    int64_t amount = 0;
};

// Only one extractor works per location and resource: the empire's strongest there (inferred).
std::vector<Extraction> remoteMining(const Rules& r, const GameState& s) {
    std::map<std::tuple<EmpireId, Location, size_t>, int64_t> best;
    for (const Vehicle& v : s.vehicles) {
        if (!v.owner.valid() || v.count <= 0 || v.status == VehicleStatus::Mothballed) continue;
        std::array<int64_t, 3> rate{};
        bool any = false;
        const Design& d = s.design(v.design);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            if (!entryIntact(r, s, v, i)) continue;
            for (size_t k = 0; k < 3; ++k) {
                rate[k] += sumValue1(r.componentAbilities(d.entries[i].component), kRemoteGen[k]);
                any = any || rate[k] > 0;
            }
        }
        if (!any) continue;
        for (size_t k = 0; k < 3; ++k) {
            if (rate[k] <= 0) continue;
            int64_t& slot = best[{v.owner, v.location, k}];
            slot = std::max(slot, rate[k] * v.count);
        }
    }
    std::vector<Extraction> out;
    for (const auto& [key, rate] : best) {
        const auto& [empire, where, k] = key;
        for (ObjectId o : planetsAt(s, where)) {
            if (s.colony(o)) continue;  // asteroids or uncolonized planets only
            const SpaceObject& obj = s.galaxy.object(o);
            const int64_t value = std::max(0, obj.value[k]);
            const int64_t amount = s.options.finiteResources ? std::min(rate, value) : rate * value / 100;
            if (amount > 0) out.push_back({empire, o, k, amount});
            break;
        }
    }
    return out;
}

void applyRemoteDepletion(const Rules& r, GameState& s, const std::vector<Extraction>& mined) {
    const bool decreases = r.settingFlag("Remote Mining Decreases Asteroid Value", true);
    const int64_t minPct = r.setting("Minimum Planet Percent Value", 0);
    const int64_t minStock = r.setting("Minimum Planet Resource Value", 0);
    for (const Extraction& x : mined) {
        int& value = s.galaxy.object(x.source).value[x.resource];
        if (s.options.finiteResources)
            value = static_cast<int>(std::max<int64_t>(minStock, value - x.amount));
        else if (decreases)
            value = static_cast<int>(std::max<int64_t>(minPct, value - 1));  // one point per turn mined (inferred)
    }
}

// Everything an empire takes in and must pay this turn, computed without changing the state.
struct Income {
    Resources colonies, undelivered, trade, tariffsIn, remote, other, floorTopUp, tariffsOut;
    int64_t research = 0;
    int64_t intelligence = 0;
    bool hasColony = false;

    Resources gross() const { return colonies + trade + tariffsIn + remote + other + floorTopUp; }
};

struct TurnOutputs {
    std::vector<std::optional<ColonyOutput>> colonies;  // per ObjectId
    std::vector<Extraction> mined;
    std::vector<Income> incomes;                          // per EmpireId
};

TurnOutputs computeTurnOutputs(const Rules& r, const GameState& s) {
    TurnOutputs t;
    t.colonies.resize(s.colonies.size());
    t.incomes.resize(s.empires.size());
    t.mined = remoteMining(r, s);
    for (size_t i = 0; i < s.colonies.size(); ++i) {
        const auto& c = s.colonies[i];
        if (!c || !c->owner.valid() || c->owner.index() >= s.empires.size()) continue;
        const ColonyOutput out = colonyOutput(r, s, *c);
        Income& inc = t.incomes[c->owner.index()];
        inc.hasColony = true;
        const Resources delivered = out.delivered();
        inc.colonies += delivered;
        inc.undelivered += out.production - delivered;
        inc.research += out.deliveredResearch();
        inc.intelligence += out.deliveredIntelligence();
        // Flat points from facilities: no modifiers, no delivery rule (spec 02 §1.5).
        if (facilitiesWork(*c))
            for (uint32_t f : c->facilities) {
                const auto ab = r.facilityAbilities(f);
                for (size_t k = 0; k < 3; ++k) inc.other.v[k] += sumValue1(ab, kGeneratePoints[k]);
                inc.research += sumValue1(ab, AbilityKind::GeneratePointsResearch);
                inc.intelligence += sumValue1(ab, AbilityKind::GeneratePointsIntelligence);
            }
        t.colonies[i] = out;
    }
    for (const Vehicle& v : s.vehicles) {
        if (!v.owner.valid() || v.owner.index() >= s.empires.size() || v.count <= 0 || v.status == VehicleStatus::Mothballed) continue;
        Income& inc = t.incomes[v.owner.index()];
        const auto ab = vehicleAbilities(r, s, v);
        for (size_t k = 0; k < 3; ++k) inc.other.v[k] += sumValue1(ab, kGeneratePoints[k]) * v.count;
        inc.research += sumValue1(ab, AbilityKind::GeneratePointsResearch) * v.count;
        inc.intelligence += sumValue1(ab, AbilityKind::GeneratePointsIntelligence) * v.count;
    }
    for (const Extraction& x : t.mined) t.incomes[x.empire.index()].remote.v[x.resource] += x.amount;

    for (size_t i = 0; i < s.empires.size(); ++i) {
        const Empire& e = s.empires[i];
        Income& inc = t.incomes[i];
        if (!e.alive) continue;
        // Trade and tariffs received come from diplomacy as one sum; split out the tariffs for the report.
        const Resources fromDiplomacy = max(diplomacy::tradeIncome(r, s, e.id), Resources{});
        inc.tariffsIn = min(max(diplomacy::tariffsReceived(r, s, e.id), Resources{}), fromDiplomacy);
        inc.trade = fromDiplomacy - inc.tariffsIn;
        inc.tariffsOut = max(diplomacy::tariffsPaid(r, s, e.id), Resources{});
        inc.research += std::max<int64_t>(0, diplomacy::researchTradeIncome(r, s, e.id));
        inc.intelligence += std::max<int64_t>(0, diplomacy::intelTradeIncome(r, s, e.id));
        inc.research += openingResearchPool(s);
        // Income floor on gross income, for empires that still hold a planet (inferred, spec 02 §13 Q11).
        if (inc.hasColony) {
            const Resources gross = inc.gross();
            const Resources floor{r.setting("Minimum Empire Minerals Generation", 200), r.setting("Minimum Empire Organics Generation", 200),
                                  r.setting("Minimum Empire Radioactives Generation", 200)};
            inc.floorTopUp = max(floor - gross, Resources{});
        }
    }
    return t;
}

// Unpaid maintenance destroys vehicles: one per `Maintenance Cost Amt Per Dead`,
// summed over the resources, victims drawn at random (inferred details, spec 02 §7).
void scuttleForMaintenance(TurnContext& ctx, EmpireId e, int64_t unpaid) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t perDead = std::max<int64_t>(1, r.setting("Maintenance Cost Amt Per Dead", 20000));
    int64_t victims = (unpaid + perDead - 1) / perDead;
    while (victims-- > 0) {
        std::vector<VehicleId> candidates;
        for (const Vehicle& v : s.vehicles)
            if (v.owner == e && v.count > 0 && v.status != VehicleStatus::Mothballed && !vehicleMaintenance(r, s, v).isZero())
                candidates.push_back(v.id);
        if (candidates.empty()) break;
        Vehicle& v = *s.vehicle(candidates[s.rng.below(candidates.size())]);
        const bool ship = isShipOrBase(vehicleType(r, s, v));
        ctx.log(e, LogCategory::Construction, std::format("{} scuttled", v.name),
                "It fell into disrepair: the empire could not pay its maintenance.", v.location);
        ++s.design(v.design).lost;
        --v.count;
        if (ship) {
            v.count = 0;
            ctx.mood(e, "Any Ship Lost", v.location.system);
            ctx.mood(e, "Ship Lost in System", v.location.system);
        }
    }
    s.removeDeadVehicles();
}

} // namespace

// ---- Phase 5 -------------------------------------------------------------------------------------------

namespace {

// Yearly planet drift from facilities: value and conditions (inferred cadence, spec 02 §1.5).
void applyPlanetDrift(const Rules& r, GameState& s) {
    const bool finite = s.options.finiteResources;
    const int64_t lo = r.setting(finite ? "Minimum Planet Resource Value" : "Minimum Planet Percent Value", 0);
    const int64_t hi = r.setting(finite ? "Maximum Planet Resource Value" : "Maximum Planet Percent Value", finite ? 999'000'000 : 250);
    constexpr std::array<AbilityKind, 3> kValue{AbilityKind::PlanetChangeMineralsValue, AbilityKind::PlanetChangeOrganicsValue,
                                                AbilityKind::PlanetChangeRadioactivesValue};
    struct Change {
        ObjectId planet;
        std::array<int64_t, 3> value{};
        int64_t conditions = 0;
    };
    std::vector<Change> changes;  // computed first so the order of colonies does not matter
    for (const auto& c : s.colonies) {
        if (!c || !facilitiesWork(*c)) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        const std::vector<ParsedAbility> own = workingAbilities(r, s, *c);
        Change ch{c->planet};
        const int64_t sysValue = bestInSystem(r, s, c->owner, sys, AbilityKind::PlanetValueChangeSystem);
        for (size_t k = 0; k < 3; ++k) ch.value[k] = bestOf(own, kValue[k]) + sysValue;
        ch.conditions = bestOf(own, AbilityKind::PlanetChangeConditions) +
                        bestInSystem(r, s, c->owner, sys, AbilityKind::PlanetConditionsChangeSystem);
        changes.push_back(ch);
    }
    for (const Change& ch : changes) {
        SpaceObject& p = s.galaxy.object(ch.planet);
        for (size_t k = 0; k < 3; ++k) {
            if (ch.value[k] == 0) continue;
            const int64_t v = finite ? p.value[k] + p.value[k] * ch.value[k] / 100 : p.value[k] + ch.value[k];
            p.value[k] = static_cast<int>(std::clamp(v, lo, hi));
        }
        if (ch.conditions != 0) p.conditions = static_cast<int>(std::clamp<int64_t>(p.conditions + ch.conditions, 0, 100));
    }
}

EconomyReport reportFrom(const Income& inc) {
    EconomyReport rep;
    rep.colonies = inc.colonies;
    rep.undelivered = inc.undelivered;
    rep.trade = inc.trade;
    rep.tariffsIn = inc.tariffsIn;
    rep.remoteMining = inc.remote;
    rep.otherIncome = inc.other + inc.floorTopUp;
    rep.research = inc.research;
    rep.intelligence = inc.intelligence;
    return rep;
}

} // namespace

void runEconomy(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;

    // 1. Planet changes that run once a year.
    if (s.turn > 0 && s.turn % 10 == 0) applyPlanetDrift(r, s);

    // 2-6. Output, delivery and other income, all computed before anything changes.
    const TurnOutputs outputs = computeTurnOutputs(r, s);
    for (size_t i = 0; i < outputs.colonies.size(); ++i)
        if (outputs.colonies[i] && !outputs.colonies[i]->depletion.isZero()) {
            SpaceObject& p = s.galaxy.object(ObjectId{i});
            for (size_t k = 0; k < 3; ++k) p.value[k] = std::max(0, p.value[k] - static_cast<int>(outputs.colonies[i]->depletion.v[k]));
        }
    applyRemoteDepletion(r, s, outputs.mined);

    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        if (!s.empire(id).alive) continue;
        const Income& inc = outputs.incomes[i];
        EconomyReport rep = reportFrom(inc);
        s.empire(id).stockpile += inc.gross();

        // 7. Maintenance comes first; a shortfall scuttles vehicles.
        rep.maintenance = maintenanceCost(r, s, id);
        int64_t unpaid = 0;
        {
            Resources& bank = s.empire(id).stockpile;
            for (size_t k = 0; k < 3; ++k) {
                const int64_t paid = std::min(bank.v[k], rep.maintenance.v[k]);
                bank.v[k] -= paid;
                unpaid += rep.maintenance.v[k] - paid;
            }
        }
        if (unpaid > 0) scuttleForMaintenance(ctx, id, unpaid);

        // 8. Tariffs paid never exceed what the empire holds.
        {
            Resources& bank = s.empire(id).stockpile;
            rep.tariffsOut = min(inc.tariffsOut, bank);
            bank -= rep.tariffsOut;
        }

        // 9. Construction queues.
        rep.construction = runConstruction(ctx, id);

        // 10. Storage cap: the excess is lost.
        Empire& e = s.empire(id);
        rep.storageCap = storageCapacity(r, s, id);
        rep.lostToStorage = max(e.stockpile - rep.storageCap, Resources{});
        e.stockpile -= rep.lostToStorage;
        e.economy = rep;
    }

    // 12. Emergency and slow build counters.
    advanceQueueModes(r, s);
}

void updateReports(const Rules& r, GameState& s) {
    const TurnOutputs outputs = computeTurnOutputs(r, s);
    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        EconomyReport rep = reportFrom(outputs.incomes[i]);
        rep.storageCap = storageCapacity(r, s, id);
        if (s.empire(id).alive) {
            Resources bank = s.empire(id).stockpile + outputs.incomes[i].gross();
            rep.maintenance = maintenanceCost(r, s, id);
            bank = max(bank - rep.maintenance, Resources{});
            rep.tariffsOut = min(outputs.incomes[i].tariffsOut, bank);
            bank -= rep.tariffsOut;
            for (const QueueRef& q : empireQueues(s, id)) {
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
