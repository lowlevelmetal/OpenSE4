#include "sim/turn.hpp"

#include "sim/ai.hpp"
#include "sim/mutate.hpp"
#include "sim/rules.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace opense4::sim {

namespace {

std::string sectorLabel(const GameState& s, Location loc) {
    return std::format("{} ({}, {})", s.system(loc.system).name, loc.sector.x, loc.sector.y);
}

// True if a ship arriving at `loc` would end up in a fight there.
bool combatAt(const GameState& s, const Ship& mover, Location loc) {
    const bool moverArmed = s.statsOf(mover).armed();
    for (const Ship& other : s.ships) {
        if (other.location != loc || !atWar(s, mover.owner, other.owner)) continue;
        if (moverArmed || s.statsOf(other).armed()) return true;
    }
    return false;
}

void resolveMovement(GameState& s) {
    struct Step {
        int k;      // step number (1-based)
        int speed;  // steps happen at times k / speed
        ShipId ship;
    };
    std::vector<Step> steps;
    for (const Ship& ship : s.ships) {
        const int speed = s.statsOf(ship).speed;
        const int count = std::min<int>(ship.movesLeft, static_cast<int>(ship.path.size()));
        for (int k = 1; k <= count; ++k) steps.push_back({k, speed, ship.id});
    }
    std::sort(steps.begin(), steps.end(), [](const Step& a, const Step& b) {
        const int64_t lhs = int64_t{a.k} * b.speed;
        const int64_t rhs = int64_t{b.k} * a.speed;
        return lhs != rhs ? lhs < rhs : a.ship < b.ship;
    });

    std::set<ShipId> halted;
    for (const Step& step : steps) {
        Ship* ship = s.findShip(step.ship);
        if (!ship || ship->path.empty() || ship->movesLeft <= 0 || halted.contains(ship->id)) continue;

        const Location next = ship->path.front();
        ship->path.erase(ship->path.begin());
        ship->location = next;
        --ship->movesLeft;

        if (markExplored(s, ship->owner, next.system))
            addEvent(s, ship->owner, EventKind::Exploration,
                     std::format("{} has explored the {} system.", ship->name, s.system(next.system).name), next);

        if (ship->path.empty() && ship->order.type == OrderType::Move) {
            ship->order = {};
            addEvent(s, ship->owner, EventKind::Movement, std::format("{} has arrived at {}.", ship->name, sectorLabel(s, next)), next);
        }

        if (combatAt(s, *ship, next)) {
            // Both the intruder and the hostiles it ran into stop here and fight.
            halted.insert(ship->id);
            for (const Ship& other : s.ships)
                if (other.location == next && atWar(s, ship->owner, other.owner)) halted.insert(other.id);
            if (!ship->path.empty())
                addEvent(s, ship->owner, EventKind::Combat,
                         std::format("{} was intercepted by hostile ships at {}.", ship->name, sectorLabel(s, next)), next);
        }
    }
}

void resolveBattle(GameState& s, const Content& c, Location loc, const std::vector<ShipId>& shipIds) {
    struct Combatant {
        ShipId id;
        EmpireId owner;
        int shields;
        int hull;  // structure remaining
        std::vector<int> weapons;
        bool alive = true;
    };
    std::vector<Combatant> fighters;
    for (ShipId id : shipIds) {
        const Ship& ship = *s.findShip(id);
        const Design& d = s.design(ship.design);
        Combatant f{id, ship.owner, d.stats.shields, d.stats.structure - ship.damage, {}};
        for (ComponentIndex ci : d.components)
            if (const auto& w = c.component(ci).weapon) f.weapons.push_back(w->damage);
        fighters.push_back(std::move(f));
    }

    // Rounds are simultaneous: every ship alive at the start of a round fires,
    // even if it is destroyed during that round, so ship ids give no advantage.
    // Shots only go to targets that still have hull left.
    std::vector<size_t> targets;
    std::vector<size_t> order(fighters.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    for (int round = 0; round < c.rules.combatRounds; ++round) {
        bool fired = false;
        s.rng.shuffle(order);
        for (size_t shooterIndex : order) {
            const Combatant& shooter = fighters[shooterIndex];
            if (!shooter.alive) continue;
            for (int damage : shooter.weapons) {
                targets.clear();
                for (size_t i = 0; i < fighters.size(); ++i)
                    if (fighters[i].hull > 0 && atWar(s, shooter.owner, fighters[i].owner)) targets.push_back(i);
                if (targets.empty()) break;
                Combatant& target = fighters[targets[s.rng.below(targets.size())]];
                const int absorbed = std::min(target.shields, damage);
                target.shields -= absorbed;
                target.hull -= damage - absorbed;
                fired = true;
            }
        }
        for (Combatant& f : fighters) f.alive = f.hull > 0;
        if (!fired) break;
    }

    std::set<EmpireId> participants;
    for (const Combatant& f : fighters) participants.insert(f.owner);
    std::map<EmpireId, std::pair<int, int>> tally;  // empire -> (lost, destroyed)
    for (EmpireId e : participants) tally[e] = {0, 0};
    for (const Combatant& f : fighters) {
        Ship& ship = *s.findShip(f.id);
        ship.damage = s.statsOf(ship).structure - std::max(0, f.hull);
        if (f.alive) continue;
        ++tally[f.owner].first;
        for (EmpireId e : participants)
            if (atWar(s, e, f.owner)) ++tally[e].second;
    }
    for (const auto& [empire, counts] : tally) {
        std::set<EmpireId> enemies;
        for (EmpireId e : participants)
            if (atWar(s, empire, e)) enemies.insert(e);
        std::string foes;
        for (EmpireId e : enemies) foes += (foes.empty() ? "" : ", ") + s.empire(e).name;
        addEvent(s, empire, EventKind::Combat,
                 std::format("Battle at {} against the {}: we lost {} ship(s) and destroyed {}.", sectorLabel(s, loc), foes,
                             counts.first, counts.second),
                 loc);
    }
    std::erase_if(s.ships, [&](const Ship& ship) {
        return std::any_of(fighters.begin(), fighters.end(), [&](const Combatant& f) { return f.id == ship.id && !f.alive; });
    });
}

void resolveCombat(GameState& s, const Content& c) {
    std::map<Location, std::vector<ShipId>> byLocation;
    for (const Ship& ship : s.ships) byLocation[ship.location].push_back(ship.id);
    for (const auto& [loc, ids] : byLocation) {
        bool hostile = false;
        bool armed = false;
        for (ShipId a : ids) {
            const Ship& sa = *s.findShip(a);
            armed = armed || s.statsOf(sa).armed();
            for (ShipId b : ids)
                if (atWar(s, sa.owner, s.findShip(b)->owner)) hostile = true;
        }
        if (hostile && armed) resolveBattle(s, c, loc, ids);
    }
}

void resolveColonization(GameState& s, const Content& c) {
    // Random order, so rival colony ships arriving together have an equal chance.
    std::vector<ShipId> candidates;
    for (const Ship& ship : s.ships)
        if (ship.order.type == OrderType::Colonize && ship.location == ship.order.destination) candidates.push_back(ship.id);
    s.rng.shuffle(candidates);

    std::vector<ShipId> consumed;
    for (ShipId id : candidates) {
        Ship& ship = *s.findShip(id);
        Planet& planet = s.planet(ship.order.planet);
        if (std::string problem = colonizeProblem(s, ship, planet); !problem.empty()) {
            addEvent(s, ship.owner, EventKind::Colonization,
                     std::format("{} could not colonize {}: {}", ship.name, planet.name, problem), planet.location());
            ship.order = {};
            continue;
        }
        foundColony(s, planet.id, ship.owner, c.rules.colonyStartPopulation);
        addEvent(s, ship.owner, EventKind::Colonization, std::format("A new colony has been founded on {}!", planet.name),
                 planet.location());
        consumed.push_back(ship.id);
    }
    std::erase_if(s.ships, [&](const Ship& ship) { return std::find(consumed.begin(), consumed.end(), ship.id) != consumed.end(); });
}

void resolveEconomy(GameState& s, const Content& c) {
    for (Empire& e : s.empires) {
        e.lastIncome = {};
        e.lastResearch = 0;
    }
    for (const Planet& p : s.planets) {
        if (!p.colony) continue;
        const ColonyOutput out = colonyOutput(c, s, p);
        Empire& e = s.empire(p.colony->owner);
        e.lastIncome += out.resources;
        e.lastResearch += out.research;
    }
    for (Empire& e : s.empires) {
        if (!e.alive) continue;
        e.stockpile += e.lastIncome;

        int64_t pool = e.lastResearch + e.unspentResearch;
        e.unspentResearch = 0;
        while (pool > 0) {
            std::erase_if(e.researchQueue, [&](TechIndex t) { return e.techLevel(t) >= c.tech(t).maxLevel; });
            const auto it = std::find_if(e.researchQueue.begin(), e.researchQueue.end(),
                                         [&](TechIndex t) { return canResearch(c, e, t); });
            if (it == e.researchQueue.end()) {
                e.unspentResearch += pool;
                break;
            }
            const TechIndex t = *it;
            int64_t& progress = e.techProgress[t.index()];
            const int64_t spend = std::min(pool, nextLevelCost(c, e, t) - progress);
            progress += spend;
            pool -= spend;
            if (progress >= nextLevelCost(c, e, t)) {
                progress = 0;
                const int level = ++e.techLevels[t.index()];
                e.researchQueue.erase(it);  // each queue entry is one level
                addEvent(s, e.id, EventKind::Research, std::format("Research complete: {} level {}.", c.tech(t).name, level));
            }
        }
    }
}

void resolveConstruction(GameState& s, const Content& c) {
    for (Planet& p : s.planets) {
        if (!p.colony || p.colony->queue.empty()) continue;
        Colony& col = *p.colony;
        Empire& e = s.empire(col.owner);
        const int64_t rate = constructionRate(c, col);
        Resources budget{rate, rate, rate};
        const bool yard = hasSpaceYard(c, col);

        while (!col.queue.empty()) {
            ConstructionItem& item = col.queue.front();
            if (item.kind == ConstructionKind::Ship && !yard) break;
            const Resources spend = componentMin(componentMin(item.cost - item.spent, budget), e.stockpile);
            item.spent += spend;
            budget -= spend;
            e.stockpile -= spend;
            if (item.spent != item.cost) break;  // out of budget or stockpile this turn

            if (item.kind == ConstructionKind::Ship) {
                const ShipId id = spawnShip(s, col.owner, item.design, p.location());
                addEvent(s, col.owner, EventKind::Construction, std::format("{} has been completed at {}.", s.findShip(id)->name, p.name),
                         p.location());
            } else {
                col.facilities.push_back(item.facility);
                addEvent(s, col.owner, EventKind::Construction,
                         std::format("{} has been completed on {}.", c.facility(item.facility).name, p.name), p.location());
            }
            col.queue.erase(col.queue.begin());
        }
    }
}

void resolveGrowthAndUpkeep(GameState& s, const Content& c) {
    for (Planet& p : s.planets) {
        if (!p.colony) continue;
        Colony& col = *p.colony;
        const int64_t maxPop = maxPopulation(c, s.empire(col.owner), p);
        if (col.population < maxPop) {
            const int64_t growth = std::max<int64_t>(1, col.population * c.rules.popGrowthPercent / 100);
            col.population = std::min(maxPop, col.population + growth);
        }
    }

    // Ships in a system where their empire has a space yard are fully repaired.
    for (Ship& ship : s.ships) {
        if (ship.damage <= 0) continue;
        for (PlanetId pid : s.system(ship.location.system).planets) {
            const Planet& p = s.planet(pid);
            if (p.colony && p.colony->owner == ship.owner && hasSpaceYard(c, *p.colony)) {
                ship.damage = 0;
                break;
            }
        }
    }

    for (Empire& e : s.empires) {
        if (!e.alive || colonyCount(s, e.id) > 0 || shipCount(s, e.id) > 0) continue;
        e.alive = false;
        for (const Empire& other : s.empires)
            addEvent(s, other.id, EventKind::Info, std::format("The {} has been eliminated.", e.name));
    }
}

} // namespace

void processTurn(GameState& s, const Content& c) {
    resolveMovement(s);
    resolveCombat(s, c);
    resolveColonization(s, c);
    resolveEconomy(s, c);
    resolveConstruction(s, c);
    resolveGrowthAndUpkeep(s, c);

    ++s.turn;
    for (Ship& ship : s.ships) ship.movesLeft = s.statsOf(ship).speed;
}

void advanceTurn(GameState& s, const Content& c) {
    s.events.clear();
    for (const Empire& e : s.empires)
        if (e.ai && e.alive) runAi(s, c, e.id);
    processTurn(s, c);
}

} // namespace opense4::sim
