#include "sim/commands.hpp"

#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <format>

namespace opense4::sim {

namespace {

std::unexpected<std::string> fail(std::string message) { return std::unexpected(std::move(message)); }

class Applier {
public:
    Applier(GameState& s, const Content& c, EmpireId issuer) : s_(s), c_(c), issuer_(issuer) {}

    CommandResult operator()(const cmd::MoveShip& m) {
        Ship* ship = ownShip(m.ship);
        if (!ship) return fail("You do not control that ship.");
        if (!validLocation(m.destination)) return fail("Invalid destination.");
        if (s_.statsOf(*ship).speed <= 0) return fail("This ship has no engines.");
        auto path = findPath(s_, ship->location, m.destination, &s_.empire(issuer_));
        if (!path) return fail("No known route to that destination.");
        ship->path = std::move(*path);
        ship->order = ShipOrder{ship->path.empty() ? OrderType::None : OrderType::Move, m.destination, {}};
        return {};
    }

    CommandResult operator()(const cmd::Colonize& m) {
        Ship* ship = ownShip(m.ship);
        if (!ship) return fail("You do not control that ship.");
        if (!m.planet.valid() || m.planet.index() >= s_.planets.size()) return fail("Invalid planet.");
        const Planet& planet = s_.planet(m.planet);
        if (!s_.empire(issuer_).hasExplored(planet.system)) return fail("That system has not been explored.");
        if (std::string problem = colonizeProblem(s_, *ship, planet); !problem.empty()) return fail(problem);
        auto path = findPath(s_, ship->location, planet.location(), &s_.empire(issuer_));
        if (!path) return fail("No known route to that planet.");
        if (!path->empty() && s_.statsOf(*ship).speed <= 0) return fail("This ship has no engines.");
        ship->path = std::move(*path);
        ship->order = ShipOrder{OrderType::Colonize, planet.location(), planet.id};
        return {};
    }

    CommandResult operator()(const cmd::StopShip& m) {
        Ship* ship = ownShip(m.ship);
        if (!ship) return fail("You do not control that ship.");
        ship->path.clear();
        ship->order = {};
        return {};
    }

    CommandResult operator()(const cmd::BuildShip& m) {
        Colony* col = ownColony(m.planet);
        if (!col) return fail("You do not own a colony there.");
        if (!hasSpaceYard(c_, *col)) return fail("Ships can only be built at colonies with a space yard.");
        if (!m.design.valid() || m.design.index() >= s_.designs.size()) return fail("Invalid design.");
        const Design& d = s_.design(m.design);
        if (d.owner != issuer_) return fail("That design belongs to another empire.");
        if (!d.stats.problems.empty()) return fail(std::format("Design '{}' is invalid: {}", d.name, d.stats.problems.front()));
        if (!isAvailable(c_, s_.empire(issuer_), d.hull)) return fail("Hull technology not yet researched.");
        for (ComponentIndex ci : d.components)
            if (!isAvailable(c_, s_.empire(issuer_), ci))
                return fail(std::format("Component '{}' not yet researched.", c_.component(ci).name));
        if (m.count < 1 || m.count > 100) return fail("Invalid count.");
        for (int i = 0; i < m.count; ++i)
            col->queue.push_back(ConstructionItem{ConstructionKind::Ship, d.id, {}, d.stats.cost, {}});
        return {};
    }

    CommandResult operator()(const cmd::BuildFacility& m) {
        Colony* col = ownColony(m.planet);
        if (!col) return fail("You do not own a colony there.");
        if (!m.facility.valid() || m.facility.index() >= c_.facilities.size()) return fail("Invalid facility.");
        if (!isAvailable(c_, s_.empire(issuer_), m.facility)) return fail("Facility technology not yet researched.");
        if (usedFacilitySlots(*col) >= facilitySlots(c_, s_.planet(m.planet))) return fail("No free facility slots.");
        col->queue.push_back(ConstructionItem{ConstructionKind::Facility, {}, m.facility, c_.facility(m.facility).cost, {}});
        return {};
    }

    CommandResult operator()(const cmd::CancelConstruction& m) {
        Colony* col = ownColony(m.planet);
        if (!col) return fail("You do not own a colony there.");
        if (m.index < 0 || m.index >= static_cast<int>(col->queue.size())) return fail("Invalid queue position.");
        s_.empire(issuer_).stockpile += col->queue[static_cast<size_t>(m.index)].spent;
        col->queue.erase(col->queue.begin() + m.index);
        return {};
    }

    CommandResult operator()(const cmd::SetResearchQueue& m) {
        for (TechIndex t : m.queue) {
            if (!t.valid() || t.index() >= c_.techs.size()) return fail("Invalid technology.");
            if (s_.empire(issuer_).techLevel(t) >= c_.tech(t).maxLevel)
                return fail(std::format("{} is already fully researched.", c_.tech(t).name));
        }
        s_.empire(issuer_).researchQueue = m.queue;
        return {};
    }

private:
    Ship* ownShip(ShipId id) {
        Ship* ship = s_.findShip(id);
        return (ship && ship->owner == issuer_) ? ship : nullptr;
    }

    Colony* ownColony(PlanetId id) {
        if (!id.valid() || id.index() >= s_.planets.size()) return nullptr;
        Planet& p = s_.planet(id);
        return (p.colony && p.colony->owner == issuer_) ? &*p.colony : nullptr;
    }

    bool validLocation(Location l) const {
        return l.system.valid() && l.system.index() < s_.systems.size() && s_.inBounds(l.sector);
    }

    GameState& s_;
    const Content& c_;
    EmpireId issuer_;
};

} // namespace

CommandResult applyCommand(GameState& s, const Content& c, EmpireId issuer, const Command& command) {
    if (!issuer.valid() || issuer.index() >= s.empires.size() || !s.empire(issuer).alive)
        return fail("Invalid empire.");
    return std::visit(Applier(s, c, issuer), command);
}

} // namespace opense4::sim
