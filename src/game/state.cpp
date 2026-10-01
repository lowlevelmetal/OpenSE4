#include "game/state.hpp"

#include <tuple>

namespace opense4::game {

std::string_view displayName(OrderKind k) {
    switch (k) {
        case OrderKind::MoveTo: return "Move To";
        case OrderKind::Warp: return "Warp Through";
        case OrderKind::Attack: return "Attack";
        case OrderKind::Resupply: return "Resupply";
        case OrderKind::Repair: return "Repair";
        case OrderKind::Explore: return "Explore";
        case OrderKind::Colonize: return "Colonize";
        case OrderKind::Sentry: return "Sentry";
        case OrderKind::LoadCargo: return "Load Cargo";
        case OrderKind::DropCargo: return "Drop Cargo";
        case OrderKind::LaunchUnits: return "Launch Units";
        case OrderKind::RecoverUnits: return "Recover Units";
        case OrderKind::Cloak: return "Cloak";
        case OrderKind::Decloak: return "Decloak";
        case OrderKind::SweepMines: return "Sweep Mines";
        case OrderKind::UseComponent: return "Use Component";
        case OrderKind::StellarManipulation: return "Stellar Manipulation";
        case OrderKind::MoveToWaypoint: return "Move To Waypoint";
        case OrderKind::SelfDestruct: return "Self-Destruct";
        case OrderKind::Count: break;
    }
    return "?";
}

std::string_view displayName(MessageType t) {
    switch (t) {
        case MessageType::General: return "General Message";
        case MessageType::ProposeTreaty: return "Propose Treaty";
        case MessageType::AcceptTreaty: return "Accept Treaty";
        case MessageType::RefuseTreaty: return "Refuse Treaty";
        case MessageType::CounterTreaty: return "Counter Treaty";
        case MessageType::BreakTreaty: return "Break Treaty";
        case MessageType::DeclareWar: return "Declare War";
        case MessageType::ProposeTrade: return "Propose Trade";
        case MessageType::AcceptTrade: return "Accept Trade";
        case MessageType::RefuseTrade: return "Refuse Trade";
        case MessageType::CounterTrade: return "Counter Trade";
        case MessageType::Gift: return "Gift";
        case MessageType::Tribute: return "Tribute";
        case MessageType::AcceptGift: return "Accept Gift";
        case MessageType::RefuseGift: return "Refuse Gift";
        case MessageType::Surrender: return "Surrender";
        case MessageType::GrantIndependence: return "Grant Independence";
        case MessageType::DemandGift: return "Demand Gift";
        case MessageType::DemandTribute: return "Demand Tribute";
        case MessageType::DemandSurrender: return "Demand Surrender";
        case MessageType::DemandRemoveShips: return "Demand Remove Ships";
        case MessageType::DemandRemoveColonies: return "Demand Remove Colonies";
        case MessageType::DemandLeavePlanet: return "Demand Leave Planet";
        case MessageType::RequestStopHostilities: return "Request Stop Hostilities";
        case MessageType::RequestBreakTreaty: return "Request Break Treaty";
        case MessageType::RequestDeclareWar: return "Request Declare War";
        case MessageType::RequestMakePeace: return "Request Make Peace";
        case MessageType::RequestSupport: return "Request Support";
        case MessageType::RequestAttackEmpire: return "Request Attack Empire";
        case MessageType::RequestAttackPlanet: return "Request Attack Planet";
        case MessageType::DemandStopEspionage: return "Demand Stop Espionage";
        case MessageType::DemandStopSabotage: return "Demand Stop Sabotage";
        case MessageType::DemandStopAttacks: return "Demand Stop Attacks";
        case MessageType::AcceptDemand: return "Accept Demand";
        case MessageType::RefuseDemand: return "Refuse Demand";
        case MessageType::Count: break;
    }
    return "?";
}

void addLog(GameState& s, EmpireId empire, LogCategory category, std::string title, std::string text, std::optional<Location> where,
            std::string picture) {
    if (!empire.valid() || empire.index() >= s.empires.size()) return;
    s.empire(empire).log.push_back(
        LogEntry{s.turn, category, std::move(title), std::move(text), where, std::move(picture)});
}

void addHistory(GameState& s, EmpireId empire, EmpireId about, std::string text, std::optional<Location> where) {
    if (!empire.valid() || empire.index() >= s.empires.size()) return;
    if (about.valid() && about.index() >= s.empires.size()) about = {};
    if (where && !(where->system.valid() && where->system.index() < s.galaxy.systems.size() && where->sector.valid())) where.reset();
    s.empire(empire).historyEvents.push_back(HistoryEntry{s.turn, about, std::move(text), where});
}

namespace {
template <class List>
auto seenLowerBound(List& list, DesignId d) {
    return std::lower_bound(list.begin(), list.end(), d, [](const SeenDesign& x, DesignId id) { return x.design < id; });
}
} // namespace

bool knowsDesign(const Knowledge& k, DesignId d) { return designSeenTurn(k, d).has_value(); }

std::optional<uint32_t> designSeenTurn(const Knowledge& k, DesignId d) {
    const auto it = seenLowerBound(k.seenDesigns, d);
    if (it == k.seenDesigns.end() || it->design != d) return std::nullopt;
    return it->turn;
}

void seeDesign(Knowledge& k, DesignId d, uint32_t turn) {
    if (!d.valid()) return;
    const auto it = seenLowerBound(k.seenDesigns, d);
    if (it != k.seenDesigns.end() && it->design == d) it->turn = std::max(it->turn, turn);
    else k.seenDesigns.insert(it, SeenDesign{d, turn});
}

std::vector<DesignId> seenDesignIds(const Knowledge& k) {
    std::vector<DesignId> out;
    out.reserve(k.seenDesigns.size());
    for (const SeenDesign& x : k.seenDesigns) out.push_back(x.design);
    return out;
}

uint32_t GameState::freeSlot() const {
    std::vector<uint32_t> used;
    used.reserve(vehicles.size() + galaxy.objects.size());
    for (const Vehicle& v : vehicles) used.push_back(v.slot);
    for (const StarSystem& sys : galaxy.systems)
        for (ObjectId o : sys.objects) used.push_back(galaxy.object(o).slot);
    std::sort(used.begin(), used.end());
    uint32_t slot = 0;
    for (uint32_t u : used) {
        if (u > slot) break;
        if (u == slot) ++slot;
    }
    return slot;
}

ObjectId GameState::addObject(SpaceObject obj, SystemId system) {
    obj.id = ObjectId{galaxy.objects.size()};
    obj.system = system;
    obj.slot = freeSlot();
    // The system lists its objects in object order (spec 04 §19.2 Q57).
    std::vector<ObjectId>& list = galaxy.system(system).objects;
    const auto at = std::find_if(list.begin(), list.end(), [&](ObjectId o) { return galaxy.object(o).slot > obj.slot; });
    list.insert(at, obj.id);
    galaxy.objects.push_back(std::move(obj));
    colonies.resize(galaxy.objects.size());
    for (Empire& e : empires) e.knowledge.knownWarpLink.resize(galaxy.objects.size(), options.omnipresent ? 1 : 0);
    return galaxy.objects.back().id;
}

Vehicle& GameState::addVehicle(Vehicle v) {
    v.id = VehicleId{nextVehicleId++};
    // The lowest slot of the object list that no object of any kind holds (spec 03 §6.3 step 5).
    v.slot = freeSlot();
    // "Automatically use Individual Ministers for newly built vehicles": every
    // new vehicle and launched unit group starts under minister control (spec 02 §10).
    if (v.owner.valid() && v.owner.index() < empires.size() && empires[v.owner.index()].ministersForNewVehicles) v.minister = true;
    vehicles.push_back(std::move(v));  // ids are increasing: stays sorted
    return vehicles.back();
}

Fleet& GameState::addFleet(Fleet f) {
    f.id = FleetId{nextFleetId++};
    if (!f.location.system.valid() && !f.members.empty())
        if (const Vehicle* first = vehicle(f.members.front())) f.location = first->location;
    fleets.push_back(std::move(f));
    return fleets.back();
}

void GameState::removeDeadVehicles() {
    std::erase_if(vehicles, [](const Vehicle& v) { return v.count <= 0; });
    tidyFleets();
}

void GameState::tidyFleets() {
    std::vector<FleetId> abandoned;
    for (Fleet& f : fleets) {
        std::erase_if(f.members, [&](VehicleId id) {
            const Vehicle* v = vehicle(id);
            return !v || v->count <= 0 || v->fleet != f.id;
        });
        // A chosen leader that left or was destroyed is no longer chosen (spec 03 §9).
        if (f.leader.valid() && std::find(f.members.begin(), f.members.end(), f.leader) == f.members.end()) f.leader = {};
        if (fleetMembersAt(*this, f).empty()) abandoned.push_back(f.id);
    }
    for (FleetId id : abandoned) disbandFleet(*this, id);
}

std::vector<ObjectRef> objectOrder(const GameState& s) {
    std::vector<ObjectRef> out;
    out.reserve(s.vehicles.size() + s.galaxy.objects.size());
    for (const StarSystem& sys : s.galaxy.systems)
        for (ObjectId o : sys.objects) out.push_back(ObjectRef{o, {}, s.galaxy.object(o).slot});
    for (const Vehicle& v : s.vehicles) out.push_back(ObjectRef{{}, v.id, v.slot});
    // Slots are unique; the rest only keeps the order fixed should two ever meet.
    std::sort(out.begin(), out.end(), [](const ObjectRef& a, const ObjectRef& b) {
        return std::tuple(a.slot, a.vehicle.valid(), a.object.value, a.vehicle.value) < std::tuple(b.slot, b.vehicle.valid(), b.object.value, b.vehicle.value);
    });
    return out;
}

std::vector<VehicleId> vehiclesInObjectOrder(const GameState& s) {
    std::vector<std::pair<uint32_t, VehicleId>> slots;
    slots.reserve(s.vehicles.size());
    for (const Vehicle& v : s.vehicles) slots.emplace_back(v.slot, v.id);
    std::sort(slots.begin(), slots.end());
    std::vector<VehicleId> out;
    out.reserve(slots.size());
    for (const auto& [slot, id] : slots) out.push_back(id);
    return out;
}

// ---- Fleets ----------------------------------------------------------------------------------------

namespace {

bool earlierInObjectOrder(const Vehicle& a, const Vehicle& b) { return std::pair(a.slot, a.id) < std::pair(b.slot, b.id); }

const std::vector<Order>& noOrders() {
    static const std::vector<Order> none;
    return none;
}

} // namespace

const Vehicle* fleetLeader(const GameState& s, const Fleet& f) {
    if (const Vehicle* v = s.vehicle(f.leader); v && v->count > 0 && v->fleet == f.id) return v;
    const Vehicle* first = nullptr;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && v->count > 0 && v->fleet == f.id && (!first || earlierInObjectOrder(*v, *first))) first = v;
    return first;
}

std::vector<VehicleId> fleetMembersAt(const GameState& s, const Fleet& f) {
    std::vector<VehicleId> out;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && v->count > 0 && v->fleet == f.id && v->location == f.location) out.push_back(id);
    return out;
}

std::vector<VehicleId> fleetGroup(const GameState& s, const Fleet& f) {
    std::vector<VehicleId> out = fleetMembersAt(s, f);
    std::erase_if(out, [&](VehicleId id) { return s.vehicle(id)->status == VehicleStatus::Mothballed; });
    return out;
}

bool inFleetGroup(const GameState& s, const Vehicle& v) {
    if (!v.fleet.valid() || v.count <= 0 || v.status == VehicleStatus::Mothballed) return false;
    const Fleet* f = s.fleet(v.fleet);
    return f && v.location == f->location && std::find(f->members.begin(), f->members.end(), v.id) != f->members.end();
}

const Vehicle* fleetOrderHolder(const GameState& s, const Fleet& f) {
    const Vehicle* holder = nullptr;
    for (VehicleId id : fleetGroup(s, f))
        if (const Vehicle* v = s.vehicle(id); !v->orders.empty() && (!holder || earlierInObjectOrder(*v, *holder))) holder = v;
    return holder;
}

const std::vector<Order>& fleetOrders(const GameState& s, const Fleet& f) {
    const Vehicle* holder = fleetOrderHolder(s, f);
    return holder ? holder->orders : noOrders();
}

bool fleetRepeats(const GameState& s, const Fleet& f) {
    if (const Vehicle* holder = fleetOrderHolder(s, f)) return holder->repeatOrders;
    // Nobody has orders: the first member's switch (Repeat is set on every list alike).
    const std::vector<VehicleId> group = fleetGroup(s, f);
    const Vehicle* first = nullptr;
    for (VehicleId id : group)
        if (const Vehicle* v = s.vehicle(id); !first || earlierInObjectOrder(*v, *first)) first = v;
    return first && first->repeatOrders;
}

void fleetMemberMoved(GameState& s, const Vehicle& v) {
    if (!v.fleet.valid()) return;
    if (Fleet* f = s.fleet(v.fleet)) f->location = v.location;
}

void leaveFleet(GameState& s, Vehicle& v) {
    const FleetId id = v.fleet;
    v.fleet = {};
    v.orders.clear();
    v.repeatOrders = false;
    if (Fleet* f = s.fleet(id)) {
        std::erase(f->members, v.id);
        if (f->leader == v.id) f->leader = {};  // the first member leads again (spec 03 §9)
    }
    s.tidyFleets();
}

void disbandFleet(GameState& s, FleetId id) {
    Fleet* f = s.fleet(id);
    if (!f) return;
    for (VehicleId m : f->members)
        if (Vehicle* v = s.vehicle(m); v && v->fleet == id) {
            v->fleet = {};
            v->orders.clear();
            v->repeatOrders = false;
        }
    std::erase_if(s.fleets, [&](const Fleet& x) { return x.id == id; });
}

std::vector<const Vehicle*> GameState::vehiclesAt(Location where) const {
    std::vector<const Vehicle*> out;
    for (const Vehicle& v : vehicles)
        if (v.location == where) out.push_back(&v);
    return out;
}

} // namespace opense4::game
