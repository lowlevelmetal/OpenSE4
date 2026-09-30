// Computer player: colony types and the construction ministers (spec 05 §7.5
// AI_Planet_Types, AI_Construction_Facilities, AI_Construction_Vehicles,
// confirmed: binary unless marked).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>

namespace opense4::game::ai {

namespace {

using datafile::keysEqual;
using namespace detail;

int stellarRank(std::string_view name) {
    static constexpr std::array<std::string_view, 5> kSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
    for (size_t i = 0; i < kSizes.size(); ++i)
        if (keysEqual(name, kSizes[i])) return static_cast<int>(i);
    return -1;
}

int planetRank(const Rules& r, const SpaceObject& planet) {
    if (const ruleset::PlanetSize* ps = planetSize(r, planet)) return ps->constructed ? 5 : stellarRank(ps->stellarSize);
    return stellarRank(planet.size);
}

// The size a `Minimum Planet Size for Type` entry names: a PlanetSize.txt
// entry, or a stellar size name.
int namedRank(const Rules& r, std::string_view name) {
    for (const ruleset::PlanetSize& ps : r.data().planetSizes)
        if (keysEqual(ps.name, name)) return ps.constructed ? 5 : stellarRank(ps.stellarSize);
    return stellarRank(name);
}

bool allResearchDone(const Rules& r, const GameState& s, const Empire& e) {
    for (uint32_t a = 0; a < r.data().techAreas.size(); ++a) {
        const ruleset::TechAreaId id{a};
        if (r.techVisible(s, e, id) && e.techLevel(id) < r.tech(id).maxLevel) return false;
    }
    return true;
}

bool sameType(std::string_view a, std::string_view b) {
    const ColonyType ta = parseColonyType(a), tb = parseColonyType(b);
    if (ta != ColonyType::Count || tb != ColonyType::Count) return ta == tb;
    return keysEqual(a, b);
}

} // namespace

std::string colonyTypeAtColonization(const Rules& r, const GameState& s, EmpireId id, ObjectId planetId) {
    const Empire& e = s.empire(id);
    const AiProfile& prof = profileFor(r, e);
    const AiState state = stateOf(e);
    const SpaceObject& planet = s.galaxy.object(planetId);
    const EconomyReport& eco = e.economy;
    const std::string mining(displayName(ColonyType::Mining));

    // Pre-rules.
    if (e.stockpile[Resource::Minerals] <= 2000 && planet.value[0] > 80) return mining;
    const Resources production = eco.colonies + eco.remoteMining + eco.otherIncome;
    const Resources deficit = eco.maintenance - (eco.trade + eco.tariffsIn) - production;
    if (deficit[Resource::Organics] > 2000 && planet.value[1] >= 50) return std::string(displayName(ColonyType::Farming));
    if (deficit[Resource::Minerals] > 2000 && planet.value[0] >= 50) return mining;
    if (deficit[Resource::Radioactives] > 2000 && planet.value[2] >= 50) return std::string(displayName(ColonyType::Refining));

    // The first AI_Planet_Types row that passes all its tests.
    int64_t colonies = 0;
    for (const auto& c : s.colonies) colonies += c && c->owner == id && c->planet != planetId;
    const int64_t low = r.setting("Planet Value Low Resources", 0), high = r.setting("Planet Value High Resources", 0);
    const int64_t mid = low + (high - low) / 2;
    for (const PlanetTypeRow& row : prof.planetTypes) {
        if (!(row.states & maskOf(state))) continue;
        const ColonyType type = parseColonyType(row.type);
        if (type == ColonyType::ResearchCompound &&
            (allResearchDone(r, s, e) || eco.research >= prof.settings.maxResearchPoints))
            continue;
        if (type == ColonyType::IntelligenceCompound && (eco.intelligence >= prof.settings.maxIntelligencePoints || !s.options.allowIntel))
            continue;
        int64_t ofType = 0, inSystem = 0;
        for (const auto& c : s.colonies) {
            if (!c || c->owner != id || c->planet == planetId || !sameType(c->colonyType, row.type)) continue;
            ++ofType;
            inSystem += s.galaxy.object(c->planet).system == planet.system;
        }
        if (row.maxInEmpire > 0 && ofType >= row.maxInEmpire) continue;
        if (row.maxPerSystem > 0 && inSystem >= row.maxPerSystem) continue;
        if (row.percentOfColonies > 0 && ofType * 100 > colonies * row.percentOfColonies) continue;
        if (!row.minimumSize.empty() && planetRank(r, planet) < namedRank(r, row.minimumSize)) continue;
        bool values = true;
        for (size_t k = 0; k < 3; ++k) {
            const int64_t t = row.values[k];
            if (t <= 100) continue;  // only thresholds above 100 count
            if (s.options.finiteResources ? int64_t{planet.value[k]} * 100 < mid * t : planet.value[k] < t) values = false;
        }
        if (!values) continue;
        return row.type;
    }
    return mining;
}

namespace detail {

namespace {

// ---- Facilities -----------------------------------------------------------------------------

bool researchAbility(AbilityKind k) {
    return k == AbilityKind::PointGenResearch || k == AbilityKind::PlanetPointGenModResearch || k == AbilityKind::SystemPointGenModResearch ||
           k == AbilityKind::GeneratePointsResearch;
}
bool intelAbility(AbilityKind k) {
    return k == AbilityKind::PointGenIntelligence || k == AbilityKind::PlanetPointGenModIntelligence ||
           k == AbilityKind::SystemPointGenModIntelligence || k == AbilityKind::GeneratePointsIntelligence;
}
// Resource generation or a planet-value modifier: which resource (0..2), or -1.
int resourceOf(AbilityKind k) {
    switch (k) {
        case AbilityKind::ResourceGenMinerals:
        case AbilityKind::ResourceGenModPlanetMinerals:
        case AbilityKind::ResourceGenModSystemMinerals:
        case AbilityKind::PlanetChangeMineralsValue: return 0;
        case AbilityKind::ResourceGenOrganics:
        case AbilityKind::ResourceGenModPlanetOrganics:
        case AbilityKind::ResourceGenModSystemOrganics:
        case AbilityKind::PlanetChangeOrganicsValue: return 1;
        case AbilityKind::ResourceGenRadioactives:
        case AbilityKind::ResourceGenModPlanetRadioactives:
        case AbilityKind::ResourceGenModSystemRadioactives:
        case AbilityKind::PlanetChangeRadioactivesValue: return 2;
        default: return -1;
    }
}
// The abilities a system needs only one of among the empire's colonies there
// (spec 05 §7.5, confirmed: binary): a fixed list, which holds none of the
// stellar `System - ...` abilities.
bool onePerSystem(AbilityKind k) {
    switch (k) {
        case AbilityKind::Palace:
        case AbilityKind::ResourceGenModSystemMinerals:
        case AbilityKind::ResourceGenModSystemOrganics:
        case AbilityKind::ResourceGenModSystemRadioactives:
        case AbilityKind::SystemPointGenModResearch:
        case AbilityKind::SystemPointGenModIntelligence:
        case AbilityKind::CombatModifierSystem:
        case AbilityKind::DamageModifierSystem:
        case AbilityKind::ShieldModifierSystem:
        case AbilityKind::PlanetValueChangeSystem:
        case AbilityKind::PlanetConditionsChangeSystem:
        case AbilityKind::ChangeBadEventChanceSystem:
        case AbilityKind::ChangeBadIntelChanceSystem:
        case AbilityKind::ChangePopulationHappinessSystem:
        case AbilityKind::ChangePopulationSystem:
        case AbilityKind::ModifyReproductionSystem:
        case AbilityKind::PlaguePreventionSystem:
        case AbilityKind::ShipTraining:
        case AbilityKind::FleetTraining:
        case AbilityKind::ShipTrainingSystem:
        case AbilityKind::FleetTrainingSystem:
        case AbilityKind::ResourceConversion:
        case AbilityKind::LongRangeScannerSystem:
        case AbilityKind::ReducedMaintenanceSystem:
        case AbilityKind::StopStarDestroyer:
        case AbilityKind::StopNebulaeCreator:
        case AbilityKind::StopBlackHoleCreator:
        case AbilityKind::StopOpenWarpPoint:
        case AbilityKind::StopCloseWarpPoint: return true;
        default: return false;
    }
}

int facilitiesWith(const Rules& r, const Colony& c, std::string_view ability) {
    int n = 0;
    for (uint32_t f : c.facilities) n += facilityHas(r, f, ability);
    for (const QueueItem& q : c.queue.items) n += q.kind == QueueItem::Kind::Facility && facilityHas(r, q.facility, ability);
    return n;
}

bool systemHas(const Planner& p, SystemId sys, std::string_view ability) {
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id && p.st.galaxy.object(c->planet).system == sys && facilitiesWith(p.r, *c, ability) > 0) return true;
    return false;
}

// Is the planet's atmosphere already right for the majority race?
bool atmosphereSuits(const GameState& s, const Colony& c) {
    EmpireId majority;
    int64_t most = -1;
    for (const PopulationGroup& g : c.population)
        if (g.millions > most) {
            most = g.millions;
            majority = g.race;
        }
    if (!majority.valid() || majority.index() >= s.empires.size()) return true;
    return keysEqual(s.galaxy.object(c.planet).atmosphere, s.empire(majority).race.atmosphere);
}

// Spec 05 §7.5: the rules that block a facility entry.
bool blocked(const Planner& p, const Colony& c, const FacilityEntry& entry) {
    const auto kind = parseAbilityKind(entry.ability).value_or(AbilityKind::Unknown);
    const SystemId sys = p.st.galaxy.object(c.planet).system;
    if (entry.amount <= 0 || facilitiesWith(p.r, c, entry.ability) >= entry.amount) return true;
    switch (kind) {
        case AbilityKind::Spaceport: return systemHas(p, sys, entry.ability) || p.r.hasTrait(p.emp().race, "No Spaceports");
        case AbilityKind::SupplyGeneration: return systemHas(p, sys, entry.ability);
        case AbilityKind::SpaceYard: return facilitiesWith(p.r, c, entry.ability) > 0;
        case AbilityKind::PlanetChangeAtmosphere: return c.totalPopulation() <= 0 || atmosphereSuits(p.st, c);
        default: break;
    }
    const EconomyReport& eco = p.emp().economy;
    if (researchAbility(kind) && (allResearchDone(p.r, p.st, p.emp()) || eco.research >= p.prof.settings.maxResearchPoints)) return true;
    if (intelAbility(kind) && (!p.st.options.allowIntel || eco.intelligence >= p.prof.settings.maxIntelligencePoints)) return true;
    // One per system: the empire's colonies there count with what they have built or queued.
    if (onePerSystem(kind) && systemHas(p, sys, entry.ability)) return true;
    if (p.st.options.finiteResources)
        if (const int res = resourceOf(kind); res >= 0 && p.st.galaxy.object(c.planet).value[static_cast<size_t>(res)] == 0) return true;
    return false;
}

// A colony without a type (spec 05 §7.5 "Later"): Homeworld in the home
// system, else the strictly highest resource value; nothing on a tie.
std::string laterType(const Planner& p, const Colony& c) {
    const SpaceObject& planet = p.st.galaxy.object(c.planet);
    if (planet.system == p.sit.home) return std::string(displayName(ColonyType::Homeworld));
    const auto& v = planet.value;
    if (v[0] > v[1] && v[0] > v[2]) return std::string(displayName(ColonyType::Mining));
    if (v[1] > v[0] && v[1] > v[2]) return std::string(displayName(ColonyType::Farming));
    if (v[2] > v[0] && v[2] > v[1]) return std::string(displayName(ColonyType::Refining));
    return {};
}

Resources queueItemCost(const Planner& p, const cmd::QueueTarget& t, const QueueItem& item) { return economy::itemCost(p.r, p.st, p.id, t, item); }

// Every fifth turn (spec 05 §7.5, confirmed: binary): obsolete facilities
// are upgraded to the newest researched version, planet by planet in queue
// order, while what was queued so far is at most half of one turn's net
// income in all three resources, checked before each planet (so a zero
// budget still upgrades the first planet). A planet's queue need not be
// empty, and every queued facility of an older version switches to the
// newest one in place, keeping its count and what was paid into it
// (cmd::QueueReplaceFacility, spec 02 §6.6).
void planUpgrades(Planner& p, const std::vector<ObjectId>& planets) {
    const Resources net = p.netIncome();
    const Resources half{net.v[0] / 2, net.v[1] / 2, net.v[2] / 2};  // division toward zero
    Resources queued;
    for (ObjectId planet : planets) {
        if (queued[Resource::Minerals] > half[Resource::Minerals] || queued[Resource::Organics] > half[Resource::Organics] ||
            queued[Resource::Radioactives] > half[Resource::Radioactives])
            break;
        const Colony* c = p.st.colony(planet);
        if (!c) continue;
        const cmd::QueueTarget target{planet, {}};
        // Queued facilities of an older version switch to the newest one in place.
        for (size_t i = 0; i < c->queue.items.size(); ++i) {
            const QueueItem& item = c->queue.items[i];
            if (item.kind != QueueItem::Kind::Facility || p.r.facility(item.facility).family == 0) continue;
            const auto latest = p.r.latestFacilityOfFamily(p.emp(), p.r.facility(item.facility).family);
            if (!latest || *latest == item.facility || p.r.facility(*latest).romanNumeral <= p.r.facility(item.facility).romanNumeral) continue;
            p.emit(cmd::QueueReplaceFacility{target, static_cast<uint32_t>(i), *latest});
            c = p.st.colony(planet);
        }
        c = p.st.colony(planet);
        std::vector<int> families;
        for (uint32_t f : c->facilities) {
            const int family = p.r.facility(f).family;
            if (family == 0 || std::find(families.begin(), families.end(), family) != families.end()) continue;
            const auto latest = p.r.latestFacilityOfFamily(p.emp(), family);
            if (!latest || p.r.facility(*latest).romanNumeral <= p.r.facility(f).romanNumeral) continue;
            bool pending = false;
            for (const QueueItem& q : c->queue.items)
                pending = pending || (q.kind == QueueItem::Kind::Upgrade && p.r.facility(q.facility).family == family);
            if (pending) continue;
            families.push_back(family);
            const QueueItem item = economy::upgradeItem(p.r, *c, *latest);  // to the newest level (spec 02 §6.6)
            const Resources cost = queueItemCost(p, target, item);
            if (p.emit(cmd::QueueAdd{target, item, -1})) queued += cost;
            c = p.st.colony(planet);
        }
    }
}

// ---- Vehicles -------------------------------------------------------------------------------

bool colonyShipType(std::string_view t) { return t.starts_with("Colony"); }

bool positive(const Resources& b) { return b[Resource::Minerals] > 0 && b[Resource::Organics] > 0 && b[Resource::Radioactives] > 0; }

// A queue's backlog in turns (spec 05 §7.5, confirmed: binary): the sum over
// its items of each item's turns, the largest over the resources the queue
// has a positive rate for of what the item still costs ÷ that rate, rounded
// up. The first item counts only what is still unpaid. A queue whose rates
// are all 0 counts 9,999.
int64_t backlogTurns(const Planner& p, const cmd::QueueTarget& target, const ConstructionQueue& q, const Resources& rate) {
    if (rate[Resource::Minerals] <= 0 && rate[Resource::Organics] <= 0 && rate[Resource::Radioactives] <= 0) return 9999;
    int64_t total = 0;
    for (size_t i = 0; i < q.items.size(); ++i) {
        const QueueItem& item = q.items[i];
        Resources left = economy::itemCost(p.r, p.st, p.id, target, item);
        if (i == 0) left = max(left - item.spent, Resources{});
        int64_t turns = 0;
        for (Resource k : kResources)
            if (rate[k] > 0 && left[k] > 0) turns = std::max(turns, (left[k] + rate[k] - 1) / rate[k]);
        total += turns;
    }
    return total;
}

// Units come in batches of as many as the queue finishes in one turn, at
// least one; recon satellites one at a time (spec 05 §7.5).
int batchSize(const DesignInfo& di, const Resources& rate) {
    if (keysEqual(di.aiType, "Recon Satellite")) return 1;
    int64_t n = INT64_MAX;
    for (Resource k : kResources)
        if (di.stats.cost[k] > 0) n = std::min(n, rate[k] / di.stats.cost[k]);
    return static_cast<int>(std::clamp<int64_t>(n == INT64_MAX ? 1 : n, 1, 1'000'000));
}

class ShipBuilder {
public:
    explicit ShipBuilder(Planner& p) : p_(p) {}

    void run() {
        const VehicleQueue* table = p_.prof.vehicleQueue(p_.state);
        cleanUp();
        // One turn of net income less the unit reserve (the reserve quirk, unitReserve()).
        budget_ = p_.netIncome().percent(100 - std::clamp<int64_t>(unitReserve(), 0, 100));
        if (table) vehicles(*table);
        units();
    }

private:
    Planner& p_;
    Resources budget_;
    // Own vehicles with a working space yard under this minister, found once
    // per turn: queueing ships changes neither (place() asks for every item).
    std::optional<std::vector<VehicleId>> yardShips_;
    // Whether a design may be queued at a target. Nothing the builder does
    // changes the answer during its turn (queueing builds no ship yet).
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, bool> queueable_;

    bool queueable(DesignId design, const cmd::QueueTarget& target) {
        const auto key = std::tuple(design.value, target.planet.value, target.vehicle.value);
        if (auto it = queueable_.find(key); it != queueable_.end()) return it->second;
        QueueItem probe;
        probe.kind = QueueItem::Kind::Vehicle;
        probe.design = design;
        return queueable_[key] = queueItemProblem(p_.r, p_.st, p_.id, target, probe).empty();
    }

    const std::vector<VehicleId>& yardShips() {
        if (!yardShips_) {
            yardShips_.emplace();
            for (const Vehicle& v : p_.st.vehicles)
                if (p_.controlsVehicle(v, Minister::ShipConstruction) && vehicleHasSpaceYard(p_.r, p_.st, v)) yardShips_->push_back(v.id);
        }
        return *yardShips_;
    }

    // The reserve quirk (spec 05 §7.5 "Units file", confirmed: binary): the
    // start-of-turn AI step resets the reserve to 0 and only a units step
    // loads it, after its empire's vehicle list. So this empire's vehicle
    // list applies what the previous empire's units step left in the same
    // round: the reserve of the last living empire before it, in empire
    // order, whose Ship Construction minister acts and that has a units
    // file; 0 for the first, and always 0 in turn-based games, whose
    // start-of-turn steps come between two players' end-of-turn processing.
    // An empire without a units file leaves the value as it was (inferred).
    int64_t unitReserve() const {
        if (!p_.st.options.simultaneous) return 0;
        for (size_t j = p_.id.index(); j-- > 0;) {
            const Empire& o = p_.st.empires[j];
            if (!o.alive) continue;
            const bool acts = o.kind != PlayerKind::Human || (ministersActive(p_.st, o.id) && ministerOn(o, Minister::ShipConstruction));
            if (!acts) continue;
            const AiProfile& prof = profileFor(p_.r, o);
            if (prof.unitsFile) return prof.unitReservePercent;
        }
        return 0;
    }

    // The design an entry builds (spec 05 §7.5, confirmed: binary): first the
    // AI's own buildable design made from the AI_DesignCreation template of
    // that name (ignoring case), newest first; only then does a design-type
    // entry take the newest buildable design of the type, hand-made ones
    // included, and a Colonizer entry the colony ship of the first uncovered
    // target. Any other text builds nothing. Neither lookup reads the obsolete mark.
    std::optional<DesignId> designFor(const VehicleEntry& entry) {
        if (auto d = p_.newestFromTemplate(entry.type)) return d;
        if (isAiDesignType(entry.type)) return p_.newestDesign(entry.type, true);
        if (keysEqual(entry.type, "Colonizer")) return colonizerDesign();
        return std::nullopt;
    }

    // The vehicle list of the AI state (spec 05 §7.5).
    void vehicles(const VehicleQueue& table) {
        const bool soft = p_.overCap(0), hard = p_.overCap(20);
        const int64_t colonies = p_.colonyCount();
        int placed = 0;
        size_t i = 0;
        while (i < table.entries.size() && placed < 1000 && positive(budget_)) {
            const VehicleEntry& entry = table.entries[i];
            const std::optional<DesignId> design = designFor(entry);
            if (!design) {
                ++i;
                continue;
            }
            // The count and the maintenance-cap gate use the chosen design's own type.
            const std::string type = p_.info(*design).aiType;
            const bool open = type == "Open Warp Point";
            const bool colonyShip = colonyShipType(type);
            if ((hard && !open) || (soft && !open && !colonyShip)) {
                ++i;
                continue;
            }
            const int64_t count = have(type, colonyShip);
            const bool under = count < entry.mustHave || (entry.planetsPerItem > 0 && count * entry.planetsPerItem < colonies * 10);
            if (under && place(*design)) {
                ++placed;
                i = 0;  // restart the scan from the top
            } else {
                ++i;
            }
        }
    }

    // `_AI_Construction_Units` (spec 05 §7.5 "Units file", confirmed: binary):
    // after the vehicle list, from a fresh budget of one full turn of net
    // income, colony by colony in queue order. A colony qualifies when its
    // queue is empty, it has no free facility slot and all three resources
    // of the budget are above 0. Its row is the last record whose Colony
    // Type text contains the colony's type name (ignoring case); a colony
    // without a type gets nothing. The first entry that names an AI design
    // type exactly, whose units in the colony's cargo take less than its
    // Maximum in kT, whose type has a newest valid design and whose design
    // tonnage is below the free cargo space, gets one batch.
    void units() {
        if (p_.prof.units.empty()) return;
        Resources budget = p_.netIncome();
        std::vector<ObjectId> planets;
        for (const auto& c : p_.st.colonies)
            if (c && p_.controlsColony(*c, Minister::ShipConstruction)) planets.push_back(c->planet);
        for (ObjectId planet : planets) {
            if (!positive(budget)) return;
            const Colony& c = *p_.st.colony(planet);
            if (!c.queue.items.empty() || static_cast<int>(c.facilities.size()) < facilitySlots(p_.r, p_.st, c) || c.colonyType.empty()) continue;
            const UnitQueue* row = p_.prof.unitQueue(c.colonyType);
            if (!row) continue;
            const cmd::QueueTarget target{planet, {}};
            const Resources rate = economy::constructionRate(p_.r, p_.st, p_.id, target);
            const int64_t free = colonyCargoCapacity(p_.r, p_.st, c) - cargoSpaceUsed(p_.r, p_.st, c.cargo);
            for (const UnitEntry& entry : row->entries) {
                const bool named = std::any_of(aiDesignTypes().begin(), aiDesignTypes().end(), [&](std::string_view t) { return t == entry.type; });
                if (!named) continue;
                int64_t heldKt = 0;
                for (const UnitStack& u : c.cargo.units)
                    if (u.count > 0 && p_.info(u.design).aiType == entry.type)
                        heldKt += int64_t{u.count} * designTonnage(p_.r, p_.st.design(u.design));
                if (heldKt >= entry.maxKt) continue;
                const std::optional<DesignId> design = p_.newestDesign(entry.type);
                if (!design || designTonnage(p_.r, p_.st.design(*design)) >= free) continue;
                QueueItem item;
                item.kind = QueueItem::Kind::Vehicle;
                item.design = *design;
                item.count = batchSize(p_.info(*design), rate);
                if (!queueItemProblem(p_.r, p_.st, p_.id, target, item).empty()) continue;
                const Resources cost = economy::itemCost(p_.r, p_.st, p_.id, target, item);
                if (!p_.emit(cmd::QueueAdd{target, item, -1})) continue;
                budget -= min(cost, rate);
                break;  // at most one item per colony
            }
        }
    }

    std::vector<std::pair<cmd::QueueTarget, const ConstructionQueue*>> queues() const {
        std::vector<std::pair<cmd::QueueTarget, const ConstructionQueue*>> out;
        for (const auto& c : p_.st.colonies)
            if (c && c->owner == p_.id) out.push_back({{c->planet, {}}, &c->queue});
        for (const Vehicle& v : p_.st.vehicles)
            if (v.owner == p_.id && !v.queue.items.empty()) out.push_back({{{}, v.id}, &v.queue});
        return out;
    }

    // Existing vehicles of a type (units one by one) plus items in all
    // queues; the three colony-ship types share one count.
    int64_t have(std::string_view type, bool colonyShips) {
        std::vector<int8_t> known(p_.st.designs.size(), -1);
        auto test = [&](DesignId d) {
            const DesignInfo& di = p_.info(d);
            return colonyShips ? colonyShipType(di.aiType) : di.aiType == type;
        };
        auto match = [&](DesignId d) {
            if (d.index() >= known.size()) return test(d);
            int8_t& m = known[d.index()];
            if (m < 0) m = test(d) ? 1 : 0;
            return m == 1;
        };
        int64_t n = 0;
        for (const Vehicle& v : p_.st.vehicles) {
            if (v.owner != p_.id) continue;
            if (v.mixed.empty()) {
                if (match(v.design)) n += std::max(1, v.count);
            } else {
                for (const UnitStack& u : v.mixed)
                    if (match(u.design)) n += u.count;
            }
            for (const UnitStack& u : v.cargo.units)
                if (match(u.design)) n += u.count;
        }
        for (const auto& c : p_.st.colonies)
            if (c && c->owner == p_.id)
                for (const UnitStack& u : c->cargo.units)
                    if (match(u.design)) n += u.count;
        for (const auto& [target, q] : queues())
            for (const QueueItem& item : q->items)
                if (item.kind == QueueItem::Kind::Vehicle && match(item.design)) n += std::max(1, item.count);
        return n;
    }

    // The colony-ship type of the first target no queued colony ship covers;
    // the race's own surface when all are covered; nothing without a target.
    std::optional<DesignId> colonizerDesign() {
        std::map<std::string, int> queued;
        for (const auto& [target, q] : queues())
            for (const QueueItem& item : q->items)
                if (item.kind == QueueItem::Kind::Vehicle && p_.info(item.design).role == Role::Colonizer)
                    queued[p_.info(item.design).aiType] += std::max(1, item.count);
        bool any = false;
        for (const ColonyTarget& t : p_.sit.colonyTargets) {
            if (!t.settleable) continue;
            any = true;
            const std::string type = colonyTypeName(p_.st.galaxy.object(t.planet).surface);
            if (queued[type] > 0) {
                --queued[type];
                continue;
            }
            return p_.newestDesign(type, true);
        }
        if (!any) return std::nullopt;
        return p_.newestDesign(colonyTypeName(p_.emp().race.nativeSurface), true);
    }

    bool place(DesignId design) {
        const DesignInfo& di = p_.info(design);
        const bool unit = isUnitType(di.stats.vehicleType);
        const bool base = di.stats.vehicleType == ruleset::VehicleType::Base;
        struct Option {
            cmd::QueueTarget target;
            Resources rate;
            int64_t backlog = 0;
            int bases = 0;
            int64_t value = 0;
        };
        std::vector<Option> options;
        auto consider = [&](const cmd::QueueTarget& target, const ConstructionQueue& q) {
            if (!queueable(design, target)) return;
            const Resources rate = economy::constructionRate(p_.r, p_.st, p_.id, target);
            const int64_t backlog = backlogTurns(p_, target, q, rate);
            if (backlog >= 5) return;  // placed only under 5 turns of backlog
            Option o{target, rate, backlog, 0, 0};
            if (base && target.planet.valid()) {
                const Location at = locationOf(p_.st.galaxy, target.planet);
                for (const Vehicle& v : p_.st.vehicles)
                    if (v.owner == p_.id && v.location == at && p_.info(v.design).stats.vehicleType == ruleset::VehicleType::Base) ++o.bases;
                for (const QueueItem& item : q.items)
                    if (item.kind == QueueItem::Kind::Vehicle && p_.info(item.design).stats.vehicleType == ruleset::VehicleType::Base) ++o.bases;
                const SpaceObject& planet = p_.st.galaxy.object(target.planet);
                o.value = int64_t{planet.value[0]} + planet.value[1] + planet.value[2];
            }
            options.push_back(o);
        };
        for (const auto& c : p_.st.colonies)
            if (c && p_.controlsColony(*c, Minister::ShipConstruction) && (unit || colonyHasSpaceYard(p_.r, *c))) consider({c->planet, {}}, c->queue);
        if (!unit)
            for (VehicleId id : yardShips())
                if (const Vehicle* v = p_.st.vehicle(id)) consider({{}, id}, v->queue);
        if (options.empty()) return false;
        std::sort(options.begin(), options.end(), [&](const Option& a, const Option& b) {
            if (base) return std::tuple(a.bases, -a.value, a.target.planet) < std::tuple(b.bases, -b.value, b.target.planet);
            return std::tuple(a.backlog, -a.rate.total(), a.target.planet, a.target.vehicle) <
                   std::tuple(b.backlog, -b.rate.total(), b.target.planet, b.target.vehicle);
        });
        for (const Option& o : options) {
            QueueItem item;
            item.kind = QueueItem::Kind::Vehicle;
            item.design = design;
            if (unit) item.count = batchSize(di, o.rate);
            const Resources cost = economy::itemCost(p_.r, p_.st, p_.id, o.target, item);
            if (!p_.emit(cmd::QueueAdd{o.target, item, -1})) continue;
            budget_ -= min(cost, o.rate);  // what the item takes from that queue this turn
            return true;
        }
        return false;
    }

    // Obsolete designs leave the queues (except a first item already paid
    // into), and units too large for the planet's free cargo space.
    void cleanUp() {
        for (const auto& c : p_.st.colonies) {
            if (!c || !p_.controlsColony(*c, Minister::ShipConstruction)) continue;
            clean({c->planet, {}});
        }
        for (const Vehicle& v : p_.st.vehicles)
            if (p_.controlsVehicle(v, Minister::ShipConstruction) && !v.queue.items.empty()) clean({{}, v.id});
    }
    void clean(const cmd::QueueTarget& target) {
        auto queue = [&]() -> const ConstructionQueue* {
            if (target.vehicle.valid()) {
                const Vehicle* v = p_.st.vehicle(target.vehicle);
                return v ? &v->queue : nullptr;
            }
            const Colony* c = p_.st.colony(target.planet);
            return c ? &c->queue : nullptr;
        };
        const ConstructionQueue* q = queue();
        if (!q) return;
        for (size_t i = q->items.size(); i-- > 0;) {
            q = queue();
            if (!q || i >= q->items.size()) continue;
            const QueueItem& item = q->items[i];
            if (item.kind != QueueItem::Kind::Vehicle) continue;
            bool drop = p_.st.design(item.design).obsolete && !(i == 0 && !item.spent.isZero());
            if (!drop && target.planet.valid() && isUnitType(p_.info(item.design).stats.vehicleType)) {
                const Colony* c = p_.st.colony(target.planet);
                const int64_t room = colonyCargoCapacity(p_.r, p_.st, *c) - cargoSpaceUsed(p_.r, p_.st, c->cargo);
                drop = int64_t{p_.r.hull(p_.st.design(item.design).hull).tonnage} * std::max(1, item.count) > room;
            }
            if (drop) p_.emit(cmd::QueueRemove{target, static_cast<uint32_t>(i)});
        }
    }
};

} // namespace

void planFacilities(Planner& p, bool firstPass) {
    std::vector<ObjectId> planets;
    for (const auto& c : p.st.colonies)
        if (c && p.controlsColony(*c, Minister::FacilityConstruction)) planets.push_back(c->planet);
    for (ObjectId planet : planets) {
        const Colony* c = p.st.colony(planet);
        if (!c || !c->queue.items.empty() || static_cast<int>(c->facilities.size()) >= facilitySlots(p.r, p.st, *c)) continue;
        if (c->colonyType.empty())
            if (std::string type = laterType(p, *c); !type.empty()) p.emit(cmd::SetColonyType{planet, std::move(type)});
        c = p.st.colony(planet);
        const ColonyType type = parseColonyType(c->colonyType);
        const std::string_view queueType = type == ColonyType::Count ? displayName(ColonyType::Homeworld) : displayName(type);
        const FacilityQueue* row = p.prof.facilityQueue(p.state, queueType);
        if (!row) continue;
        for (const FacilityEntry& entry : row->entries) {
            const auto facility = bestFacilityFor(p.r, p.emp(), entry.ability);
            if (!facility || blocked(p, *p.st.colony(planet), entry)) continue;
            QueueItem item;
            item.kind = QueueItem::Kind::Facility;
            item.facility = *facility;
            // The queue must accept it (a colony without population cannot build).
            if (!queueItemProblem(p.r, p.st, p.id, {planet, {}}, item).empty()) continue;
            if (p.emit(cmd::QueueAdd{{planet, {}}, item, -1})) break;
        }
    }
    if (!firstPass && p.date % 5 == 0) planUpgrades(p, planets);
}

void planShips(Planner& p) { ShipBuilder(p).run(); }

} // namespace detail

} // namespace opense4::game::ai
