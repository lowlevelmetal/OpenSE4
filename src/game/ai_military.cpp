// Computer player: fleets, defense and attack (spec 05 §7.5 AI_Fleets), and
// logistics (resupply, repair, retrofit, scrapping, population transport).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <map>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;

Order simple(OrderKind k) {
    Order o;
    o.kind = k;
    return o;
}

Order moveTo(Location where) {
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = where;
    return o;
}

Order attackVehicle(const Vehicle& target) {
    Order o;
    o.kind = OrderKind::Attack;
    o.vehicle = target.id;
    o.location = target.location;
    return o;
}

Order attackPlanet(const GameState& s, ObjectId planet) {
    Order o;
    o.kind = OrderKind::Attack;
    o.object = planet;
    o.location = locationOf(s.galaxy, planet);
    return o;
}

bool lowSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    const int64_t cap = vehicleSupplyCapacity(r, s, v);
    return cap > 0 && v.supply * 100 < cap * 20 && !vehicleHasQuantumReactor(r, s, v);
}

bool badlyDamaged(const Rules& r, const GameState& s, const Vehicle& v) {
    const int structure = vehicleStructure(r, s, v);
    return structure > 0 && int64_t{vehicleDamageTaken(s, v)} * 100 > int64_t{structure} * 30;
}

bool headingFor(const std::vector<Order>& orders, OrderKind k) { return !orders.empty() && orders.front().kind == k; }

// The best known sector of an own colony in a system (the most populous).
Location colonyLocationIn(const Planner& p, SystemId sys) {
    const Colony* best = nullptr;
    for (ObjectId o : p.st.galaxy.system(sys).objects)
        if (const Colony* c = p.st.colony(o); c && c->owner == p.id && (!best || c->totalPopulation() > best->totalPopulation())) best = c;
    if (best) return locationOf(p.st.galaxy, best->planet);
    return {sys, Sector{kSystemCenter, kSystemCenter}};
}

// The strongest visible hostile armed vehicle in a system.
const Vehicle* worstThreatIn(Planner& p, SystemId sys) {
    const Vehicle* best = nullptr;
    int64_t bestValue = 0;
    for (VehicleId id : p.emp().knowledge.visibleVehicles) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->location.system != sys || !p.fightsWith(v->owner)) continue;
        const int64_t value = p.vehicleCombat(*v);
        if (p.info(v->design).attack <= 0) continue;
        if (!best || value > bestValue) {
            best = v;
            bestValue = value;
        }
    }
    return best;
}

struct AttackTarget {
    ObjectId planet;      // a colony, or
    VehicleId vehicle;    // an enemy ship we can see
    SystemId system;
    int64_t defense = 0;

    std::vector<Order> orders(const Planner& p) const {
        if (planet.valid()) return {moveTo(locationOf(p.st.galaxy, planet)), attackPlanet(p.st, planet)};
        const Vehicle* v = p.st.vehicle(vehicle);
        return v ? std::vector<Order>{attackVehicle(*v)} : std::vector<Order>{};
    }
};

// The most rewarding war-enemy colony or visible warship we are strong enough for.
std::optional<AttackTarget> pickAttackTarget(Planner& p, SystemId from, int64_t strength) {
    static constexpr std::array<int64_t, 4> kRatio{200, 150, 120, 110};  // needed strength vs known defense (inferred)
    const int64_t ratio = kRatio[static_cast<size_t>(p.difficulty)];
    const std::vector<int> jumps = p.jumpsFrom(from);
    std::optional<AttackTarget> best;
    int64_t bestScore = 0;
    auto consider = [&](AttackTarget t, int64_t value) {
        const int j = jumps[t.system.index()];
        if (!p.explored(t.system) || j < 0 || !p.mayEnter(t.system)) return;
        if (strength * 100 < t.defense * ratio) return;
        const int64_t score = value - int64_t{j} * 100 - t.defense / 5;
        if (!best || score > bestScore) {
            best = t;
            bestScore = score;
        }
    };
    for (const auto& c : p.st.colonies) {
        if (!c || !p.atWarWith(c->owner)) continue;
        const SystemId sys = p.st.galaxy.object(c->planet).system;
        const int64_t defense = p.threat[sys.index()] + 50 + static_cast<int64_t>(c->facilities.size()) * 10;
        consider({c->planet, {}, sys, defense},
                 c->totalPopulation() / 10 + static_cast<int64_t>(c->facilities.size()) * 50 + (c->homeworld ? 500 : 0));
    }
    // Enemy warships within reach (at most 3 jumps).
    for (VehicleId vid : p.emp().knowledge.visibleVehicles) {
        const Vehicle* v = p.st.vehicle(vid);
        if (!v || !p.atWarWith(v->owner) || p.info(v->design).attack <= 0) continue;
        const SystemId sys = v->location.system;
        if (sys.index() >= jumps.size() || jumps[sys.index()] < 0 || jumps[sys.index()] > 3) continue;
        consider({{}, vid, sys, p.threat[sys.index()]}, p.vehicleCombat(*v) / 2);
    }
    return best;
}

int desiredFleetCount(const Planner& p, int warships) {
    const int planets = p.colonyCount();
    for (const FleetDivision& d : p.prof.fleets.divisions) {
        if (d.maxShips > 0 && warships <= d.maxShips) return d.fleets;
        if (d.maxShips <= 0 && d.maxPlanets > 0 && planets <= d.maxPlanets) return d.fleets;
    }
    return p.prof.fleets.divisions.empty() ? 0 : p.prof.fleets.divisions.back().fleets;
}

uint32_t formationIndex(const Planner& p, std::string_view name) {
    const auto& list = p.r.data().formations;
    for (size_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return static_cast<uint32_t>(i);
    return 0;
}

uint32_t strategyIndex(const Planner& p, std::string_view name) {
    const auto& list = p.emp().strategies;
    for (size_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return static_cast<uint32_t>(i);
    return 0;
}

// Sorts (value, system) pairs: largest value first, then lowest system id.
bool biggestFirst(const std::pair<int64_t, SystemId>& a, const std::pair<int64_t, SystemId>& b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
}

bool isWarship(Planner& p, const Vehicle& v) {
    return v.status == VehicleStatus::Normal && p.info(v.design).role == Role::Warship && p.info(v.design).stats.movement > 0;
}

void formFleets(Planner& p, std::vector<VehicleId>& loose, int warships) {
    const int desired = desiredFleetCount(p, warships);
    int inFleets = warships - static_cast<int>(loose.size());
    const int maxInFleets = warships * p.prof.fleets.percentInFleets / 100;
    int fleets = 0;
    for (const Fleet& f : p.st.fleets) fleets += f.owner == p.id;

    std::map<Location, std::vector<VehicleId>> byPlace;
    for (VehicleId id : loose) {
        const Vehicle* v = p.st.vehicle(id);
        if (v && v->orders.empty()) byPlace[v->location].push_back(id);
    }
    std::vector<VehicleId> joined;
    for (auto& [where, ids] : byPlace) {
        // Join an idle fleet already here.
        FleetId target;
        for (const Fleet& f : p.st.fleets) {
            if (!p.controlsFleet(f) || !f.orders.empty()) continue;
            const Vehicle* leader = p.st.vehicle(f.leader);
            if (leader && leader->location == where) {
                target = f.id;
                break;
            }
        }
        if (!target.valid() && fleets < desired && ids.size() >= 2 && inFleets + 2 <= std::max(2, maxInFleets)) {
            if (p.emit(cmd::CreateFleet{{}, {ids[0], ids[1]}})) {
                target = p.st.fleets.back().id;
                ++fleets;
                inFleets += 2;
                joined.push_back(ids[0]);
                joined.push_back(ids[1]);
                const uint32_t formation = formationIndex(p, p.prof.fleets.defaultFormation);
                const uint32_t strategy = strategyIndex(p, p.prof.fleets.defaultStrategy);
                if (formation != 0 || strategy != 0) p.emit(cmd::SetFleetOptions{target, formation, strategy});
                ids.erase(ids.begin(), ids.begin() + 2);
            }
        }
        if (!target.valid()) continue;
        for (VehicleId id : ids) {
            if (inFleets >= std::max(2, maxInFleets)) break;
            if (p.emit(cmd::JoinFleet{target, id})) {
                ++inFleets;
                joined.push_back(id);
            }
        }
    }
    std::erase_if(loose, [&](VehicleId id) { return std::find(joined.begin(), joined.end(), id) != joined.end(); });
}

bool carriesTroops(const Planner& p, const Vehicle& v, DesignId& troop) {
    for (const UnitStack& u : v.cargo.units)
        if (u.count > 0 && p.r.hull(p.st.design(u.design).hull).type == ruleset::VehicleType::Troop) {
            troop = u.design;
            return true;
        }
    return false;
}

// Troop transports load troops at home and land them on war-enemy colonies
// where our warships already hold the system (inferred: a DropCargo of troop
// units in an enemy colony's sector lands them for ground combat).
void planInvasions(Planner& p) {
    std::vector<ObjectId> targets;
    for (const auto& c : p.st.colonies) {
        if (!c || !p.atWarWith(c->owner)) continue;
        const SystemId sys = p.st.galaxy.object(c->planet).system;
        if (!p.explored(sys) || p.threat[sys.index()] > 0 || !p.mayEnter(sys)) continue;
        bool held = false;
        for (const Vehicle& v : p.st.vehicles)
            held = held || (v.owner == p.id && v.location.system == sys && p.info(v.design).role == Role::Warship);
        if (held) targets.push_back(c->planet);
    }
    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || p.busy.contains(id) || !v->orders.empty() || v->status != VehicleStatus::Normal) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.role != Role::Transport || datafile::normalizeKey(di.aiType).find("troop") == std::string::npos) continue;
        const std::vector<int> jumps = p.jumpsFrom(v->location.system);
        auto nearest = [&](const std::vector<ObjectId>& planets) {
            std::optional<ObjectId> best;
            for (ObjectId o : planets) {
                const int j = jumps[p.st.galaxy.object(o).system.index()];
                if (j >= 0 && (!best || j < jumps[p.st.galaxy.object(*best).system.index()])) best = o;
            }
            return best;
        };
        DesignId troop;
        if (carriesTroops(p, *v, troop)) {
            if (auto target = nearest(targets)) {
                Order drop;
                drop.kind = OrderKind::DropCargo;
                drop.location = locationOf(p.st.galaxy, *target);
                drop.design = troop;
                drop.amount = -1;
                p.setOrders(id, {drop});
            }
            continue;
        }
        auto troopsAt = [&](const Colony& c, DesignId& which) {
            for (const UnitStack& u : c.cargo.units)
                if (u.count > 0 && p.r.hull(p.st.design(u.design).hull).type == ruleset::VehicleType::Troop) {
                    which = u.design;
                    return true;
                }
            return false;
        };
        std::vector<ObjectId> depots;
        for (const auto& c : p.st.colonies)
            if (c && c->owner == p.id && troopsAt(*c, troop)) depots.push_back(c->planet);
        if (auto depot = nearest(depots)) {
            troopsAt(*p.st.colony(*depot), troop);
            Order load;
            load.kind = OrderKind::LoadCargo;
            load.location = locationOf(p.st.galaxy, *depot);
            load.design = troop;
            load.amount = -1;
            p.setOrders(id, {load});
        }
    }
}

} // namespace

void planMilitary(Planner& p) {
    // ---- Ships and fleets under our control.
    std::vector<VehicleId> loose;
    int warships = 0;
    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || !isWarship(p, *v)) continue;
        ++warships;
        if (!v->fleet.valid() && !p.busy.contains(id)) loose.push_back(id);
    }

    const bool defending = p.state == AiState::DefendShortTerm || p.state == AiState::DefendLongTerm || p.state == AiState::PrepareForDefense;
    const bool attacking = p.state == AiState::Attack || p.state == AiState::Incursion || p.state == AiState::SecureHoldings;
    const int dontUse = p.difficulty >= 2 ? p.prof.fleets.dontUseForTurns / 2 : p.prof.fleets.dontUseForTurns;
    const bool useFleets = p.fullControl() && static_cast<int>(p.st.turn) >= dontUse;
    if (useFleets) formFleets(p, loose, warships);

    // ---- Systems that need defending, worst first.
    std::vector<std::pair<int64_t, SystemId>> threatened;
    for (size_t i = 0; i < p.threat.size(); ++i)
        if (p.threat[i] > 0 && p.ownSystem[i]) threatened.emplace_back(p.threat[i], SystemId{i});
    std::sort(threatened.begin(), threatened.end(), biggestFirst);
    if (threatened.size() > static_cast<size_t>(std::max(1, p.prof.settings.maxSystemsToDefend)))
        threatened.resize(static_cast<size_t>(std::max(1, p.prof.settings.maxSystemsToDefend)));
    std::vector<int> defenders(threatened.size(), 0);

    // ---- Fleets.
    std::vector<FleetId> fleets;
    for (const Fleet& f : p.st.fleets)
        if (p.controlsFleet(f)) fleets.push_back(f.id);
    // The first fleets (by id) guard; the rest go on the offensive when the state says so.
    const size_t nDefense = defending || p.neutral ? fleets.size() : fleets.size() * static_cast<size_t>(p.prof.fleets.percentForDefense) / 100;

    // Peacetime posts for defense fleets: home first, then the most valuable colony systems.
    std::vector<std::pair<int64_t, SystemId>> posts;
    for (const auto& c : p.st.colonies) {
        if (!c || c->owner != p.id) continue;
        const SystemId sys = p.st.galaxy.object(c->planet).system;
        const int64_t value = (sys == p.home ? 1'000'000'000 : 0) + (colonyHasSpaceYard(p.r, *c) ? 100'000 : 0) + c->totalPopulation();
        auto it = std::find_if(posts.begin(), posts.end(), [&](const auto& x) { return x.second == sys; });
        if (it == posts.end()) posts.emplace_back(value, sys);
        else it->first += value;
    }
    std::sort(posts.begin(), posts.end(), biggestFirst);
    size_t defenseFleetsPlaced = 0;

    for (size_t i = 0; i < fleets.size(); ++i) {
        const Fleet* f = p.st.fleet(fleets[i]);
        if (!f || f->members.empty()) continue;
        const Vehicle* leader = p.st.vehicle(f->leader);
        if (!leader) continue;
        const Location at = leader->location;
        int64_t strength = 0;
        bool needSupply = false, needRepair = false;
        for (VehicleId m : f->members)
            if (const Vehicle* v = p.st.vehicle(m)) {
                strength += p.vehicleCombat(*v);
                needSupply = needSupply || lowSupply(p.r, p.st, *v);
                needRepair = needRepair || badlyDamaged(p.r, p.st, *v);
            }
        if (needRepair || needSupply) {
            const OrderKind k = needRepair ? OrderKind::Repair : OrderKind::Resupply;
            if (!headingFor(f->orders, k)) p.setFleetOrders(f->id, {simple(k)});
            else p.busyFleets.insert(f->id);
            continue;
        }
        const bool isDefense = i < nDefense;
        std::vector<Order> orders;
        if (isDefense || !attacking) {
            // Defend the worst threatened system with the fewest defenders, else a post.
            SystemId guard = p.home;
            if (!posts.empty() && isDefense) guard = posts[defenseFleetsPlaced++ % posts.size()].second;
            if (!threatened.empty()) {
                size_t pick = 0;
                for (size_t t = 1; t < threatened.size(); ++t)
                    if (defenders[t] < defenders[pick]) pick = t;
                guard = threatened[pick].second;
                ++defenders[pick];
            } else if (p.state == AiState::PrepareForAttack && !isDefense) {
                // Gather at our colony nearest the best target.
                if (auto target = pickAttackTarget(p, at.system, strength * 2)) {
                    const SystemId goal = target->system;
                    const std::vector<int> jumps = p.jumpsFrom(goal);
                    int bestJ = -1;
                    for (size_t s = 0; s < p.ownSystem.size(); ++s)
                        if (p.ownSystem[s] && jumps[s] >= 0 && (bestJ < 0 || jumps[s] < bestJ)) {
                            bestJ = jumps[s];
                            guard = SystemId{s};
                        }
                }
            }
            if (!guard.valid()) continue;
            if (at.system == guard) {
                if (const Vehicle* enemy = worstThreatIn(p, guard)) orders.push_back(attackVehicle(*enemy));
                else if (!f->orders.empty() && f->orders.front().kind == OrderKind::MoveTo && f->orders.front().location.system == guard)
                    orders = f->orders;  // still settling into position
            } else {
                orders.push_back(moveTo(colonyLocationIn(p, guard)));
            }
        } else if (auto target = pickAttackTarget(p, at.system, strength)) {
            orders = target->orders(p);
        } else if (at.system != p.home && p.home.valid()) {
            orders.push_back(moveTo(p.homeLocation));
        }
        p.setFleetOrders(f->id, std::move(orders));
    }

    // ---- Warships outside fleets.
    for (VehicleId id : loose) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || p.busy.contains(id)) continue;
        if (headingFor(v->orders, OrderKind::Resupply) || headingFor(v->orders, OrderKind::Repair)) continue;
        std::vector<Order> orders;
        if (const Vehicle* enemy = worstThreatIn(p, v->location.system)) {
            orders.push_back(attackVehicle(*enemy));
        } else if (!threatened.empty()) {
            orders.push_back(moveTo(colonyLocationIn(p, threatened.front().second)));
        } else if (attacking && !useFleets) {
            if (auto target = pickAttackTarget(p, v->location.system, p.vehicleCombat(*v))) orders = target->orders(p);
        } else if (v->location.system != p.home && p.home.valid() && v->orders.empty()) {
            orders.push_back(moveTo(p.homeLocation));  // gather at home to form fleets
        } else {
            continue;  // keep what it is doing
        }
        p.setOrders(id, std::move(orders));
    }

    const bool atWar = attacking || p.state == AiState::PrepareForAttack;
    if (atWar && p.fullControl() && !p.neutral) planInvasions(p);
}

void planLogistics(Planner& p) {
    const bool full = p.fullControl();
    int retrofits = 0;
    const std::vector<ObjectId> frontier = full ? explorationFrontier(p) : std::vector<ObjectId>{};
    int scouts = 0;
    for (VehicleId id : p.ownVehicles())
        if (const Vehicle* v = p.st.vehicle(id); v && p.info(v->design).role == Role::Scout) ++scouts;

    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || v->status != VehicleStatus::Normal || p.busy.contains(id)) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.stats.movement <= 0 || di.role == Role::Unit) continue;
        if (headingFor(v->orders, OrderKind::Resupply) || headingFor(v->orders, OrderKind::Repair)) {
            p.busy.insert(id);
            continue;
        }
        if (badlyDamaged(p.r, p.st, *v)) {
            p.setOrders(id, {simple(OrderKind::Repair)});
            continue;
        }
        if (lowSupply(p.r, p.st, *v)) {
            p.setOrders(id, {simple(OrderKind::Resupply)});
            continue;
        }
        if (!full || !v->orders.empty() || !spaceYardAt(p.r, p.st, p.id, v->location)) continue;

        // A warship idle at a yard: bring an obsolete design up to date.
        const bool fighting = di.role == Role::Warship || di.role == Role::Carrier;
        if (fighting && p.st.design(v->design).obsolete && retrofits < 2 && p.emp().stockpile.total() > 10000) {
            for (DesignId d : p.designsOfType(di.aiType)) {
                if (d == v->design || p.st.design(d).hull != p.st.design(v->design).hull) continue;
                if (p.emit(cmd::Retrofit{id, d})) {
                    ++retrofits;
                    p.busy.insert(id);
                }
                break;
            }
            if (p.busy.contains(id)) continue;
        }
        // Scouts with nothing left to explore are scrapped, keeping one (inferred).
        if (di.role == Role::Scout && frontier.empty() && scouts > 1 && p.st.turn > 30 && p.mode == Mode::Computer) {
            if (p.emit(cmd::Scrap{id, {}, -1})) --scouts;
        }
    }

    // Population transports (spec 02 §10): from crowded colonies (over 1000M)
    // to thin ones (under 500M) that have room.
    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || p.busy.contains(id) || !v->orders.empty() || v->status != VehicleStatus::Normal) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.role != Role::Transport || di.stats.cargoCapacity <= 0 || !v->cargo.empty() || p.neutral) continue;
        if (datafile::normalizeKey(di.aiType).find("troop") != std::string::npos) continue;  // troops are for invasions
        const Colony* from = nullptr;
        const Colony* to = nullptr;
        for (const auto& c : p.st.colonies) {
            if (!c || c->owner != p.id) continue;
            if (c->totalPopulation() > 1000 && (!from || c->totalPopulation() > from->totalPopulation())) from = &*c;
            if (c->totalPopulation() < 500 && c->totalPopulation() < maxPopulation(p.r, p.st, *c) && breathable(p.st, *c) &&
                (!to || c->totalPopulation() < to->totalPopulation()))
                to = &*c;
        }
        if (!from || !to || from == to) break;
        Order load;
        load.kind = OrderKind::LoadCargo;
        load.location = locationOf(p.st.galaxy, from->planet);
        load.amount = -1;
        Order drop;
        drop.kind = OrderKind::DropCargo;
        drop.location = locationOf(p.st.galaxy, to->planet);
        drop.amount = -1;
        p.setOrders(id, {load, drop});
    }
}

} // namespace opense4::game::ai::detail
