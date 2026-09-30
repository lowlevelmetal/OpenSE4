// Computer player: colony types, facilities, upgrades and vehicle
// construction (spec 05 §7.5 AI_Planet_Types, AI_Construction_Facilities,
// AI_Construction_Vehicles).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <tuple>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;

int sizeRank(std::string_view stellarSize) {
    static constexpr std::array<std::string_view, 5> kSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
    for (size_t i = 0; i < kSizes.size(); ++i)
        if (keysEqual(stellarSize, kSizes[i])) return static_cast<int>(i);
    return 2;
}

int planetRank(const Rules& r, const SpaceObject& planet) {
    const ruleset::PlanetSize* ps = planetSize(r, planet);
    if (!ps) return sizeRank(planet.size);
    if (ps->constructed) return 5;
    return sizeRank(ps->stellarSize);
}

// Row thresholds are ratios (spec 05 §7.5): with 125/100/100 the planet's
// minerals must be at least 1.25 times each of the other values (inferred).
bool valuesFit(const SpaceObject& planet, const std::array<int, 3>& t) {
    size_t dominant = 0;
    for (size_t i = 1; i < 3; ++i)
        if (t[i] > t[dominant]) dominant = i;
    if (t[dominant] <= 0) return true;
    for (size_t o = 0; o < 3; ++o) {
        if (o == dominant || t[o] <= 0) continue;
        if (t[o] == t[dominant]) continue;
        if (int64_t{planet.value[dominant]} * t[o] < int64_t{planet.value[o]} * t[dominant]) return false;
    }
    return true;
}

// The facility-queue name for a colony: its own type when the tables know
// it, else the closest by first word ("Mining" -> "Mining Colony").
std::string queueTypeFor(const Planner& p, const Colony& c) {
    if (c.homeworld) return "Homeworld";
    for (const auto& q : p.prof.facilities)
        if (keysEqual(q.queueType, c.colonyType)) return q.queueType;
    const std::string mine = datafile::normalizeKey(c.colonyType);
    const std::string firstWord = mine.substr(0, mine.find(' '));
    if (!firstWord.empty())
        for (const auto& q : p.prof.facilities) {
            const std::string theirs = datafile::normalizeKey(q.queueType);
            if (theirs.starts_with(firstWord) || mine.starts_with(theirs.substr(0, theirs.find(' ')))) return q.queueType;
        }
    for (const auto& q : p.prof.facilities)
        if (keysEqual(q.queueType, "Mining Colony")) return q.queueType;
    for (const auto& q : p.prof.facilities)
        if (!keysEqual(q.queueType, "Homeworld")) return q.queueType;
    return "Homeworld";
}

bool knownColonyType(const Planner& p, std::string_view type) {
    if (type.empty()) return false;
    for (const auto& q : p.prof.facilities)
        if (keysEqual(q.queueType, type)) return true;
    for (const auto& row : p.prof.planetTypes)
        if (keysEqual(row.type, type)) return true;
    return false;
}

int facilitiesWith(const Rules& r, const Colony& c, std::string_view ability, bool includeQueued) {
    int n = 0;
    for (uint32_t f : c.facilities) n += facilityHas(r, f, ability);
    if (includeQueued)
        for (const QueueItem& q : c.queue.items) n += q.kind == QueueItem::Kind::Facility && facilityHas(r, q.facility, ability);
    return n;
}

bool systemHas(const Planner& p, SystemId sys, std::string_view ability) {
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id && p.st.galaxy.object(c->planet).system == sys && facilitiesWith(p.r, *c, ability, true) > 0) return true;
    return false;
}

void planFacilities(Planner& p, ObjectId planet) {
    const Colony* c = p.st.colony(planet);
    if (!c || c->totalPopulation() == 0) return;
    if (p.mode == Mode::Minimal && !c->queue.items.empty()) return;
    for (const QueueItem& q : c->queue.items)
        if (q.kind != QueueItem::Kind::Vehicle) return;  // one facility job at a time
    const cmd::QueueTarget target{planet, {}};
    const SystemId sys = p.st.galaxy.object(planet).system;

    const int slots = facilitySlots(p.r, p.st, *c);
    if (static_cast<int>(c->facilities.size()) < slots)
        if (const FacilityQueue* fq = p.prof.facilityQueue(p.state, queueTypeFor(p, *c))) {
            for (const FacilityEntry& entry : fq->entries) {
                c = p.st.colony(planet);
                if (facilitiesWith(p.r, *c, entry.ability, true) >= entry.amount) continue;
                if (systemWideAbility(entry.ability) && systemHas(p, sys, entry.ability)) continue;
                const auto f = bestFacilityFor(p.r, p.emp(), entry.ability);
                if (!f) continue;  // not researched yet: skip the entry (rule 1)
                QueueItem item;
                item.kind = QueueItem::Kind::Facility;
                item.facility = *f;
                if (p.emit(cmd::QueueAdd{target, item, -1})) return;
            }
        }

    // Upgrades: one family at a time, oldest first.
    c = p.st.colony(planet);
    for (uint32_t f : c->facilities) {
        const auto latest = p.r.latestFacilityOfFamily(p.emp(), p.r.facility(f).family);
        if (!latest || p.r.facility(*latest).romanNumeral <= p.r.facility(f).romanNumeral || p.r.facility(f).family == 0) continue;
        QueueItem item;
        item.kind = QueueItem::Kind::Upgrade;
        item.facility = f;
        if (p.emit(cmd::QueueAdd{target, item, -1})) return;
    }
}

// A resource the empire is running out of: little in store and produced at
// under a third of the rate of the best-produced one (inferred).
std::optional<Resource> shortage(const Planner& p) {
    const EconomyReport& eco = p.emp().economy;
    const Resources& stock = p.emp().stockpile;
    int64_t most = 0;
    for (Resource r : kResources) most = std::max(most, eco.colonies[r]);
    if (most <= 0) return std::nullopt;
    std::optional<Resource> worst;
    for (Resource r : kResources) {
        const int64_t cap = eco.storageCap[r];
        const bool low = cap > 0 ? stock[r] * 10 < cap : stock[r] < 5000;
        if (!low || eco.colonies[r] * 3 >= most) continue;
        if (!worst || stock[r] + eco.colonies[r] * 4 < stock[*worst] + eco.colonies[*worst] * 4) worst = r;
    }
    return worst;
}

// One extra generator of a scarce resource per turn, on the colony whose
// planet is richest in it and has a free slot (inferred: the classic tables
// do not balance resources on their own).
void relieveShortage(Planner& p, const std::vector<ObjectId>& planets) {
    const auto scarce = shortage(p);
    if (!scarce) return;
    const AbilityKind kind = *scarce == Resource::Minerals   ? AbilityKind::ResourceGenMinerals
                             : *scarce == Resource::Organics ? AbilityKind::ResourceGenOrganics
                                                             : AbilityKind::ResourceGenRadioactives;
    const auto facility = p.r.bestFacilityWith(p.emp(), kind);
    if (!facility) return;
    std::optional<ObjectId> best;
    int bestValue = 0;
    for (ObjectId o : planets) {
        const Colony* c = p.st.colony(o);
        if (!c || c->totalPopulation() == 0) continue;
        int queued = 0;
        for (const QueueItem& q : c->queue.items) queued += q.kind != QueueItem::Kind::Vehicle;
        if (queued > 0 || static_cast<int>(c->facilities.size()) >= facilitySlots(p.r, p.st, *c)) continue;
        const int value = p.st.galaxy.object(o).value[static_cast<size_t>(*scarce)];
        if (!best || value > bestValue) {
            best = o;
            bestValue = value;
        }
    }
    if (!best) return;
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = *facility;
    p.emit(cmd::QueueAdd{{*best, {}}, item, -1});
}

// Income outgrowing the yards: resources pile up against the storage cap
// while every yard is busy. Add one space yard at a time, on the most
// populous colony without one that has a free slot (inferred).
void expandYards(Planner& p, const std::vector<ObjectId>& planets) {
    const EconomyReport& eco = p.emp().economy;
    const int64_t cap = eco.storageCap.total();
    if (cap <= 0 || p.emp().stockpile.total() * 100 < cap * 70) return;
    // More yards only help while we may still add ships under the maintenance cap.
    const int64_t income = eco.colonies.total();
    if (income <= 0 || eco.maintenance.total() * 100 >= income * p.prof.settings.maxMaintenancePercent) return;
    int yards = 0, populated = 0;
    for (ObjectId o : planets)
        if (const Colony* c = p.st.colony(o); c && c->totalPopulation() > 0) {
            ++populated;
            yards += colonyHasSpaceYard(p.r, *c);
        }
    if (yards >= std::max(2, populated / 3)) return;
    const auto yard = p.r.bestFacilityWith(p.emp(), AbilityKind::SpaceYard);
    if (!yard) return;
    std::optional<ObjectId> best;
    int64_t bestPop = 0;
    for (ObjectId o : planets) {
        const Colony* c = p.st.colony(o);
        if (!c || c->totalPopulation() == 0) continue;
        for (const QueueItem& q : c->queue.items)
            if (q.kind == QueueItem::Kind::Facility && hasAbility(p.r.facilityAbilities(q.facility), AbilityKind::SpaceYard)) return;
        if (colonyHasSpaceYard(p.r, *c)) continue;
        int queued = 0;
        for (const QueueItem& q : c->queue.items) queued += q.kind == QueueItem::Kind::Facility;
        if (static_cast<int>(c->facilities.size()) + queued >= facilitySlots(p.r, p.st, *c)) continue;
        if (!best || c->totalPopulation() > bestPop) {
            best = o;
            bestPop = c->totalPopulation();
        }
    }
    if (!best) return;
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = *yard;
    p.emit(cmd::QueueAdd{{*best, {}}, item, 0});
}

// ---- Vehicles ------------------------------------------------------------------------------

struct Builder {
    Planner& p;
    std::vector<ObjectId> yards;      // colonies with a space yard
    std::vector<ObjectId> colonies;   // populated colonies (units)
    int maxPerYard = 2;
    int64_t budget = 0;
    int64_t spent = 0;
    bool combatAllowed = true;

    int queued(bool units) {
        int n = 0;
        auto count = [&](const ConstructionQueue& q) {
            for (const QueueItem& item : q.items)
                if (item.kind == QueueItem::Kind::Vehicle && isUnitType(p.info(item.design).stats.vehicleType) == units)
                    n += units ? item.count : 1;
        };
        for (const auto& c : p.st.colonies)
            if (c && c->owner == p.id) count(c->queue);
        for (const Vehicle& v : p.st.vehicles)
            if (v.owner == p.id) count(v.queue);
        return n;
    }

    int64_t yardRate(const Colony& c) const {
        const int64_t rate = economy::constructionRate(p.r, p.st, p.id, {c.planet, {}}).total();
        if (rate > 0) return rate;
        Resources yard;  // the economy module has no estimate yet: the yard's own rates
        for (uint32_t f : c.facilities) yard = max(yard, spaceYardRates(p.r.facilityAbilities(f)));
        return yard.total();
    }

    int vehicleItems(const Colony& c) const {
        int n = 0;
        for (const QueueItem& q : c.queue.items) n += q.kind == QueueItem::Kind::Vehicle;
        return n;
    }

    // Existing and queued vehicles of an AI type (units counted one by one).
    int have(std::string_view aiType, bool colonizers) {
        int n = 0;
        auto match = [&](DesignId d) {
            const DesignInfo& di = p.info(d);
            return colonizers ? di.role == Role::Colonizer : keysEqual(di.aiType, aiType);
        };
        for (const Vehicle& v : p.st.vehicles)
            if (v.owner == p.id && match(v.design)) n += std::max(1, v.count);
        for (const auto& c : p.st.colonies) {
            if (!c || c->owner != p.id) continue;
            for (const QueueItem& q : c->queue.items)
                if (q.kind == QueueItem::Kind::Vehicle && match(q.design)) n += q.count;
            for (const UnitStack& u : c->cargo.units)
                if (match(u.design)) n += u.count;
        }
        for (const Vehicle& v : p.st.vehicles) {
            if (v.owner != p.id) continue;
            for (const QueueItem& q : v.queue.items)
                if (q.kind == QueueItem::Kind::Vehicle && match(q.design)) n += q.count;
            for (const UnitStack& u : v.cargo.units)
                if (match(u.design)) n += u.count;
        }
        return n;
    }

    bool queue(DesignId design, int count) {
        const DesignInfo& di = p.info(design);
        const bool unit = isUnitType(di.stats.vehicleType);
        const int64_t cost = di.stats.cost.total() * count;
        if (spent + cost > budget) return false;
        // Player caps count what is already queued too.
        if (unit ? unitCount(p.r, p.st, p.id) + queued(true) + count > p.st.options.maxUnitsPerPlayer
                 : shipCount(p.r, p.st, p.id) + queued(false) + 1 > p.st.options.maxShipsPerPlayer)
            return false;
        const std::vector<ObjectId>& where = unit ? colonies : yards;
        // The yard that finishes soonest: fewest queued items, then the highest rate.
        std::vector<std::tuple<int, int64_t, ObjectId>> order;
        for (ObjectId o : where) {
            const Colony* c = p.st.colony(o);
            if (!c) continue;
            const int items = vehicleItems(*c);
            if (items >= maxPerYard) continue;
            if (unit) {
                const int64_t room = colonyCargoCapacity(p.r, p.st, *c) - cargoSpaceUsed(p.r, p.st, c->cargo);
                if (room < int64_t{p.r.hull(p.st.design(design).hull).tonnage} * count) continue;
            }
            order.emplace_back(items, -yardRate(*c), o);
        }
        std::sort(order.begin(), order.end());
        for (const auto& [items, rate, o] : order) {
            QueueItem item;
            item.kind = QueueItem::Kind::Vehicle;
            item.design = design;
            item.count = count;
            if (p.emit(cmd::QueueAdd{{o, {}}, item, -1})) {
                spent += cost;
                return true;
            }
        }
        return false;
    }
};

// What every queue of the empire still has to pay for.
int64_t committedCost(Planner& p) {
    int64_t total = 0;
    auto add = [&](const ConstructionQueue& q) {
        for (const QueueItem& item : q.items) {
            Resources cost;
            if (item.kind == QueueItem::Kind::Vehicle) cost = p.info(item.design).stats.cost.percent(100 * int64_t{std::max(1, item.count)});
            else cost = Resources::from(p.r.facility(item.facility).cost);
            total += std::max<int64_t>(0, (cost - item.spent).total());
        }
    };
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id) add(c->queue);
    for (const Vehicle& v : p.st.vehicles)
        if (v.owner == p.id) add(v.queue);
    return total;
}

// The colony-ship design for the surface with the best open target.
std::optional<DesignId> colonizerFor(Planner& p, int& openTargets) {
    openTargets = 0;
    std::optional<DesignId> best;
    int64_t bestValue = 0;
    for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
        const auto design = p.bestDesign(colonyTypeName(surface));
        if (!design) continue;
        const auto targets = colonyTargets(p, surface, p.home);
        openTargets += static_cast<int>(targets.size());
        if (!targets.empty() && (!best || targets.front().value > bestValue)) {
            best = design;
            bestValue = targets.front().value;
        }
    }
    return best;
}

void planVehicles(Planner& p) {
    if (p.difficulty == 0 && p.st.turn % 2 == 1) return;  // an easy computer builds at half pace (inferred)
    Builder b{p};
    for (const auto& c : p.st.colonies) {
        if (!c || !p.controlsColony(*c) || c->totalPopulation() == 0) continue;
        b.colonies.push_back(c->planet);
        if (colonyHasSpaceYard(p.r, *c)) b.yards.push_back(c->planet);
    }
    if (b.colonies.empty()) return;
    b.maxPerYard = 1 + std::min(p.difficulty, 2);
    // Spend what we have plus a few turns of income, minus what the queues
    // still owe (inferred budget rule).
    const EconomyReport& eco = p.emp().economy;
    const int64_t income = eco.colonies.total();
    b.budget = p.emp().stockpile.total() + income * 6 - committedCost(p);
    if (income > 0 && eco.maintenance.total() * 100 >= income * p.prof.settings.maxMaintenancePercent) b.combatAllowed = false;

    const int planets = p.colonyCount();
    std::vector<VehicleEntry> entries;
    if (const VehicleQueue* q = p.prof.vehicleQueue(p.state)) entries = q->entries;
    if (entries.empty()) entries = {{"Colonizer", 20, 1}, {"Attack Ship", 10, 2}};

    // Scouts are not in the classic tables; keep two while there is exploring to do (inferred).
    if (!p.neutral && (p.state == AiState::Exploration || p.state == AiState::Infrastructure) && !explorationFrontier(p).empty()) {
        if (auto scout = p.bestDesign("Scout"))
            for (int n = b.have("Scout", false); n < 2 && b.queue(*scout, 1); ++n) {}
    }

    for (int pass = 0; pass < 2; ++pass) {
        bool bought = false;
        for (const VehicleEntry& entry : entries) {
            const bool colonizer = keysEqual(entry.type, "Colonizer");
            std::optional<DesignId> design;
            int openTargets = 0;
            if (colonizer) design = colonizerFor(p, openTargets);
            else design = p.bestDesign(entry.type);
            if (!design) continue;
            const DesignInfo& di = p.info(*design);
            if (p.neutral && di.stats.movement > 0 && di.role != Role::Colonizer && di.role != Role::Unit) continue;
            if (!b.combatAllowed && (di.role == Role::Warship || di.role == Role::Base || di.role == Role::Unit)) continue;
            int have = b.have(entry.type, colonizer);
            for (int guard = 0; guard < 20; ++guard) {
                const bool need = have < entry.mustHave || (entry.planetsPerItem > 0 && planets * 10 / entry.planetsPerItem > have);
                if (!need) break;
                if (colonizer && have >= openTargets) break;  // one ship per known target
                const bool unit = di.role == Role::Unit;
                const int count = unit ? std::clamp(entry.mustHave - have, 1, 5) : 1;
                if (!b.queue(*design, count)) break;
                have += count;
                bought = true;
            }
        }
        if (!bought) break;
    }

    // Resources piling up against the storage limit: keep idle yards busy
    // with warships rather than lose the surplus (inferred).
    const int64_t cap = eco.storageCap.total();
    const int64_t stock = p.emp().stockpile.total();
    const bool rich = cap > 0 ? stock * 100 >= cap * 70 : income > 0 && stock > income * 4;
    if (rich && b.combatAllowed) {
        const auto design = p.bestDesign(p.neutral ? "Defense Base" : "Attack Ship");
        for (ObjectId yard : b.yards) {
            const Colony* c = p.st.colony(yard);
            if (design && c && b.vehicleItems(*c) == 0) b.queue(*design, 1);
        }
    }
}

// Minimal mode: an empty yard keeps building the best existing warship.
void refillEmptyYards(Planner& p) {
    std::optional<DesignId> design = p.bestDesign("Attack Ship");
    if (!design) design = p.bestDesign("Defense Base");
    if (!design) return;
    for (const auto& c : p.st.colonies) {
        if (!c || c->owner != p.id || c->totalPopulation() == 0 || !c->queue.items.empty() || !colonyHasSpaceYard(p.r, *c)) continue;
        QueueItem item;
        item.kind = QueueItem::Kind::Vehicle;
        item.design = *design;
        p.emit(cmd::QueueAdd{{c->planet, {}}, item, -1});
    }
}

void planEmergency(Planner& p) {
    if (p.difficulty < 1) return;
    const bool defending = p.state == AiState::DefendShortTerm || p.state == AiState::DefendLongTerm;
    for (const auto& c : p.st.colonies) {
        if (!c || !p.controlsColony(*c) || !colonyHasSpaceYard(p.r, *c)) continue;
        const ConstructionQueue& q = c->queue;
        const bool threatened = p.threat[p.st.galaxy.object(c->planet).system.index()] > 0;
        const cmd::QueueTarget target{c->planet, {}};
        if (defending && threatened && !q.emergency && q.slowTurns == 0 && !q.items.empty())
            p.emit(cmd::QueueFlags{target, q.onHold, q.repeat, true, q.autoWaypoint});
        else if (q.emergency && (!defending || !threatened))
            p.emit(cmd::QueueFlags{target, q.onHold, q.repeat, false, q.autoWaypoint});
    }
}

} // namespace

void planColonyTypes(Planner& p) {
    if (!p.fullControl() || p.prof.planetTypes.empty()) return;
    std::vector<ObjectId> todo;
    for (const auto& c : p.st.colonies)
        if (c && p.controlsColony(*c) && !c->homeworld && !knownColonyType(p, c->colonyType)) todo.push_back(c->planet);
    const auto& rows = p.prof.planetTypes;
    for (ObjectId planet : todo) {
        const SpaceObject& obj = p.st.galaxy.object(planet);
        const int total = p.colonyCount();
        std::string pick;
        for (size_t i = 0; i < rows.size() && pick.empty(); ++i) {
            const PlanetTypeRow& row = rows[i];
            if (i + 1 < rows.size()) {  // the last row always fits
                if (!(row.states & maskOf(p.state))) continue;
                if (!row.minimumSize.empty() && planetRank(p.r, obj) < sizeRank(row.minimumSize)) continue;
                int inSystem = 0, inEmpire = 0;
                for (const auto& c : p.st.colonies) {
                    if (!c || c->owner != p.id || !keysEqual(c->colonyType, row.type)) continue;
                    ++inEmpire;
                    inSystem += p.st.galaxy.object(c->planet).system == obj.system;
                }
                if (inSystem >= row.maxPerSystem) continue;
                if ((inEmpire + 1) * 100 > row.percentOfColonies * std::max(1, total)) continue;
                if (row.maxInEmpire > 0 && inEmpire >= row.maxInEmpire) continue;
                if (!valuesFit(obj, row.values)) continue;
            }
            pick = row.type;
        }
        if (!pick.empty()) p.emit(cmd::SetColonyType{planet, pick});
    }
}

void planConstruction(Planner& p) {
    std::vector<ObjectId> planets;
    for (const auto& c : p.st.colonies)
        if (c && p.controlsColony(*c)) planets.push_back(c->planet);
    if (p.fullControl() && p.mode != Mode::Minimal) {
        relieveShortage(p, planets);
        expandYards(p, planets);
    }
    for (ObjectId o : planets) planFacilities(p, o);
    if (p.mode == Mode::Minimal) {
        refillEmptyYards(p);
        return;
    }
    if (!p.fullControl()) return;
    planVehicles(p);
    planEmergency(p);
}

} // namespace opense4::game::ai::detail
