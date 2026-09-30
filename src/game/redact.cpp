#include "game/redact.hpp"

#include <algorithm>

namespace opense4::game {

namespace {

bool contains(const std::vector<VehicleId>& sorted, VehicleId id) { return std::binary_search(sorted.begin(), sorted.end(), id); }

bool treatySharesMaps(const GameState& s, EmpireId viewer, EmpireId other) {
    return viewer.valid() && other.valid() && treatySharesSight(s.empire(viewer).relation(other).treaty);
}

} // namespace

GameState redactForEmpire(const GameState& s, EmpireId viewer) {
    GameState v = s;
    const bool spectator = !viewer.valid() || viewer.index() >= s.empires.size();
    const Empire* me = spectator ? nullptr : &s.empire(viewer);

    // Other empires: only what diplomacy and the score screens show.
    for (Empire& e : v.empires) {
        e.passwordHash.clear();
        if (e.id == viewer) continue;
        const bool partner = treatySharesMaps(s, viewer, e.id);
        e.stockpile = {};
        e.economy = {};
        e.research.clear();
        e.intel.clear();
        e.log.clear();
        e.waypoints = {};
        e.systemsToAvoid.clear();
        e.taggedMinefields.clear();
        e.repairPriorities.clear();
        e.aiState = 0;
        e.aiTurnsInState = 0;
        if (!partner) {
            e.knowledge = {};
            e.knowledge.explored.assign(s.galaxy.systems.size(), 0);
            e.knowledge.present.assign(s.galaxy.systems.size(), 0);
            e.knowledge.lastSeen.assign(s.galaxy.systems.size(), 0);
            e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), 0);
            e.knowledge.notes.assign(s.galaxy.systems.size(), {});
            // Tech levels are only known to partners (the Race Report hides them otherwise).
            std::fill(e.techLevels.begin(), e.techLevels.end(), 0);
        }
        if (!s.options.showAllScores && !partner) e.history.clear();
        // Treaties stay (the treaty grid decides what to show); a computer
        // player's anger is visible only toward us (its mood in Empires).
        for (size_t k = 0; k < e.relations.size(); ++k)
            if (spectator || k != viewer.index()) e.relations[k].anger = 0;
    }

    // Vehicles: our own, and foreign ones we see this turn (without their plans).
    std::vector<VehicleId> visible = me ? me->knowledge.visibleVehicles : std::vector<VehicleId>{};
    std::sort(visible.begin(), visible.end());
    std::erase_if(v.vehicles, [&](const Vehicle& x) { return x.owner != viewer && !contains(visible, x.id); });
    for (Vehicle& x : v.vehicles) {
        if (x.owner == viewer) continue;
        x.orders.clear();
        x.repeatOrders = false;
        x.cargo = {};
        x.queue = {};
        x.supply = 0;
        x.movement = 0;
        x.fleet = {};
        x.targetVehicle = {};
        x.targetObject = {};
        x.minister = false;
    }
    std::erase_if(v.fleets, [&](const Fleet& f) { return f.owner != viewer; });

    // Colonies: foreign ones only in systems we have explored; their contents stay hidden.
    for (auto& c : v.colonies) {
        if (!c || c->owner == viewer) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (!me || !me->hasExplored(sys)) {
            c.reset();
            continue;
        }
        c->cargo = {};
        c->queue = {};
        c->facilities.clear();
        c->minister = false;
    }

    // Designs: foreign designs we have not seen keep only their hull.
    std::vector<DesignId> seen = me ? me->knowledge.seenDesigns : std::vector<DesignId>{};
    for (const Vehicle& x : v.vehicles) seen.push_back(x.design);
    std::sort(seen.begin(), seen.end());
    for (Design& d : v.designs) {
        if (d.owner == viewer || std::binary_search(seen.begin(), seen.end(), d.id)) continue;
        d.name = "Unknown design";
        d.designType.clear();
        d.entries.clear();
        d.built = d.lost = d.kills = 0;
    }

    // Messages to or from us; battles we fought; our own pending events.
    std::erase_if(v.messages, [&](const DiplomaticMessage& m) { return m.from != viewer && m.to != viewer; });
    std::erase_if(v.combats, [&](const CombatRecord& c) {
        return std::find(c.participants.begin(), c.participants.end(), viewer) == c.participants.end();
    });
    std::erase_if(v.pendingEvents, [&](const PendingEvent& p) { return p.empire != viewer; });

    // The random stream would let a client predict the next turn.
    v.rng = Rng(0);
    return v;
}

} // namespace opense4::game
