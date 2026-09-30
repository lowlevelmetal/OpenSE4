#include "game/state.hpp"

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

Vehicle& GameState::addVehicle(Vehicle v) {
    v.id = VehicleId{nextVehicleId++};
    // "Automatically use Individual Ministers for newly built vehicles": every
    // new vehicle and launched unit group starts under minister control (spec 02 §10).
    if (v.owner.valid() && v.owner.index() < empires.size() && empires[v.owner.index()].ministersForNewVehicles) v.minister = true;
    vehicles.push_back(std::move(v));  // ids are increasing: stays sorted
    return vehicles.back();
}

Fleet& GameState::addFleet(Fleet f) {
    f.id = FleetId{nextFleetId++};
    fleets.push_back(std::move(f));
    return fleets.back();
}

void GameState::removeDeadVehicles() {
    std::erase_if(vehicles, [](const Vehicle& v) { return v.count <= 0; });
    for (Fleet& f : fleets) {
        std::erase_if(f.members, [this](VehicleId id) { return vehicle(id) == nullptr; });
        if (f.leader.valid() && !vehicle(f.leader)) f.leader = f.members.empty() ? VehicleId{} : f.members.front();
    }
    std::erase_if(fleets, [](const Fleet& f) { return f.members.empty(); });
}

std::vector<const Vehicle*> GameState::vehiclesAt(Location where) const {
    std::vector<const Vehicle*> out;
    for (const Vehicle& v : vehicles)
        if (v.location == where) out.push_back(&v);
    return out;
}

} // namespace opense4::game
