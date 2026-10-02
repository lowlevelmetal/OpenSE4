#include "game/redact.hpp"

#include "game/design.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <vector>

namespace opense4::game {

namespace {

bool contains(const std::vector<VehicleId>& sorted, VehicleId id) { return std::binary_search(sorted.begin(), sorted.end(), id); }

bool treatySharesMaps(const GameState& s, EmpireId viewer, EmpireId other) {
    return viewer.valid() && other.valid() && treatySharesSight(s.empire(viewer).relation(other).treaty);
}

} // namespace

GameState redactForEmpire(const Rules& r, const GameState& s, EmpireId viewer) {
    GameState v = s;
    const bool spectator = !viewer.valid() || viewer.index() >= s.empires.size();
    const Empire* me = spectator ? nullptr : &s.empire(viewer);

    // Colonies hidden from us by their cloak (spec 01 §6.9): their planets
    // fail the "seeing the planet" test on the full state. Worked out before
    // anything changes, in planet order.
    std::vector<ObjectId> hidden;
    if (me)
        for (const auto& c : s.colonies)
            if (c && c->owner != viewer && me->hasExplored(s.galaxy.object(c->planet).system) &&
                !sight::canSeePlanet(r, s, viewer, c->planet))
                hidden.push_back(c->planet);

    // Other empires: only what diplomacy and the score screens show.
    for (Empire& e : v.empires) {
        e.passwordHash.clear();
        if (e.id == viewer) continue;
        const bool partner = treatySharesMaps(s, viewer, e.id);
        e.stockpile = {};
        e.economy = {};
        e.researchPool = e.intelPool = 0;
        e.research.clear();
        e.intel.clear();
        e.log.clear();
        e.historyEvents.clear();
        e.waypoints = {};
        e.homeSystem = {};  // where another empire started is not ours to know
        e.homeSector = {};
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
        if (!score::scoreVisible(s, viewer, e.id) && !partner) e.history.clear();
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
        x.minister = false;
    }
    std::erase_if(v.fleets, [&](const Fleet& f) { return f.owner != viewer; });

    // A hidden colony is not in our view at all: no colony record, and its
    // planet leaves its system's object list (as an object removed by stellar
    // manipulation does), so nothing draws, lists or selects it. Its object
    // stays, so every reference to it remains valid.
    for (ObjectId planet : hidden) {
        v.colonies[planet.index()].reset();
        std::erase(v.galaxy.system(v.galaxy.object(planet).system).objects, planet);
    }

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
        c->orders.clear();
        if (c->invader != viewer) c->landedTroops.clear();   // another empire's landed troops stay hidden
    }

    // Designs: foreign designs we have not seen keep only their hull. No
    // foreign design shows its statistics (built, lost, enemy tonnage
    // destroyed): they are the owner's records (confirmed: binary, spec 04 §15).
    std::vector<DesignId> seen = me ? seenDesignIds(me->knowledge) : std::vector<DesignId>{};
    for (const Vehicle& x : v.vehicles)
        for (const UnitStack& st : groupStacks(x)) seen.push_back(st.design);   // every design of a unit group
    std::sort(seen.begin(), seen.end());
    for (Design& d : v.designs) {
        if (d.owner == viewer) continue;
        resetDesignStatistics(d);
        if (std::binary_search(seen.begin(), seen.end(), d.id)) continue;
        d.name = "Unknown design";
        d.designType.clear();
        d.entries.clear();
    }

    // Messages to or from us; battles we fought; our own pending events.
    std::erase_if(v.messages, [&](const DiplomaticMessage& m) { return m.from != viewer && m.to != viewer; });
    std::erase_if(v.combats, [&](const CombatRecord& c) {
        return std::find(c.participants.begin(), c.participants.end(), viewer) == c.participants.end();
    });
    std::erase_if(v.pendingEvents, [&](const PendingEvent& p) { return p.empire != viewer; });
    std::erase_if(v.pendingMood, [&](const MoodEvent& m) { return m.empire != viewer; });

    // Turn-based games: whose turn it is is public; what that player's groups
    // did during it, and the questions it has open, only for that player.
    if (v.playerTurn.empire != viewer || spectator) {
        auto own = [&](VehicleId id) {
            const Vehicle* x = v.vehicle(id);
            return x && x->owner == viewer;
        };
        std::erase_if(v.playerTurn.moves, [&](const TurnMoves& m) { return !own(m.vehicle); });
        std::erase_if(v.playerTurn.launched, [&](const TurnLaunches& l) {
            if (l.vehicle.valid()) return !own(l.vehicle);
            const Colony* c = v.colony(l.planet);
            return !c || c->owner != viewer;
        });
        v.playerTurn.questions.clear();
    }

    // A map's starting points tell where the players started.
    v.startingPoints.clear();
    // Facilities left on abandoned planets are seen only by whoever colonizes them.
    v.leftFacilities.clear();

    // The random stream would let a client predict the next turn.
    v.rng = Rng(0);
    return v;
}

} // namespace opense4::game
