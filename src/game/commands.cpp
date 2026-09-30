#include "game/commands.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/orders.hpp"
#include "game/query.hpp"
#include "game/rules.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

namespace opense4::game {

namespace {

using R = CommandResult;

Vehicle* ownVehicle(GameState& s, EmpireId e, VehicleId id) {
    Vehicle* v = s.vehicle(id);
    return v && v->owner == e ? v : nullptr;
}

Fleet* ownFleet(GameState& s, EmpireId e, FleetId id) {
    Fleet* f = s.fleet(id);
    return f && f->owner == e ? f : nullptr;
}

Colony* ownColony(GameState& s, EmpireId e, ObjectId planet) {
    Colony* c = s.colony(planet);
    return c && c->owner == e ? c : nullptr;
}

bool ownDesign(const GameState& s, EmpireId e, DesignId d) { return d.valid() && d.index() < s.designs.size() && s.design(d).owner == e; }

bool knownSystem(const GameState& s, SystemId sys) { return sys.valid() && sys.index() < s.galaxy.systems.size(); }

std::string orderProblem(const GameState& s, EmpireId e, const Order& o) {
    if (o.kind >= OrderKind::Count) return "Unknown order";
    switch (o.kind) {
        case OrderKind::MoveTo:
            if (!knownSystem(s, o.location.system) || !o.location.sector.valid()) return "Invalid destination";
            break;
        case OrderKind::Warp:
            if (!o.object.valid() || o.object.index() >= s.galaxy.objects.size() || s.galaxy.object(o.object).kind != ObjectKind::WarpPoint)
                return "Not a warp point";
            break;
        case OrderKind::Colonize:
            if (!o.object.valid() || o.object.index() >= s.galaxy.objects.size()) return "Invalid planet";
            break;
        case OrderKind::Attack:
            if (!o.vehicle.valid() && !o.object.valid()) return "No target";
            break;
        case OrderKind::MoveToWaypoint:
            if (o.amount < 0 || o.amount >= static_cast<int>(s.empire(e).waypoints.size())) return "Invalid waypoint";
            break;
        case OrderKind::LoadCargo:
        case OrderKind::DropCargo:
        case OrderKind::LaunchUnits:
        case OrderKind::RecoverUnits:
            if (o.location.system.valid() && !knownSystem(s, o.location.system)) return "Invalid location";
            if (o.design.valid() && o.design.index() >= s.designs.size()) return "Invalid unit design";
            break;
        default: break;
    }
    return {};
}

// Moves cargo items between two holds; returns the amount actually moved.
int64_t moveCargo(const Rules& r, const GameState& s, Cargo& from, Cargo& to, int64_t space, DesignId unit, EmpireId race, int64_t amount,
                  bool fromColony) {
    const int64_t popMass = r.setting("Population Mass", 5);
    if (unit.valid()) {
        auto it = std::find_if(from.units.begin(), from.units.end(), [&](const UnitStack& u) { return u.design == unit; });
        if (it == from.units.end()) return 0;
        const int64_t tons = std::max(1, r.hull(s.design(unit).hull).tonnage);
        int64_t n = std::min<int64_t>({amount, it->count, space / tons});
        if (n <= 0) return 0;
        it->count -= static_cast<int>(n);
        if (it->count == 0) from.units.erase(it);
        auto dst = std::find_if(to.units.begin(), to.units.end(), [&](const UnitStack& u) { return u.design == unit; });
        if (dst == to.units.end()) to.units.push_back({unit, static_cast<int>(n)});
        else dst->count += static_cast<int>(n);
        return n;
    }
    auto it = std::find_if(from.population.begin(), from.population.end(), [&](const PopulationGroup& p) { return p.race == race; });
    if (it == from.population.end()) return 0;
    // A colony keeps at least 1M in total.
    const int64_t available = fromColony ? std::min(it->millions, from.totalPopulation() - 1) : it->millions;
    int64_t n = std::min({amount, available, popMass > 0 ? space / popMass : amount});
    if (n <= 0) return 0;
    it->millions -= n;
    if (it->millions == 0) from.population.erase(it);
    auto dst = std::find_if(to.population.begin(), to.population.end(), [&](const PopulationGroup& p) { return p.race == race; });
    if (dst == to.population.end()) to.population.push_back({race, n});
    else dst->millions += n;
    return n;
}

struct Applier {
    const Rules& r;
    GameState& s;
    EmpireId e;

    Empire& emp() { return s.empire(e); }

    // An own space yard in the sector: a planet facility, or a ship's Space
    // Yard component while that ship is not cloaked (spec 03 §14, §15).
    bool yardAt(Location where) const {
        for (ObjectId o : planetsAt(s, where))
            if (const Colony* c = s.colony(o); c && c->owner == e && colonyHasSpaceYard(r, *c)) return true;
        for (const Vehicle& v : s.vehicles)
            if (v.count > 0 && v.owner == e && v.location == where && v.status != VehicleStatus::Cloaked && vehicleHasSpaceYard(r, s, v))
                return true;
        return false;
    }

    // Ships always; bases when the setting allows; fighter groups yes; drones,
    // satellites and mines never (spec 03 §9, confirmed: binary).
    std::string fleetJoinProblem(const Vehicle& v) const {
        switch (vehicleType(r, s, v)) {
            case ruleset::VehicleType::Ship:
            case ruleset::VehicleType::Fighter: return {};
            case ruleset::VehicleType::Base:
                return r.settingFlag("Bases Can Join Fleets", false) ? std::string{} : std::string("Bases cannot join fleets");
            default: return std::format("{} cannot join a fleet", v.name);
        }
    }

    // The chosen leader, else the first member in object order (spec 03 §9).
    const Vehicle* fleetLeaderOf(const Fleet& f) const { return movement::detail::fleetLeader(s, f); }

    R operator()(const cmd::SetOrders& c) {
        for (const Order& o : c.orders)
            if (auto p = orderProblem(s, e, o); !p.empty()) return R::fail(p);
        if (c.planet.valid()) {
            Colony* col = ownColony(s, e, c.planet);
            if (!col) return R::fail("Not your planet");
            for (const Order& o : c.orders)
                if (o.kind != OrderKind::LaunchUnits && o.kind != OrderKind::RecoverUnits) return R::fail("Planets can only launch and recover units");
            col->orders = c.orders;
            return {};
        }
        // Explore, Resupply, Repair and the composite orders are expanded into
        // simple orders as they are given (spec 03 §8, orders.hpp).
        if (c.fleet.valid()) {
            Fleet* f = ownFleet(s, e, c.fleet);
            if (!f) return R::fail("Not your fleet");
            f->orders = expandGivenOrders(r, s, orderContextOf(s, *f), f->orders, c.orders);
            f->repeatOrders = c.repeat;
            return {};
        }
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v) return R::fail("Not your vehicle");
        v->orders = expandGivenOrders(r, s, orderContextOf(s, *v), v->orders, c.orders);
        v->repeatOrders = c.repeat;
        return {};
    }

    R operator()(const cmd::CreateFleet& c) {
        if (c.members.empty()) return R::fail("A fleet needs members");
        const Vehicle* first = ownVehicle(s, e, c.members.front());
        if (!first) return R::fail("Not your vehicle");
        const Location where = first->location;
        for (VehicleId id : c.members) {
            const Vehicle* v = ownVehicle(s, e, id);
            if (!v) return R::fail("Not your vehicle");
            if (v->location != where) return R::fail("Fleet members must share a sector");
            if (v->fleet.valid()) return R::fail(std::format("{} is already in a fleet", v->name));
            if (auto why = fleetJoinProblem(*v); !why.empty()) return R::fail(why);
        }
        Fleet f;
        f.owner = e;
        f.name = c.name.empty() ? std::format("Fleet {}", s.nextFleetId + 1) : c.name;
        f.members = c.members;
        f.leader = c.members.front();
        const FleetId id = s.addFleet(std::move(f)).id;
        for (VehicleId v : c.members) s.vehicle(v)->fleet = id;
        return {};
    }

    R operator()(const cmd::JoinFleet& c) {
        Fleet* f = ownFleet(s, e, c.fleet);
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!f || !v) return R::fail("Not yours");
        if (v->fleet.valid()) return R::fail("Already in a fleet");
        const Vehicle* leader = fleetLeaderOf(*f);
        if (leader && leader->location != v->location) return R::fail("Must be in the fleet's sector");
        if (auto why = fleetJoinProblem(*v); !why.empty()) return R::fail(why);
        f->members.push_back(v->id);
        v->fleet = f->id;
        return {};
    }

    R operator()(const cmd::LeaveFleet& c) {
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v || !v->fleet.valid()) return R::fail("Not in a fleet");
        Fleet* f = s.fleet(v->fleet);
        v->fleet = {};
        if (f) {
            std::erase(f->members, v->id);
            // A chosen leader that leaves is no longer chosen: the first member leads (spec 03 §9).
            if (f->leader == v->id) f->leader = {};
            // Members that leave take the fleet's orders with them.
            if (v->orders.empty()) v->orders = f->orders;
        }
        std::erase_if(s.fleets, [](const Fleet& x) { return x.members.empty(); });
        return {};
    }

    R operator()(const cmd::DisbandFleet& c) {
        Fleet* f = ownFleet(s, e, c.fleet);
        if (!f) return R::fail("Not your fleet");
        for (VehicleId id : f->members)
            if (Vehicle* v = s.vehicle(id)) {
                v->fleet = {};
                if (v->orders.empty()) v->orders = f->orders;
            }
        std::erase_if(s.fleets, [&](const Fleet& x) { return x.id == c.fleet; });
        return {};
    }

    R operator()(const cmd::SetFleetOptions& c) {
        Fleet* f = ownFleet(s, e, c.fleet);
        if (!f) return R::fail("Not your fleet");
        if (c.formation >= std::max<size_t>(1, r.data().formations.size())) return R::fail("Unknown formation");
        if (c.strategy >= std::max<size_t>(1, emp().strategies.size())) return R::fail("Unknown strategy");
        f->formation = c.formation;
        f->strategy = c.strategy;
        return {};
    }

    R operator()(const cmd::SetVehicleStrategy& c) {
        if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
        if (c.strategy >= std::max<size_t>(1, emp().strategies.size())) return R::fail("Unknown strategy");
        s.design(c.design).strategy = c.strategy;
        return {};
    }

    R operator()(const cmd::Rename& c) {
        if (c.name.empty() || c.name.size() > 64) return R::fail("Invalid name");
        if (c.vehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.vehicle);
            if (!v) return R::fail("Not your vehicle");
            v->name = c.name;
        } else if (c.fleet.valid()) {
            Fleet* f = ownFleet(s, e, c.fleet);
            if (!f) return R::fail("Not your fleet");
            f->name = c.name;
        } else if (c.design.valid()) {
            if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
            // A design name differs from every design in the game, exactly (spec 03 §4.1).
            if (s.design(c.design).name != c.name && designNameInUse(s, c.name)) return R::fail("A design with that name exists");
            s.design(c.design).name = c.name;
        } else if (c.planet.valid()) {
            if (!ownColony(s, e, c.planet)) return R::fail("Not your planet");
            s.galaxy.object(c.planet).name = c.name;
        } else {
            return R::fail("Nothing to rename");
        }
        return {};
    }

    R operator()(const cmd::Scrap& c) {
        if (c.vehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.vehicle);
            if (!v) return R::fail("Not your vehicle");
            if (v->status == VehicleStatus::Cloaked) return R::fail("A cloaked vehicle cannot be scrapped");
            if (!yardAt(v->location)) return R::fail("Scrapping needs a space yard in the sector");
            const auto type = vehicleType(r, s, *v);
            if (type == ruleset::VehicleType::Drone || type == ruleset::VehicleType::Mine) return R::fail("Drones and minefields cannot be scrapped");
            // Damage does not lower the value and cargo is lost (spec 03 §15).
            emp().stockpile += scrapRefund(r, s, *v);
            addLog(s, e, LogCategory::Construction, std::format("{} scrapped", v->name), {}, v->location);
            v->count = 0;
            s.removeDeadVehicles();
            return {};
        }
        Colony* col = ownColony(s, e, c.facilityPlanet);
        if (!col) return R::fail("Not your planet");
        if (c.facilitySlot < 0 || static_cast<size_t>(c.facilitySlot) >= col->facilities.size()) return R::fail("No such facility");
        const uint32_t f = col->facilities[static_cast<size_t>(c.facilitySlot)];
        int pct = static_cast<int>(r.setting("Scrap Facility Percent Returned", 30));
        pct = std::max(pct, reclamationPercentAt(r, s, e, locationOf(s.galaxy, col->planet)));
        // round(cost × % / 100) of each resource (spec 02 §6.6, confirmed: binary).
        emp().stockpile += Resources::from(r.facility(f).cost).percentRounded(pct);
        col->facilities.erase(col->facilities.begin() + c.facilitySlot);
        // Scrapping the space yard removes vehicles from the queue.
        if (hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard) && !colonyHasSpaceYard(r, *col))
            std::erase_if(col->queue.items, [&](const QueueItem& q) {
                return q.kind == QueueItem::Kind::Vehicle && !isUnitType(r.hull(s.design(q.design).hull).type);
            });
        return {};
    }

    // Spec 03 §15: mothballing needs a yard, status Normal and no cargo; it
    // leaves no abilities, movement or supply. Unmothballing needs no yard.
    R operator()(const cmd::Mothball& c) {
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v) return R::fail("Not your vehicle");
        if (isUnitType(vehicleType(r, s, *v))) return R::fail("Units cannot be mothballed");
        if (c.mothball) {
            if (v->status == VehicleStatus::Cloaked) return R::fail("A cloaked vehicle cannot be mothballed");
            if (v->status != VehicleStatus::Normal) return R::fail("Already mothballed");
            if (!yardAt(v->location)) return R::fail("Needs a space yard in the sector");
            if (!v->cargo.empty()) return R::fail("Unload the cargo first");
            v->status = VehicleStatus::Mothballed;
            v->orders.clear();
            v->repeatOrders = false;
            v->queue.items.clear();
            v->supply = 0;
            v->movement = 0;
        } else {
            if (v->status != VehicleStatus::Mothballed) return R::fail("Not mothballed");
            // Every resource must be in stock.
            const Resources cost = unmothballCharge(r, s, *v);
            if (!emp().stockpile.covers(cost)) return R::fail("Not enough resources to unmothball");
            emp().stockpile -= cost;
            v->status = VehicleStatus::Normal;
            // Unlimited supply comes back full; others only at a depot, else 0.
            if (vehicleHasUnlimitedSupply(r, s, *v)) v->supply = kUnlimitedSupply;
            else if (movement::resupplyDepotAt(r, s, e, v->location)) v->supply = vehicleSupplyCapacity(r, s, *v);
            else v->supply = 0;
        }
        return {};
    }

    R operator()(const cmd::SetMinister& c) {
        if (c.empireWide) {
            emp().ministerAll = c.on;
        } else if (c.vehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.vehicle);
            if (!v) return R::fail("Not your vehicle");
            v->minister = c.on;
        } else {
            Colony* col = ownColony(s, e, c.planet);
            if (!col) return R::fail("Not your planet");
            col->minister = c.on;
        }
        return {};
    }

    R queueEdit(const cmd::QueueTarget& t, auto&& fn) {
        ConstructionQueue* q = findQueue(s, e, t);
        if (!q) return R::fail("No such queue");
        return fn(*q);
    }

    R operator()(const cmd::QueueAdd& c) {
        if (auto p = queueItemProblem(r, s, e, c.target, c.item); !p.empty()) return R::fail(p);
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            QueueItem item = c.item;
            item.spent = {};
            item.count = std::max(1, item.count);
            if (c.position < 0 || static_cast<size_t>(c.position) >= q.items.size()) q.items.push_back(item);
            else q.items.insert(q.items.begin() + c.position, item);
            return R{};
        });
    }

    R operator()(const cmd::QueueRemove& c) {
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.index >= q.items.size()) return R::fail("No such item");
            q.items.erase(q.items.begin() + c.index);
            return R{};
        });
    }

    R operator()(const cmd::QueueMove& c) {
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.from >= q.items.size() || c.to >= q.items.size()) return R::fail("No such item");
            QueueItem item = q.items[c.from];
            q.items.erase(q.items.begin() + c.from);
            q.items.insert(q.items.begin() + c.to, item);
            return R{};
        });
    }

    R operator()(const cmd::QueueSetCount& c) {
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.index >= q.items.size()) return R::fail("No such item");
            if (c.count < 1) return R::fail("Count must be positive");
            q.items[c.index].count = c.count;
            return R{};
        });
    }

    R operator()(const cmd::QueueFlags& c) {
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.autoWaypoint < -1 || c.autoWaypoint >= static_cast<int>(emp().waypoints.size())) return R::fail("Invalid waypoint");
            q.onHold = c.onHold;
            q.repeat = c.repeat;
            // Leaving emergency mode starts slow mode (spec 02 §6.4).
            if (q.emergency && !c.emergency) q.slowTurns = std::max(1, q.emergencyTurns);
            if (!q.emergency && c.emergency && q.slowTurns > 0) return R::fail("The yard is recovering from emergency construction");
            if (!q.emergency && c.emergency) q.emergencyTurns = 0;
            q.emergency = c.emergency;
            q.autoWaypoint = c.autoWaypoint;
            return R{};
        });
    }

    // Retrofit (spec 03 §14, confirmed: binary): the checks in order, the
    // first failure cancels it.
    R operator()(const cmd::Retrofit& c) {
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v) return R::fail("Not your vehicle");
        if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
        if (v->status == VehicleStatus::Cloaked) return R::fail("A cloaked vehicle cannot be retrofitted");
        const Design& oldD = s.design(v->design);
        const Design& newD = s.design(c.design);
        // Pair each target component with the first unpaired current entry of the
        // same component and mount; unpaired target parts cost Comps %, unpaired
        // current parts Comp Removal %, each truncated. The hull costs nothing.
        std::vector<bool> paired(oldD.entries.size(), false);
        std::vector<int> pairOf(newD.entries.size(), -1);
        Resources cost;
        bool added = false;
        const int64_t addPct = r.setting("Retrofit Cost Percent For Comps", 120);
        const int64_t removePct = r.setting("Retrofit Cost Percent For Comp Removal", 30);
        for (size_t i = 0; i < newD.entries.size(); ++i) {
            for (size_t j = 0; j < oldD.entries.size(); ++j)
                if (!paired[j] && oldD.entries[j] == newD.entries[i]) {
                    paired[j] = true;
                    pairOf[i] = static_cast<int>(j);
                    break;
                }
            if (pairOf[i] >= 0) continue;
            added = true;
            const Resources each = mounted(r, newD.entries[i]).cost;
            for (Resource res : kResources) cost[res] += xmath::pctTrunc(each[res], addPct);
        }
        for (size_t j = 0; j < oldD.entries.size(); ++j) {
            if (paired[j]) continue;
            const Resources each = mounted(r, oldD.entries[j]).cost;
            for (Resource res : kResources) cost[res] += xmath::pctTrunc(each[res], removePct);
        }
        // 1. Identical designs.
        if (cost.total() == 0) return R::fail("The designs are the same");
        // 2. An own space yard in the sector (a ship's only while it is not cloaked).
        if (!yardAt(v->location)) return R::fail("Retrofit needs a space yard in the sector");
        // 3. The hull.
        if (oldD.hull != newD.hull) return R::fail("A retrofit must keep the hull");
        // 4. Cargo.
        if (!v->cargo.empty()) return R::fail("Unload the cargo before a retrofit");
        // 5. Resources in stock.
        if (!emp().stockpile.covers(cost)) return R::fail("Not enough resources for the retrofit");
        // 6. Space yards and colony modules cannot be added.
        auto has = [&](const Design& d, AbilityKind k) {
            return std::any_of(d.entries.begin(), d.entries.end(), [&](const DesignEntry& en) { return hasAbility(r.componentAbilities(en.component), k); });
        };
        auto colonizes = [&](const Design& d) {
            return has(d, AbilityKind::ColonizeRock) || has(d, AbilityKind::ColonizeIce) || has(d, AbilityKind::ColonizeGas);
        };
        if (r.settingFlag("No Retrofit Adding Of Spaceyards", true) && !has(oldD, AbilityKind::SpaceYard) && has(newD, AbilityKind::SpaceYard))
            return R::fail("Space yards cannot be added by retrofit");
        if (r.settingFlag("No Retrofit Adding Of Colony Module", true) && !colonizes(oldD) && colonizes(newD))
            return R::fail("Colony modules cannot be added by retrofit");
        // 7. Only an increase in total cost is limited, compared in floating point.
        const int64_t oldTotal = computeDesignStats(r, nullptr, oldD).cost.total();
        const int64_t newTotal = computeDesignStats(r, nullptr, newD).cost.total();
        const int64_t maxPct = r.setting("Retrofit Max Percent Difference in Cost", 50);
        if (xmath::Ext(newTotal) > xmath::Ext(oldTotal) * xmath::percent(100 + maxPct)) return R::fail("The new design costs too much more");

        // The cost is taken only when a component is added.
        if (added) emp().stockpile -= cost;
        // Paired parts keep their state; added parts start destroyed and must be repaired.
        std::vector<int> damage;
        for (size_t i = 0; i < newD.entries.size(); ++i)
            damage.push_back(pairOf[i] >= 0 && static_cast<size_t>(pairOf[i]) < v->damage.size() ? v->damage[static_cast<size_t>(pairOf[i])]
                             : pairOf[i] >= 0                                                        ? 0
                                                                                                     : entryStructure(r, newD, i));
        const std::string name = newD.name;
        // A design a ship is retrofitted to is no longer a prototype (spec 03 §4.1).
        s.design(c.design).retrofitted = true;
        v->design = c.design;
        v->damage = std::move(damage);
        // Movement and supply recomputed and clamped to the new maxima.
        v->movement = std::min(v->movement, vehicleMaxMovement(r, s, *v));
        if (vehicleHasUnlimitedSupply(r, s, *v)) v->supply = kUnlimitedSupply;
        else v->supply = std::clamp<int64_t>(v->supply, 0, vehicleSupplyCapacity(r, s, *v));
        addLog(s, e, LogCategory::Construction, std::format("{} retrofitted to {}", v->name, name), {}, v->location);
        return {};
    }

    R operator()(const cmd::SetColonyType& c) {
        Colony* col = ownColony(s, e, c.planet);
        if (!col) return R::fail("Not your planet");
        if (col->homeworld) return R::fail("A homeworld's colony type is fixed");
        col->colonyType = c.colonyType;
        return {};
    }

    R operator()(const cmd::AbandonPlanet& c) {
        Colony* col = ownColony(s, e, c.planet);
        if (!col) return R::fail("Not your planet");
        if (col->totalPopulation() > r.setting("Maximum Population For Abandon Planet Order", 50))
            return R::fail("Too many people live there to abandon it");
        s.colonies[c.planet.index()].reset();
        addLog(s, e, LogCategory::Misc, std::format("{} abandoned", s.galaxy.object(c.planet).name), {}, locationOf(s.galaxy, c.planet));
        addHistory(s, e, e, std::format("Abandoned {}", s.galaxy.object(c.planet).name), locationOf(s.galaxy, c.planet));
        return {};
    }

    R operator()(const cmd::TransferCargo& c) {
        Cargo* from = nullptr;
        Cargo* to = nullptr;
        Location a, b;
        bool fromColony = false;
        int64_t space = 0;
        if (c.fromVehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.fromVehicle);
            if (!v) return R::fail("Not your vehicle");
            from = &v->cargo;
            a = v->location;
        } else if (Colony* col = ownColony(s, e, c.fromPlanet)) {
            if (col->plagueLevel > 0) return R::fail("Nothing can be loaded from a planet quarantined by plague");
            from = &col->cargo;
            a = locationOf(s.galaxy, col->planet);
            if (!c.unitDesign.valid()) {
                from = nullptr;  // population lives on the colony itself (below)
                fromColony = true;
            }
        } else {
            return R::fail("No source");
        }
        if (c.toVehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.toVehicle);
            if (!v) return R::fail("Not your vehicle");
            to = &v->cargo;
            b = v->location;
            space = vehicleCargoCapacity(r, s, *v) - cargoSpaceUsed(r, s, v->cargo);
        } else if (Colony* col = ownColony(s, e, c.toPlanet)) {
            b = locationOf(s.galaxy, col->planet);
            if (c.unitDesign.valid()) {
                to = &col->cargo;
                space = colonyCargoCapacity(r, s, *col) - cargoSpaceUsed(r, s, col->cargo);
            } else {
                // Population lands on the colony (bounded by its maximum).
                Cargo landing;
                landing.population = col->population;
                const int64_t room = std::max<int64_t>(0, maxPopulation(r, s, *col) - col->totalPopulation());
                Cargo* src = from;
                if (!src) return R::fail("Population cannot move between colonies directly");
                if (a != b) return R::fail("Both holders must be in the same sector");
                const int64_t moved =
                    moveCargo(r, s, *src, landing, room * r.setting("Population Mass", 5), {}, c.populationRace, c.amount, false);
                col->population = std::move(landing.population);
                return moved > 0 ? R{} : R::fail("Nothing could be moved");
            }
        } else {
            return R::fail("No destination");
        }
        if (a != b) return R::fail("Both holders must be in the same sector");
        if (fromColony) {
            Colony* col = ownColony(s, e, c.fromPlanet);
            Cargo pop;
            pop.population = col->population;
            const int64_t moved = moveCargo(r, s, pop, *to, space, {}, c.populationRace, c.amount, true);
            col->population = std::move(pop.population);
            return moved > 0 ? R{} : R::fail("Nothing could be moved");
        }
        const int64_t moved = moveCargo(r, s, *from, *to, space, c.unitDesign, c.populationRace, c.amount, false);
        return moved > 0 ? R{} : R::fail("Nothing could be moved");
    }

    R operator()(const cmd::CreateDesign& c) {
        Design d = c.design;
        if (d.name.empty()) return R::fail("A design needs a name");
        if (d.hull >= r.data().vehicleSizes.size()) return R::fail("Unknown hull");
        for (const auto& en : d.entries) {
            if (en.component >= r.data().components.size()) return R::fail("Unknown component");
            if (en.mount >= static_cast<int32_t>(r.data().weaponMounts.size())) return R::fail("Unknown mount");
        }
        const DesignStats st = computeDesignStats(r, &emp(), d);
        if (!st.problems.empty()) return R::fail(st.problems.front());
        // A design name differs from every design in the game (spec 03 §4.1). The
        // designer offers only free names; a name another empire took since (two
        // players' orders in one turn, a computer player's name list) gets the
        // first free numeral instead of refusing the design (inferred).
        d.name = uniqueDesignName(s, d.name);
        d.owner = e;
        d.createdTurn = s.turn;
        resetDesignStatistics(d);
        d.obsolete = false;
        if (d.strategy >= std::max<size_t>(1, emp().strategies.size())) d.strategy = 0;
        addDesign(s, std::move(d));
        return {};
    }

    // Edit (spec 03 §4.1, confirmed: binary): only an own design that is still
    // a prototype and in none of the empire's construction queues, changed in
    // place (it may keep its name). The result starts as a prototype that is
    // not obsolete, with no sightings and empty statistics.
    R operator()(const cmd::EditDesign& c) {
        if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
        Design& current = s.design(c.design);
        if (!designIsPrototype(current)) return R::fail("Only a prototype can be edited; copy or upgrade a built design");
        if (designInQueue(s, e, c.design)) return R::fail("The design is in a construction queue");
        for (const Vehicle& v : s.vehicles)
            if (v.count > 0)
                for (const UnitStack& st : groupStacks(v))
                    if (st.design == c.design) return R::fail("Vehicles of this design exist");
        Design d = c.with;
        if (d.name.empty()) return R::fail("A design needs a name");
        if (d.hull >= r.data().vehicleSizes.size()) return R::fail("Unknown hull");
        for (const auto& en : d.entries) {
            if (en.component >= r.data().components.size()) return R::fail("Unknown component");
            if (en.mount >= static_cast<int32_t>(r.data().weaponMounts.size())) return R::fail("Unknown mount");
        }
        const DesignStats st = computeDesignStats(r, &emp(), d);
        if (!st.problems.empty()) return R::fail(st.problems.front());
        if (d.name != current.name && designNameInUse(s, d.name)) return R::fail("A design with that name exists");
        current.name = d.name;
        current.designType = d.designType;
        current.hull = d.hull;
        current.entries = d.entries;
        current.strategy = d.strategy < std::max<size_t>(1, emp().strategies.size()) ? d.strategy : 0;
        current.obsolete = false;
        current.retrofitted = false;
        current.createdTurn = s.turn;
        resetDesignStatistics(current);
        for (Empire& other : s.empires) std::erase_if(other.knowledge.seenDesigns, [&](const SeenDesign& x) { return x.design == c.design; });
        return {};
    }

    R operator()(const cmd::SetDesignObsolete& c) {
        if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
        s.design(c.design).obsolete = c.obsolete;
        return {};
    }

    R operator()(const cmd::DeleteDesign& c) {
        if (!ownDesign(s, e, c.design)) return R::fail("Not your design");
        if (s.design(c.design).built > 0) return R::fail("Built designs can only be made obsolete");
        if (designInQueue(s, e, c.design)) return R::fail("The design is in a queue");
        // Designs are indexed by id; deleting only unlinks it from the empire.
        std::erase(emp().designs, c.design);
        s.design(c.design).obsolete = true;
        return {};
    }

    R operator()(const cmd::SetResearch& c) {
        if (c.queue.size() > 12) return R::fail("At most 12 research projects");
        for (const auto& p : c.queue) {
            if (!p.area.valid() || p.area.index() >= r.data().techAreas.size()) return R::fail("Unknown technology");
            if (!r.techVisible(s, emp(), p.area)) return R::fail(std::format("{} cannot be researched", r.tech(p.area).name));
            if (emp().techLevel(p.area) >= r.tech(p.area).maxLevel) return R::fail(std::format("{} is complete", r.tech(p.area).name));
        }
        // Progress belongs to the area, not the queue slot: keep it for areas still queued.
        // Adding an area that is already queued does nothing (spec 05 §1.4): later repeats are dropped.
        std::vector<ResearchProject> q;
        for (ResearchProject p : c.queue) {
            if (std::any_of(q.begin(), q.end(), [&](const ResearchProject& x) { return x.area == p.area; })) continue;
            p.progress = 0;
            for (const auto& old : emp().research)
                if (old.area == p.area) p.progress = old.progress;
            q.push_back(p);
        }
        emp().research = std::move(q);
        emp().researchEvenly = c.evenly;
        emp().repeatResearch = c.repeat;
        return {};
    }

    R operator()(const cmd::SetIntel& c) {
        if (!s.options.allowIntel && !c.queue.empty()) return R::fail("Intelligence is disabled in this game");
        for (const auto& p : c.queue) {
            if (p.project >= r.data().intelProjects.size()) return R::fail("Unknown project");
            if (!r.meets(emp(), r.data().intelProjects[p.project].requirements)) return R::fail("Project not available");
            if (p.target.valid() && (p.target.index() >= s.empires.size() || p.target == e)) return R::fail("Invalid target");
        }
        std::vector<IntelProjectOrder> q = c.queue;
        for (auto& p : q) {
            p.progress = 0;
            for (const auto& old : emp().intel)
                if (old.project == p.project && old.target == p.target) p.progress = old.progress;
        }
        emp().intel = std::move(q);
        emp().intelEvenly = c.evenly;
        emp().repeatIntel = c.repeat;
        return {};
    }

    R operator()(const cmd::SendMessage& c) {
        DiplomaticMessage m = c.message;
        if (!m.to.valid() || m.to.index() >= s.empires.size() || m.to == e) return R::fail("Invalid recipient");
        if (!emp().relation(m.to).contact) return R::fail("No contact with that empire");
        if (emp().relation(m.to).messageSentThisTurn) return R::fail("Only one message per empire per turn");
        if (!s.options.allowGifts && (m.type == MessageType::Gift || m.type == MessageType::Tribute))
            return R::fail("Gifts are disabled in this game");
        m.id = MessageId{s.nextMessageId++};
        m.from = e;
        m.sentTurn = s.turn;
        m.delivered = m.answered = false;
        emp().relation(m.to).messageSentThisTurn = true;
        s.messages.push_back(std::move(m));
        return {};
    }

    R operator()(const cmd::AnswerMessage& c) {
        auto it = std::find_if(s.messages.begin(), s.messages.end(), [&](const DiplomaticMessage& m) { return m.id == c.message; });
        if (it == s.messages.end() || it->to != e || !it->delivered) return R::fail("No such message");
        if (it->answered) return R::fail("Already answered");
        it->answered = true;
        DiplomaticMessage reply;
        reply.id = MessageId{s.nextMessageId++};
        reply.from = e;
        reply.to = it->from;
        reply.sentTurn = s.turn;
        reply.inReplyTo = it->id;
        reply.text = c.text;
        reply.treaty = it->treaty;
        reply.offer = it->request;  // what they asked us for is what we now give
        reply.request = it->offer;
        switch (it->type) {
            case MessageType::ProposeTreaty:
            case MessageType::CounterTreaty: reply.type = c.accept ? MessageType::AcceptTreaty : MessageType::RefuseTreaty; break;
            case MessageType::ProposeTrade:
            case MessageType::CounterTrade: reply.type = c.accept ? MessageType::AcceptTrade : MessageType::RefuseTrade; break;
            case MessageType::Gift:
            case MessageType::Tribute: reply.type = c.accept ? MessageType::AcceptGift : MessageType::RefuseGift; break;
            default: reply.type = c.accept ? MessageType::AcceptDemand : MessageType::RefuseDemand; break;
        }
        s.messages.push_back(std::move(reply));
        return {};
    }

    R operator()(const cmd::SetWaypoint& c) {
        if (c.slot < 0 || c.slot >= static_cast<int>(emp().waypoints.size())) return R::fail("Invalid slot");
        if (c.waypoint && !knownSystem(s, c.waypoint->location.system)) return R::fail("Invalid location");
        emp().waypoints[static_cast<size_t>(c.slot)] = c.waypoint ? *c.waypoint : Waypoint{};
        if (c.waypoint) emp().waypoints[static_cast<size_t>(c.slot)].set = true;
        return {};
    }

    R operator()(const cmd::SetSystemFlags& c) {
        if (!knownSystem(s, c.system)) return R::fail("Unknown system");
        auto toggle = [&](std::vector<SystemId>& list, bool on) {
            std::erase(list, c.system);
            if (on) list.insert(std::upper_bound(list.begin(), list.end(), c.system), c.system);
        };
        if (c.avoid) toggle(emp().systemsToAvoid, *c.avoid);
        if (c.claim) toggle(emp().claimedSystems, *c.claim);
        return {};
    }

    R operator()(const cmd::SetSystemNote& c) {
        if (!knownSystem(s, c.system)) return R::fail("Unknown system");
        auto& notes = emp().knowledge.notes;
        if (notes.size() < s.galaxy.systems.size()) notes.resize(s.galaxy.systems.size());
        notes[c.system.index()] = c.note;
        return {};
    }

    R operator()(const cmd::TagMinefield& c) {
        auto& list = emp().taggedMinefields;
        std::erase(list, c.location);
        if (c.tagged) list.push_back(c.location);
        return {};
    }

    R operator()(const cmd::SetStrategy& c) {
        auto& list = emp().strategies;
        if (c.index < 0) {
            if (c.remove) return R::fail("Nothing to remove");
            list.push_back(c.strategy);
            return {};
        }
        if (static_cast<size_t>(c.index) >= list.size()) return R::fail("No such strategy");
        if (c.remove) {
            if (list.size() <= 1) return R::fail("The last strategy cannot be removed");
            list.erase(list.begin() + c.index);
            const auto removed = static_cast<uint32_t>(c.index);
            auto fix = [&](uint32_t& st) {
                if (st == removed) st = 0;
                else if (st > removed) --st;
            };
            for (DesignId id : emp().designs) fix(s.design(id).strategy);
            for (Fleet& f : s.fleets)
                if (f.owner == e) fix(f.strategy);
        } else {
            list[static_cast<size_t>(c.index)] = c.strategy;
        }
        return {};
    }

    R operator()(const cmd::SetRepairPriorities& c) {
        emp().repairPriorities = c.priorities;
        return {};
    }
    R operator()(const cmd::SetDesignTypes& c) {
        emp().designTypes = c.designTypes;
        return {};
    }
    R operator()(const cmd::SetColonyTypes& c) {
        emp().colonyTypes = c.colonyTypes;
        return {};
    }
    R operator()(const cmd::SetEmpireOptions& c) {
        if (c.aiMinimalChanges) emp().aiMinimalChanges = *c.aiMinimalChanges;
        if (c.passwordHash) emp().passwordHash = *c.passwordHash;
        return {};
    }

    // ---- Ministers (spec 02 §10, spec 05 §7.1) ----------------------------------------------------

    // The flag of every own vehicle, fleet and colony ("Indiv. Ministers On/Off").
    void setIndividualMinisters(bool on) {
        for (Vehicle& v : s.vehicles)
            if (v.owner == e) v.minister = on;
        for (Fleet& f : s.fleets)
            if (f.owner == e) f.minister = on;
        for (auto& c : s.colonies)
            if (c && c->owner == e) c->minister = on;
    }

    R operator()(const cmd::SetMinisters& c) {
        if (c.areas && (*c.areas & ~kAllMinisters) != 0) return R::fail("Unknown minister");
        if (c.style) {
            // A folder name under Ai/ (the lookup never leaves that folder).
            if (c.style->size() > 64) return R::fail("Minister style name too long");
            for (char ch : *c.style)
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != ' ' && ch != '_' && ch != '-') return R::fail("Invalid minister style");
        }
        Empire& me = emp();
        // Complete AI does all of the bulk buttons at once, then the single fields apply.
        if (c.completeAi) {
            me.ministers = *c.completeAi ? kAllMinisters : 0;
            me.ministersForNewVehicles = *c.completeAi;
            me.ministerAll = *c.completeAi;
            setIndividualMinisters(*c.completeAi);
        }
        if (c.areas) me.ministers = *c.areas;
        if (c.style) me.ministerStyle = *c.style;
        if (c.useRaceStyle) me.useRaceMinisterStyle = *c.useRaceStyle;
        if (c.newVehicles) me.ministersForNewVehicles = *c.newVehicles;
        if (c.individual) setIndividualMinisters(*c.individual);
        return {};
    }

    // ---- Ship Movement and Ship Orders options (spec 03 §6.2, §6.4) ---------------------------------
    R operator()(const cmd::SetEncounterOptions& c) {
        if (c.clearOrdersOnEncounter && *c.clearOrdersOnEncounter > EncounterClear::Any) return R::fail("Unknown option");
        if (c.clearOrdersOnEncounter) emp().clearOrdersOnEncounter = *c.clearOrdersOnEncounter;
        if (c.avoidTaggedMinefields) emp().avoidTaggedMinefields = *c.avoidTaggedMinefields;
        if (c.avoidRestrictedSystems) emp().avoidRestrictedSystems = *c.avoidRestrictedSystems;
        return {};
    }

    // ---- Turn-based games ----------------------------------------------------------------------

    // The Attack Sector answer (spec 03 §6.2). Entering is carried out by the
    // turn-based pipeline (applyLive, turn.hpp), which moves the group on;
    // declining stops the move and the order fails: the list is cleared and
    // Repeat switched off (spec 03 §6.4, §8).
    R operator()(const cmd::EnterSector& c) {
        if (s.options.simultaneous) return R::fail("Only in turn-based games");
        std::string name;
        if (c.fleet.valid()) {
            Fleet* f = ownFleet(s, e, c.fleet);
            if (!f) return R::fail("Not your fleet");
            if (f->orders.empty()) return R::fail("The fleet has no orders");
            if (c.enter) return {};
            name = f->name;
            f->orders.clear();
            f->repeatOrders = false;
            // Like any failed order: the members moving with the fleet lose theirs too.
            const Vehicle* leader = fleetLeaderOf(*f);
            for (VehicleId id : f->members)
                if (Vehicle* v = s.vehicle(id); v && leader && v->location == leader->location) {
                    v->orders.clear();
                    v->repeatOrders = false;
                }
        } else {
            Vehicle* v = ownVehicle(s, e, c.vehicle);
            if (!v) return R::fail("Not your vehicle");
            if (v->orders.empty()) return R::fail("The vehicle has no orders");
            if (c.enter) return {};
            name = v->name;
            v->orders.clear();
            v->repeatOrders = false;
        }
        addLog(s, e, LogCategory::Misc, std::format("{}: orders cancelled", name), "It did not enter the sector with enemy forces.");
        return {};
    }
};

template <class T>
struct NameOf;
#define OPENSE4_CMD_NAME(T) \
    template <>             \
    struct NameOf<cmd::T> { static constexpr std::string_view value = #T; };
OPENSE4_CMD_NAME(SetOrders)
OPENSE4_CMD_NAME(CreateFleet)
OPENSE4_CMD_NAME(JoinFleet)
OPENSE4_CMD_NAME(LeaveFleet)
OPENSE4_CMD_NAME(DisbandFleet)
OPENSE4_CMD_NAME(SetFleetOptions)
OPENSE4_CMD_NAME(SetVehicleStrategy)
OPENSE4_CMD_NAME(Rename)
OPENSE4_CMD_NAME(Scrap)
OPENSE4_CMD_NAME(Mothball)
OPENSE4_CMD_NAME(SetMinister)
OPENSE4_CMD_NAME(QueueAdd)
OPENSE4_CMD_NAME(QueueRemove)
OPENSE4_CMD_NAME(QueueMove)
OPENSE4_CMD_NAME(QueueSetCount)
OPENSE4_CMD_NAME(QueueFlags)
OPENSE4_CMD_NAME(Retrofit)
OPENSE4_CMD_NAME(SetColonyType)
OPENSE4_CMD_NAME(AbandonPlanet)
OPENSE4_CMD_NAME(TransferCargo)
OPENSE4_CMD_NAME(CreateDesign)
OPENSE4_CMD_NAME(SetDesignObsolete)
OPENSE4_CMD_NAME(DeleteDesign)
OPENSE4_CMD_NAME(SetResearch)
OPENSE4_CMD_NAME(SetIntel)
OPENSE4_CMD_NAME(SendMessage)
OPENSE4_CMD_NAME(AnswerMessage)
OPENSE4_CMD_NAME(SetWaypoint)
OPENSE4_CMD_NAME(SetSystemFlags)
OPENSE4_CMD_NAME(SetSystemNote)
OPENSE4_CMD_NAME(TagMinefield)
OPENSE4_CMD_NAME(SetStrategy)
OPENSE4_CMD_NAME(SetRepairPriorities)
OPENSE4_CMD_NAME(SetDesignTypes)
OPENSE4_CMD_NAME(SetColonyTypes)
OPENSE4_CMD_NAME(SetEmpireOptions)
OPENSE4_CMD_NAME(SetMinisters)
OPENSE4_CMD_NAME(SetEncounterOptions)
OPENSE4_CMD_NAME(EnterSector)
OPENSE4_CMD_NAME(EditDesign)
#undef OPENSE4_CMD_NAME

} // namespace

CommandResult apply(const Rules& r, GameState& s, EmpireId empire, const Command& c) {
    if (!empire.valid() || empire.index() >= s.empires.size()) return R::fail("Unknown empire");
    if (!s.empire(empire).alive) return R::fail("That empire has been defeated");
    return std::visit(Applier{r, s, empire}, c);
}

std::string_view commandName(const Command& c) {
    return std::visit([](const auto& x) { return NameOf<std::decay_t<decltype(x)>>::value; }, c);
}

Resources scrapRefund(const Rules& r, const GameState& s, const Vehicle& v) {
    const Resources cost = computeDesignStats(r, nullptr, s.design(v.design)).cost;
    Resources value;
    if (isUnitType(vehicleType(r, s, v))) {
        // A fighter or satellite group: the unit percentage, per unit of each design.
        const int64_t pct = r.setting("Scrap Unit Percent Returned", 30);
        for (const UnitStack& st : groupStacks(v)) {
            const Resources each = st.design == v.design ? cost : computeDesignStats(r, nullptr, s.design(st.design)).cost;
            for (Resource res : kResources) value[res] += xmath::pctRound(each[res], pct) * std::max(1, st.count);
        }
        return value;
    }
    // Ships and bases: the larger of the setting and the owner's best Resource Reclamation here.
    int64_t pct = r.setting("Scrap Ship Percent Returned", 30);
    if (v.owner.valid()) pct = std::max<int64_t>(pct, reclamationPercentAt(r, s, v.owner, v.location));
    for (Resource res : kResources) value[res] = xmath::pctRound(cost[res], pct);
    return value;
}

Resources unmothballCharge(const Rules& r, const GameState& s, const Vehicle& v) {
    const Resources cost = computeDesignStats(r, nullptr, s.design(v.design)).cost;
    Resources out;
    for (Resource res : kResources) out[res] = xmath::pctRound(cost[res], r.setting("UnMothball Ship Percent Cost", 20));
    return out;
}

ConstructionQueue* findQueue(GameState& s, EmpireId empire, const cmd::QueueTarget& t) {
    if (t.vehicle.valid()) {
        Vehicle* v = ownVehicle(s, empire, t.vehicle);
        return v ? &v->queue : nullptr;
    }
    Colony* c = ownColony(s, empire, t.planet);
    return c ? &c->queue : nullptr;
}

std::string queueItemProblem(const Rules& r, const GameState& s, EmpireId empire, const cmd::QueueTarget& t, const QueueItem& item) {
    const Empire& emp = s.empire(empire);
    const Colony* col = nullptr;
    const Vehicle* yardShip = nullptr;
    if (t.vehicle.valid()) {
        yardShip = s.vehicle(t.vehicle);
        if (!yardShip || yardShip->owner != empire) return "Not your vehicle";
        if (!vehicleHasSpaceYard(r, s, *yardShip)) return "This vehicle has no space yard";
        if (yardShip->status == VehicleStatus::Cloaked) return "A cloaked ship cannot build";
    } else {
        col = s.colony(t.planet);
        if (!col || col->owner != empire) return "Not your planet";
        if (col->totalPopulation() == 0) return "A colony without population cannot build";
    }
    switch (item.kind) {
        case QueueItem::Kind::Vehicle: {
            if (!item.design.valid() || item.design.index() >= s.designs.size() || s.design(item.design).owner != empire)
                return "Not your design";
            const Design& d = s.design(item.design);
            const DesignStats st = computeDesignStats(r, &emp, d);
            if (!st.problems.empty()) return st.problems.front();
            const bool unit = isUnitType(st.vehicleType);
            if (!unit && col && !colonyHasSpaceYard(r, *col)) return "Building ships and bases needs a space yard";
            // Ships and bases are capped when built; units only when launched,
            // counting units in space (spec 03 §12, confirmed: binary).
            if (!unit && shipCount(r, s, empire) >= s.options.maxShipsPerPlayer) return "Ship limit reached";
            return {};
        }
        case QueueItem::Kind::Facility: {
            if (!col) return "Only planets build facilities";
            if (item.facility >= r.data().facilities.size()) return "Unknown facility";
            if (!r.facilityAvailable(emp, item.facility)) return "Facility not yet researched";
            const auto ab = r.facilityAbilities(item.facility);
            if (hasAbility(ab, AbilityKind::SpaceYard)) {
                if (colonyHasSpaceYard(r, *col)) return "A planet can have only one space yard";
                for (const auto& q : col->queue.items)
                    if (q.kind == QueueItem::Kind::Facility && hasAbility(r.facilityAbilities(q.facility), AbilityKind::SpaceYard))
                        return "A space yard is already queued here";
            }
            int queued = 0;
            for (const auto& q : col->queue.items) queued += q.kind == QueueItem::Kind::Facility;
            if (static_cast<int>(col->facilities.size()) + queued >= facilitySlots(r, s, *col)) return "No free facility slots";
            if (!breathable(s, *col) && static_cast<int>(col->facilities.size()) + queued >= facilitySlots(r, s, *col))
                return "Domed colony is full";
            return {};
        }
        case QueueItem::Kind::Upgrade: {
            if (!col) return "Only planets upgrade facilities";
            if (item.facility >= r.data().facilities.size()) return "Unknown facility";
            const int family = r.facility(item.facility).family;
            const auto latest = r.latestFacilityOfFamily(emp, family);
            if (!latest) return "Nothing to upgrade to";
            const int newest = r.facility(*latest).romanNumeral;
            for (uint32_t f : col->facilities)
                if (r.facility(f).family == family && r.facility(f).romanNumeral < newest) return {};
            return "No older facilities of that kind here";
        }
    }
    return "Unknown item";
}

} // namespace opense4::game
