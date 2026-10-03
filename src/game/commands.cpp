#include "game/commands.hpp"

#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/orders.hpp"
#include "game/query.hpp"
#include "game/rules.hpp"
#include "game/scrap.hpp"
#include "game/setup.hpp"
#include "game/sight.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <type_traits>

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

// The requests that name a third empire (spec 05 §3.4, open question 51).
bool needsThirdEmpire(MessageType t) {
    switch (t) {
        case MessageType::RequestStopHostilities:
        case MessageType::RequestBreakTreaty:
        case MessageType::RequestDeclareWar:
        case MessageType::RequestMakePeace:
        case MessageType::RequestSupport:
        case MessageType::RequestAttackEmpire: return true;
        default: return false;
    }
}

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
            // An Attack naming no target and no place is the stored form the
            // computer's ministers give a ship already on its target's sector:
            // it attacks where the group stands (spec 03 §8, spec 05 §7.5).
            if (!o.vehicle.valid() && !o.object.valid() && o.location.system.valid()) return "No target";
            break;
        case OrderKind::Seek:
            if (o.vehicle.valid() || o.object.valid()) break;
            if (!knownSystem(s, o.location.system) || !o.location.sector.valid()) return "Invalid destination";
            break;
        case OrderKind::JoinFleet: {
            const Fleet* f = o.amount >= 0 ? s.fleet(FleetId{static_cast<uint32_t>(o.amount)}) : nullptr;
            if (!f || f->owner != e) return "Not your fleet";
            break;
        }
        case OrderKind::MoveToWaypoint:
            if (o.amount < 0 || o.amount >= static_cast<int>(s.empire(e).waypoints.size())) return "Invalid waypoint";
            break;
        case OrderKind::UseComponent:
        case OrderKind::UseFacility:
            if (o.amount < 0) return "No such position";
            break;
        case OrderKind::ConvertResources:
            if (o.amount < 0 || o.amount > economy::kMaxConversionOrder) return "At most 65,000 per conversion order";
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


    // Ships always; bases when the setting allows; fighter groups yes; drones,
    // satellites and mines never (spec 03 §9, confirmed: binary).
    std::string fleetJoinProblem(const Vehicle& v) const { return game::fleetJoinProblem(r, s, v); }

    // Orders for a fleet, given to the fleet or to any of its members (spec 03
    // §8, §9, §19 Q65, Q76, confirmed: binary): the fleet has no list of its
    // own; every member at its location holds a copy, and each change is
    // applied list by list to those members only. A member away from the
    // fleet's location that is addressed passes the change on to them and
    // keeps its own list as it is. `base` is the list the player changed (the
    // addressed member's, else the fleet's as fleetOrders shows it): orders
    // added after it are appended to each list,
    // expanded once from where the base leaves the fleet; Clear Orders empties
    // each list and switches its Repeat off; Repeat Orders sets each list's
    // flag. The original has no other change to a list; taking an order back
    // or putting one in front makes every list the new one (an OpenSE4 choice,
    // Q76).
    R setFleetOrders(const Fleet& f, const Vehicle* addressed, const std::vector<Order>& given, bool repeat) {
        const std::vector<VehicleId> holders = fleetGroup(s, f);
        if (holders.empty()) return R::fail("The fleet has no member at its location");
        const std::vector<Order> base = addressed ? addressed->orders : fleetOrders(s, f);
        std::vector<Order> full = expandGivenOrders(r, s, orderContextOf(s, f), base, given);
        bool appended = given.size() >= base.size() && std::equal(base.begin(), base.end(), given.begin());
        // Use Component in a turn-based game: each list at the location is cleared first.
        if (appended && clearBeforeUse(full, base.size())) {
            appended = false;
            repeat = false;
        }
        for (VehicleId id : holders) {
            Vehicle& v = *s.vehicle(id);
            if (appended) v.orders.insert(v.orders.end(), full.begin() + static_cast<std::ptrdiff_t>(base.size()), full.end());
            else v.orders = full;
            v.repeatOrders = repeat;
        }
        return {};
    }

    // Turn-based games: Use Component and Use Facility clear the list before
    // they are added (spec 03 §8, §19 Q76, confirmed: binary). `list` is the
    // new list, whose orders from `added` on were just given; when one of
    // them is such an order, only it and what follows it stay. True when the
    // list was cut; the caller then switches Repeat off, as Clear Orders does
    // (inferred).
    bool clearBeforeUse(std::vector<Order>& list, size_t added) const {
        if (s.options.simultaneous) return false;
        for (size_t i = list.size(); i-- > added;)
            if (list[i].kind == OrderKind::UseComponent || list[i].kind == OrderKind::UseFacility) {
                list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    // The orders a colony carries out (spec 02 §5.6, spec 03 §8, §12).
    static bool colonyOrder(OrderKind k) {
        return k == OrderKind::LaunchUnits || k == OrderKind::RecoverUnits || k == OrderKind::UseFacility || k == OrderKind::ConvertResources;
    }

    R operator()(const cmd::SetOrders& c) {
        for (const Order& o : c.orders)
            if (auto p = orderProblem(s, e, o); !p.empty()) return R::fail(p);
        if (c.planet.valid()) {
            Colony* col = ownColony(s, e, c.planet);
            if (!col) return R::fail("Not your planet");
            for (const Order& o : c.orders)
                if (!colonyOrder(o.kind)) return R::fail("Planets cannot carry out that order");
            std::vector<Order> list = c.orders;
            const size_t kept = static_cast<size_t>(std::mismatch(col->orders.begin(), col->orders.end(), list.begin(), list.end()).second - list.begin());
            // Convert Resources is given only to a colony with a converter, as
            // its button is lit (spec 02 §5.6; checked here since commands can
            // come from anywhere, an OpenSE4 choice). Orders already in the
            // list stay even when the converter is gone.
            for (size_t i = kept; i < list.size(); ++i)
                if (list[i].kind == OrderKind::ConvertResources && !economy::colonyConverts(r, s, *col))
                    return R::fail("This colony cannot convert resources");
            const bool cleared = clearBeforeUse(list, kept);
            col->orders = std::move(list);
            col->repeatOrders = c.repeat && !cleared;
            return {};
        }
        for (const Order& o : c.orders)
            if (o.kind == OrderKind::UseFacility || o.kind == OrderKind::ConvertResources) return R::fail("Only colonies carry out that order");
        // The Scrap window's orders come only from its commands (spec 03 §15);
        // a list may keep those it holds already.
        const std::vector<Order>* held = nullptr;
        if (const Fleet* f = c.fleet.valid() ? ownFleet(s, e, c.fleet) : nullptr) held = &fleetOrders(s, *f);
        else if (const Vehicle* v = c.fleet.valid() ? nullptr : ownVehicle(s, e, c.vehicle)) held = &v->orders;
        for (const Order& o : c.orders)
            if (scrapWindowOrder(o.kind) &&
                (!held || std::count(c.orders.begin(), c.orders.end(), o) > std::count(held->begin(), held->end(), o)))
                return R::fail("That order is given only in the Scrap window");
        // Explore, Resupply, Repair and the composite orders are expanded into
        // simple orders as they are given (spec 03 §8, orders.hpp).
        if (c.fleet.valid()) {
            const Fleet* f = ownFleet(s, e, c.fleet);
            if (!f) return R::fail("Not your fleet");
            return setFleetOrders(*f, nullptr, c.orders, c.repeat);
        }
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v) return R::fail("Not your vehicle");
        if (const Fleet* f = v->fleet.valid() ? s.fleet(v->fleet) : nullptr) return setFleetOrders(*f, v, c.orders, c.repeat);
        const size_t kept = static_cast<size_t>(std::mismatch(v->orders.begin(), v->orders.end(), c.orders.begin(), c.orders.end()).second - c.orders.begin());
        v->orders = expandGivenOrders(r, s, orderContextOf(s, *v), v->orders, c.orders);
        const bool cleared = clearBeforeUse(v->orders, kept);
        v->repeatOrders = c.repeat && !cleared;
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
        f.location = where;  // the fleet's own record of where it is (spec 03 §9)
        const FleetId id = s.addFleet(std::move(f)).id;
        // Joining a fleet clears the vehicle's list (spec 03 §9, §19 Q65).
        for (VehicleId v : c.members) {
            Vehicle& member = *s.vehicle(v);
            member.fleet = id;
            member.orders.clear();
            member.repeatOrders = false;
        }
        return {};
    }

    R operator()(const cmd::JoinFleet& c) {
        Fleet* f = ownFleet(s, e, c.fleet);
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!f || !v) return R::fail("Not yours");
        if (v->fleet.valid()) return R::fail("Already in a fleet");
        if (f->location != v->location) return R::fail("Must be in the fleet's sector");
        if (auto why = fleetJoinProblem(*v); !why.empty()) return R::fail(why);
        // Joining clears the vehicle's list: it does not get the orders the
        // fleet already has, only those given after it joined (spec 03 §9, §19 Q65).
        joinFleet(*f, *v);
        return {};
    }

    // Leaving by Fleet Transfer clears the vehicle's list; a fleet left with no
    // member at its location is disbanded, its members losing their orders
    // (spec 03 §9, §19 Q65).
    R operator()(const cmd::LeaveFleet& c) {
        Vehicle* v = ownVehicle(s, e, c.vehicle);
        if (!v || !v->fleet.valid()) return R::fail("Not in a fleet");
        leaveFleet(s, *v);
        return {};
    }

    // Every member leaves ("Remove All") and loses its orders.
    R operator()(const cmd::DisbandFleet& c) {
        if (!ownFleet(s, e, c.fleet)) return R::fail("Not your fleet");
        disbandFleet(s, c.fleet);
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
        if (c.vehicle.valid() && c.moveFirst.system.valid()) return scrapAfterMove(c.vehicle, c.moveFirst);
        if (c.vehicle.valid()) return scrapWindow(c.vehicle, ScrapAction::Scrap);
        Colony* col = ownColony(s, e, c.facilityPlanet);
        if (!col) return R::fail("Not your planet");
        if (c.facilitySlot < 0 || static_cast<size_t>(c.facilitySlot) >= col->facilities.size()) return R::fail("No such facility");
        const uint32_t f = col->facilities[static_cast<size_t>(c.facilitySlot)];
        int pct = static_cast<int>(r.setting("Scrap Facility Percent Returned", 30));
        pct = std::max(pct, reclamationPercentAt(r, s, e, locationOf(s.galaxy, col->planet)));
        // round(cost × % / 100) of each resource (spec 02 §6.6, confirmed: binary).
        emp().stockpile += Resources::from(r.facility(f).cost).percentRounded(pct);
        col->facilities.erase(col->facilities.begin() + c.facilitySlot);
        // Scrap Facilities refreshes the cloak and sensor levels; a colony
        // that can no longer cloak is decloaked as by Decloak (spec 01 §6.9, §14 Q44).
        TurnContext refresh{r, s, {}, {}, {}};
        diplomacy::recalculateColony(refresh, *col, decloakSide());
        // Scrapping the space yard removes vehicles from the queue.
        if (hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard) && !colonyHasSpaceYard(r, *col))
            std::erase_if(col->queue.items, [&](const QueueItem& q) {
                return q.kind == QueueItem::Kind::Vehicle && !isUnitType(r.hull(s.design(q.design).hull).type);
            });
        return {};
    }

    // Mothball and Unmothball (spec 03 §15): Scrap window actions.
    R operator()(const cmd::Mothball& c) { return scrapWindow(c.vehicle, c.mothball ? ScrapAction::Mothball : ScrapAction::Unmothball); }
    R operator()(const cmd::Analyze& c) { return scrapWindow(c.vehicle, ScrapAction::Analyze); }
    R operator()(const cmd::SelfDestruct& c) { return scrapWindow(c.vehicle, ScrapAction::SelfDestruct); }
    R operator()(const cmd::FireOn& c) { return scrapWindow(c.vehicle, ScrapAction::FireOn); }

    // A Scrap window action on one vehicle (spec 03 §15, confirmed: binary):
    // tested now; in a turn-based game carried out at once, the order list
    // untouched; in a simultaneous game the list is cleared, Repeat goes off
    // and the action becomes its only order, carried out (and tested again)
    // at the vehicle's first action during movement.
    R scrapWindow(VehicleId id, ScrapAction a, DesignId design = {}) {
        Vehicle* v = ownVehicle(s, e, id);
        if (!v || v->count <= 0) return R::fail("Not your vehicle");
        if (auto why = scrapActionProblem(r, s, e, *v, a, design); !why.empty()) return R::fail(why);
        if (s.options.simultaneous) {
            Order o{scrapOrderKind(a), v->location};
            o.design = design;
            v->orders = {o};
            v->repeatOrders = false;
            return {};
        }
        TurnContext ctx{r, s, {}, {}, {}};
        carryOutScrapAction(ctx, *v, a, design);
        if (v->count <= 0) {
            s.removeDeadVehicles();
            sight::updateKnowledge(r, s);  // the system's sight is recalculated
        }
        return {};
    }

    // The Scrap minister's Move To and Scrap (spec 05 §7.5 *Scrap*, confirmed:
    // binary): the Move To to the scrap place `to` only when the vehicle can
    // move and stands elsewhere, then the Scrap, put straight onto its own
    // list in either turn style, without the Scrap window's checks (a cloaked
    // vehicle or a fleet member gets them too). Movement makes the Scrap test
    // when it reaches the Scrap, where the vehicle then stands.
    R scrapAfterMove(VehicleId id, Location to) {
        Vehicle* v = ownVehicle(s, e, id);
        if (!v || v->count <= 0) return R::fail("Not your vehicle");
        if (!knownSystem(s, to.system) || !to.sector.valid()) return R::fail("Invalid destination");
        const bool moves = v->location != to && v->status != VehicleStatus::Mothballed && vehicleMaxMovement(r, s, *v) > 0;
        std::vector<Order> list;
        if (moves) list.push_back(Order{OrderKind::MoveTo, to});
        list.push_back(Order{scrapOrderKind(ScrapAction::Scrap), moves ? to : v->location});
        v->orders = std::move(list);
        v->repeatOrders = false;
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
            // An upgrade's count is every lower-level facility of the target's
            // family there now, fixed from here on (spec 02 §6.6, confirmed: binary).
            if (item.kind == QueueItem::Kind::Upgrade) item = economy::upgradeItem(r, *s.colony(c.target.planet), item.facility);
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
            if (q.items[c.index].kind == QueueItem::Kind::Upgrade) return R::fail("An upgrade converts every older facility; its count is fixed");
            q.items[c.index].count = c.count;
            return R{};
        });
    }

    // A queued facility switches to another level of its family in place: its
    // count and what was paid into it stay (spec 02 §6.6, spec 05 §7.5).
    R operator()(const cmd::QueueReplaceFacility& c) {
        if (c.target.vehicle.valid()) return R::fail("Only planets build facilities");
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.index >= q.items.size()) return R::fail("No such item");
            QueueItem& item = q.items[c.index];
            if (item.kind != QueueItem::Kind::Facility) return R::fail("Only a queued facility can be switched");
            if (c.facility >= r.data().facilities.size()) return R::fail("Unknown facility");
            if (!r.facilityAvailable(emp(), c.facility)) return R::fail("Facility not yet researched");
            const int family = r.facility(item.facility).family;
            if (family == 0 || r.facility(c.facility).family != family) return R::fail("Not a facility of the same family");
            item.facility = c.facility;
            return R{};
        });
    }

    R operator()(const cmd::QueueFlags& c) {
        return queueEdit(c.target, [&](ConstructionQueue& q) {
            if (c.autoWaypoint < -1 || c.autoWaypoint >= static_cast<int>(emp().waypoints.size())) return R::fail("Invalid waypoint");
            q.onHold = c.onHold;
            q.repeat = c.repeat;
            // Leaving emergency mode leaves the counter as it is: slow mode lasts
            // as many turns as the emergency ran, none when no turn passed (spec 02 §6.4).
            if (q.emergency && !c.emergency) q.slowTurns = q.emergencyTurns;
            if (!q.emergency && c.emergency && q.slowTurns > 0) return R::fail("The yard is recovering from emergency construction");
            if (!q.emergency && c.emergency) q.emergencyTurns = 0;
            q.emergency = c.emergency;
            q.autoWaypoint = c.autoWaypoint;
            return R{};
        });
    }

    // Retrofit (spec 03 §14, §15, confirmed: binary): a Scrap window action;
    // retrofitProblem makes the checks in order, the first failure cancels it.
    R operator()(const cmd::Retrofit& c) { return scrapWindow(c.vehicle, ScrapAction::Retrofit, c.design); }

    R operator()(const cmd::SetColonyType& c) {
        Colony* col = ownColony(s, e, c.planet);
        if (!col) return R::fail("Not your planet");
        if (col->homeworld) return R::fail("A homeworld's colony type is fixed");
        col->colonyType = c.colonyType;
        std::erase(emp().colonyTypeChoices, c.planet);  // the colonization dialog is answered (spec 03 §8)
        return {};
    }

    R operator()(const cmd::AbandonPlanet& c) {
        Colony* col = ownColony(s, e, c.planet);
        if (!col) return R::fail("Not your planet");
        if (col->totalPopulation() > r.setting("Maximum Population For Abandon Planet Order", 50))
            return R::fail("Too many people live there to abandon it");
        // The people leave and the colony's anger is reset. The colony goes
        // only when no facility is left (the player may scrap them first);
        // otherwise the planet stays ours, empty, with its facilities, and
        // nobody can colonize it (spec 06 §7 Q47, confirmed: binary).
        col->population.clear();
        col->anger = kNewColonyAnger;
        if (col->facilities.empty()) s.colonies[c.planet.index()].reset();
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

    R operator()(const cmd::CloakColony& c) {
        Colony* col = ownColony(s, e, c.planet);
        if (!col) return R::fail("Not your planet");
        if (c.cloak) {
            if (col->cloaked) return R::fail("The colony is already cloaked");
            if (!sight::colonyCanCloak(*col)) return R::fail("No facility of this colony can cloak it");
            col->cloaked = true;
            sight::updateKnowledge(r, s);
            return {};
        }
        if (!col->cloaked) return R::fail("The colony is not cloaked");
        col->cloaked = false;
        // Decloak recalculates sight and runs the first-contact check at once,
        // in the colony's system (spec 05 §3.1); in a simultaneous game for the
        // acting side only (decloakSide).
        TurnContext contact{r, s, {}, {}, {}};
        diplomacy::afterDecloak(contact, s.galaxy.object(c.planet).system, decloakSide());
        return {};
    }

    // Whose side of a first contact a decloak in this command makes: in a
    // simultaneous game the check runs on the player's machine, and the host
    // takes over only the player's side when it reads the orders; the other
    // empire meets the player at the host's next first-contact check (spec 01
    // §6.9, §14 Q44, spec 05 §9.2, confirmed: binary). Our command is carried
    // out on both machines, so it makes only that side on both. In a
    // turn-based game both sides meet at once.
    EmpireId decloakSide() const { return s.options.simultaneous ? e : EmpireId{}; }

    R operator()(const cmd::JettisonCargo& c) {
        Cargo* hold = nullptr;
        if (c.vehicle.valid()) {
            Vehicle* v = ownVehicle(s, e, c.vehicle);
            if (!v || v->count <= 0) return R::fail("Not your vehicle");
            if (isUnitType(vehicleType(r, s, *v))) return R::fail("Unit groups carry no cargo");
            if (v->status == VehicleStatus::Mothballed) return R::fail("A mothballed vehicle cannot jettison cargo");
            hold = &v->cargo;
        } else {
            Colony* col = ownColony(s, e, c.planet);
            if (!col) return R::fail("Not your planet");
            hold = &col->cargo;
        }
        if (c.population.empty() && c.units.empty()) return R::fail("Nothing to jettison");
        // Every entry must be held, in at least the amount named (all named
        // entries of one race or design together).
        std::map<EmpireId, int64_t> races;
        std::map<DesignId, int64_t> designs;
        for (const PopulationGroup& p : c.population) {
            if (p.millions <= 0) return R::fail("Nothing to jettison");
            races[p.race] += p.millions;
        }
        for (const UnitStack& u : c.units) {
            if (u.count <= 0) return R::fail("Nothing to jettison");
            designs[u.design] += u.count;
        }
        for (const auto& [race, n] : races) {
            const auto held = std::find_if(hold->population.begin(), hold->population.end(), [&](const PopulationGroup& p) { return p.race == race; });
            if (held == hold->population.end() || held->millions < n) return R::fail("Not that much population aboard");
        }
        for (const auto& [design, n] : designs)
            if (hold->unitCount(design) < n) return R::fail("Not that many units aboard");
        // Destroyed: nothing goes into space or onto a planet.
        for (const auto& [race, n] : races)
            for (PopulationGroup& p : hold->population)
                if (p.race == race) p.millions -= n;
        for (const auto& [design, n] : designs) {
            for (UnitStack& u : hold->units)
                if (u.design == design) u.count -= static_cast<int>(n);
            s.design(design).lost += static_cast<int>(n);  // spec 04 §15 Number Lost
        }
        std::erase_if(hold->population, [](const PopulationGroup& p) { return p.millions <= 0; });
        std::erase_if(hold->units, [](const UnitStack& u) { return u.count <= 0; });
        return {};
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
        current.everBuilt = false;
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
        // A request about a third empire names one the sender picks from the
        // empires it has met that are still in the game, other than itself and
        // the recipient (spec 05 open question 51, confirmed: binary). Only the
        // messages the player writes go through that picker: a computer
        // player's, and those a human empire's Politics minister writes, are
        // not checked (question 52, confirmed: binary).
        if (needsThirdEmpire(m.type) && emp().kind == PlayerKind::Human && !c.minister) {
            const EmpireId third = m.thirdEmpire;
            if (!third.valid() || third.index() >= s.empires.size() || third == e || third == m.to || !s.empire(third).alive ||
                !emp().relation(third).contact)
                return R::fail("Choose an empire we have met, other than the recipient");
        }
        // The game option limits the message types a player picks; the
        // answer to a request for a gift or tribute (the computer player's,
        // spec 05 §7.4, which never reads the option) is always allowed.
        if (!s.options.allowGifts && (m.type == MessageType::Gift || m.type == MessageType::Tribute)) {
            const auto request = std::find_if(s.messages.begin(), s.messages.end(), [&](const DiplomaticMessage& x) { return x.id == m.inReplyTo; });
            const bool answersRequest = m.inReplyTo.valid() && request != s.messages.end() && request->from == m.to && request->to == e &&
                                        (request->type == MessageType::DemandGift || request->type == MessageType::DemandTribute);
            if (!answersRequest) return R::fail("Gifts are disabled in this game");
        }
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

    // A war decision with nothing said: anger toward the target becomes 100,
    // nothing is declared (spec 05 §7.3-§7.5).
    R operator()(const cmd::DecideWar& c) {
        if (!c.target.valid() || c.target.index() >= s.empires.size() || c.target == e) return R::fail("Invalid empire");
        if (!s.empire(c.target).alive) return R::fail("That empire is gone");
        Relation& rel = emp().relation(c.target);
        if (!rel.contact) return R::fail("No contact with that empire");
        rel.anger = kMaxAnger;
        return {};
    }

    R operator()(const cmd::CarryOutDemand& c) {
        auto it = std::find_if(s.messages.begin(), s.messages.end(), [&](const DiplomaticMessage& m) { return m.id == c.demand; });
        if (it == s.messages.end() || it->to != e || !it->delivered) return R::fail("No such message");
        if (it->type < MessageType::DemandRemoveShips || it->type > MessageType::DemandStopAttacks) return R::fail("Not a demand or request");
        const DiplomaticMessage d = *it;
        Empire& me = emp();
        auto addUnique = [](std::vector<SystemId>& list, SystemId sys) {
            if (sys.valid() && std::find(list.begin(), list.end(), sys) == list.end()) list.push_back(sys);
        };
        const SystemId planetSystem = d.planet.valid() && d.planet.index() < s.galaxy.objects.size() ? s.galaxy.object(d.planet).system : SystemId{};
        // The empire the demand names (a valid other empire), else nothing.
        Relation* named = d.thirdEmpire.valid() && d.thirdEmpire.index() < me.relations.size() && d.thirdEmpire != e ? &me.relation(d.thirdEmpire)
                                                                                                                       : nullptr;
        switch (d.type) {
            case MessageType::DemandRemoveShips:
            case MessageType::DemandRemoveColonies: addUnique(me.aiMemory.avoid, d.system); break;
            case MessageType::DemandLeavePlanet: addUnique(me.aiMemory.avoid, d.system.valid() ? d.system : planetSystem); break;
            case MessageType::RequestBreakTreaty:
                if (named) ++named->queuedBreak;
                break;
            case MessageType::RequestDeclareWar:
            case MessageType::RequestSupport:
                if (named) ++named->queuedWar;
                break;
            case MessageType::RequestMakePeace:
                if (named) ++named->queuedPeace;
                break;
            case MessageType::RequestAttackEmpire: addUnique(me.aiMemory.attackSystems, d.system); break;
            case MessageType::RequestAttackPlanet: addUnique(me.aiMemory.attackSystems, d.system.valid() ? d.system : planetSystem); break;
            // A promise about the empire the demand names, not the requester
            // (spec 05 open question 47). None named: nothing, which has the
            // effect of the original's record of an empire it never uses (spec
            // 05 open question 51; a player cannot send such a request).
            case MessageType::RequestStopHostilities:
                if (named) ++named->promises;
                break;
            case MessageType::DemandStopEspionage:
            case MessageType::DemandStopSabotage: std::erase_if(me.intel, [&](const IntelProjectOrder& o) { return o.target == d.from; }); break;
            default: break;  // stop attacks in a system: nothing
        }
        return {};
    }

    R operator()(const cmd::UseDemandEntry& c) {
        if (!c.about.valid() || c.about.index() >= emp().relations.size() || c.about == e) return R::fail("Invalid empire");
        Relation& rel = emp().relation(c.about);
        int& entries = c.list == cmd::DemandList::War ? rel.queuedWar : c.list == cmd::DemandList::Break ? rel.queuedBreak : rel.queuedPeace;
        if (entries <= 0) return R::fail("No such entry");
        --entries;
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
        if (c.chooseColonyType) emp().chooseColonyType = *c.chooseColonyType;
        return {};
    }
    R operator()(const cmd::SetEmail& c) {
        emp().email = cleanEmail(c.email);
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
            // ASCII letters and digits only, whatever the C locale.
            auto allowed = [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '-';
            };
            for (char ch : *c.style)
                if (!allowed(ch)) return R::fail("Invalid minister style");
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

    // ---- Empire Options and window memories (spec 06 §1.9) -----------------------------------------
    R operator()(const cmd::SetInterfaceOptions& c) {
        const InterfaceOptions& o = c.options;
        if (o.logFilter > uint8_t(LogCategory::Misc) + 1) return R::fail("Unknown log filter");
        if (o.planetsTab > 9 || o.queuesTab > 4 || o.queuesShown > 0x0f) return R::fail("Unknown window choice");
        if (o.facilityMarkers >= (1u << kFacilityMarkerGroups)) return R::fail("Unknown facility markers");
        if (o.logPosition < 0 || o.logScroll < 0) return R::fail("Invalid log position");
        emp().interfaceOptions = o;
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
            if (fleetOrders(s, *f).empty()) return R::fail("The fleet has no orders");
            if (c.enter) return {};
            name = f->name;
            // Like any failed order: every copy of the fleet's orders is cleared.
            for (VehicleId id : fleetGroup(s, *f)) {
                Vehicle& v = *s.vehicle(id);
                v.orders.clear();
                v.repeatOrders = false;
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

    // ---- Reports -------------------------------------------------------------------------------------

    R operator()(const cmd::OpenVehicleReport& c) {
        const Vehicle* v = s.vehicle(c.vehicle);
        if (!v || v->owner == e) return R::fail("Not a foreign vehicle");
        if (emp().kind != PlayerKind::Human) return R::fail("Only a human player opens reports");
        if (!sight::scannerReaches(r, s, e, *v)) return R::fail("Our scanners do not reach it");
        sight::learnFromReport(r, s, e, c.vehicle);
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
OPENSE4_CMD_NAME(OpenVehicleReport)
OPENSE4_CMD_NAME(QueueReplaceFacility)
OPENSE4_CMD_NAME(DecideWar)
OPENSE4_CMD_NAME(SetInterfaceOptions)
OPENSE4_CMD_NAME(CarryOutDemand)
OPENSE4_CMD_NAME(UseDemandEntry)
OPENSE4_CMD_NAME(JettisonCargo)
OPENSE4_CMD_NAME(CloakColony)
OPENSE4_CMD_NAME(Analyze)
OPENSE4_CMD_NAME(SelfDestruct)
OPENSE4_CMD_NAME(FireOn)
OPENSE4_CMD_NAME(SetEmail)
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

std::vector<ObjectId> coloniesNamed(std::span<const Command> commands) {
    std::vector<ObjectId> out;
    auto add = [&](ObjectId planet) {
        if (planet.valid() && std::find(out.begin(), out.end(), planet) == out.end()) out.push_back(planet);
    };
    for (const Command& c : commands)
        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, cmd::SetOrders> || std::is_same_v<T, cmd::Rename> || std::is_same_v<T, cmd::SetMinister> ||
                              std::is_same_v<T, cmd::SetColonyType> || std::is_same_v<T, cmd::AbandonPlanet> ||
                              std::is_same_v<T, cmd::JettisonCargo> || std::is_same_v<T, cmd::CloakColony>)
                    add(x.planet);
                else if constexpr (std::is_same_v<T, cmd::Scrap>)
                    add(x.facilityPlanet);
                else if constexpr (std::is_same_v<T, cmd::TransferCargo>) {
                    add(x.fromPlanet);
                    add(x.toPlanet);
                } else if constexpr (requires { x.target.planet; })
                    add(x.target.planet);
            },
            c);
    return out;
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
            // `facility` is the target (spec 02 §6.6). The Upgrades tab offers only
            // researched targets; we check that here too, since commands can come
            // from anywhere (spec 02 §13 Q56).
            if (!col) return "Only planets upgrade facilities";
            if (item.facility >= r.data().facilities.size()) return "Unknown facility";
            if (!r.facilityAvailable(emp, item.facility)) return "Facility not yet researched";
            if (economy::upgradeCount(r, *col, item.facility) == 0) return "Nothing to upgrade here";
            // A queue refuses a second upgrade to the same target (confirmed: binary).
            for (const auto& q : col->queue.items)
                if (q.kind == QueueItem::Kind::Upgrade && q.facility == item.facility) return "That upgrade is already queued here";
            return {};
        }
    }
    return "Unknown item";
}

} // namespace opense4::game
