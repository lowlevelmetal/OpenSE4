#include "game/sight.hpp"

namespace opense4::game::sight {

// Minimal version: presence from own vehicles and colonies, every vehicle in
// a present system is visible. The vehicles work package replaces this with
// sensors, cloaking and scanners (docs/spec/01 §6).

bool hasPresence(const GameState& s, EmpireId viewer, SystemId sys) {
    const Empire& e = s.empire(viewer);
    return sys.index() < e.knowledge.present.size() && e.knowledge.present[sys.index()];
}

bool canSeeVehicle(const Rules&, const GameState& s, EmpireId viewer, const Vehicle& v) {
    return v.owner == viewer || hasPresence(s, viewer, v.location.system) || s.options.omnipresent;
}

bool canSeePlanet(const Rules&, const GameState& s, EmpireId viewer, ObjectId planet) {
    return s.empire(viewer).hasExplored(s.galaxy.object(planet).system);
}

void updateKnowledge(const Rules& r, GameState& s) {
    const size_t nSys = s.galaxy.systems.size();
    for (Empire& e : s.empires) {
        e.knowledge.present.assign(nSys, 0);
        e.knowledge.explored.resize(nSys, 0);
        e.knowledge.lastSeen.resize(nSys, 0);
        e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), 0);
    }
    auto mark = [&](EmpireId id, SystemId sys) {
        Empire& e = s.empire(id);
        e.knowledge.present[sys.index()] = 1;
        e.knowledge.explored[sys.index()] = 1;
        e.knowledge.lastSeen[sys.index()] = s.turn;
    };
    for (const Vehicle& v : s.vehicles)
        if (v.owner.valid()) mark(v.owner, v.location.system);
    for (const auto& c : s.colonies)
        if (c) mark(c->owner, s.galaxy.object(c->planet).system);
    for (Empire& e : s.empires) {
        e.knowledge.visibleVehicles.clear();
        for (const Vehicle& v : s.vehicles)
            if (v.owner != e.id && canSeeVehicle(r, s, e.id, v)) e.knowledge.visibleVehicles.push_back(v.id);
    }
}

} // namespace opense4::game::sight
