#include "sim/ai.hpp"

#include "sim/commands.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <algorithm>
#include <array>
#include <set>

namespace opense4::sim {

namespace {

class Ai {
public:
    Ai(GameState& s, const Content& c, EmpireId id) : s_(s), c_(c), id_(id) {}

    void run() {
        planResearch();
        collectExistingTargets();
        for (const ShipId shipId : ownShipIds()) orderShip(shipId);
        for (Planet& p : s_.planets)
            if (p.colony && p.colony->owner == id_ && p.colony->queue.empty()) planConstruction(p);
    }

private:
    Empire& empire() { return s_.empire(id_); }

    std::vector<ShipId> ownShipIds() const {
        std::vector<ShipId> ids;
        for (const Ship& ship : s_.ships)
            if (ship.owner == id_) ids.push_back(ship.id);
        return ids;
    }

    void planResearch() {
        std::vector<TechIndex> queue = empire().researchQueue;
        std::erase_if(queue, [&](TechIndex t) { return !canResearch(c_, empire(), t); });
        while (queue.size() < 2) {
            std::optional<TechIndex> best;
            for (size_t i = 0; i < c_.techs.size(); ++i) {
                const TechIndex t{i};
                if (!canResearch(c_, empire(), t) || std::find(queue.begin(), queue.end(), t) != queue.end()) continue;
                if (!best || nextLevelCost(c_, empire(), t) < nextLevelCost(c_, empire(), *best)) best = t;
            }
            if (!best) break;
            queue.push_back(*best);
        }
        (void)applyCommand(s_, c_, id_, cmd::SetResearchQueue{queue});
    }

    void collectExistingTargets() {
        for (const Ship& ship : s_.ships) {
            if (ship.owner != id_) continue;
            if (ship.order.type == OrderType::Colonize) colonyTargets_.insert(ship.order.planet);
            if (!ship.path.empty()) exploreTargets_.insert(ship.path.back().system);
        }
    }

    std::optional<PlanetId> bestColonyTarget(const Ship& ship) {
        const DesignStats& st = s_.statsOf(ship);
        const std::vector<int> hops = warpHopDistances(s_, ship.location.system, &empire());
        std::optional<PlanetId> best;
        int bestScore = INT32_MIN;
        for (const Planet& p : s_.planets) {
            if (p.colony || !empire().hasExplored(p.system) || !st.canColonize(p.surface)) continue;
            if (colonyTargets_.contains(p.id) || hops[p.system.index()] < 0) continue;
            int score = static_cast<int>(enumIndex(p.size)) * 10 + (canBreathe(empire(), p) ? 30 : 0);
            score += (p.value[0] + p.value[1] + p.value[2]) / 15;
            score -= hops[p.system.index()] * 8;
            if (score > bestScore) {
                bestScore = score;
                best = p.id;
            }
        }
        return best;
    }

    void orderShip(ShipId shipId) {
        const Ship* ship = s_.findShip(shipId);
        if (!ship) return;
        const DesignStats& st = s_.statsOf(*ship);

        if (ship->order.type == OrderType::Colonize) {
            // Keep going unless someone else got there first.
            if (colonizeProblem(s_, *ship, s_.planet(ship->order.planet)).empty()) return;
            colonyTargets_.erase(ship->order.planet);
            (void)applyCommand(s_, c_, id_, cmd::StopShip{shipId});
        } else if (!ship->path.empty()) {
            return;
        }

        if (!st.colonizes.empty()) {
            if (auto target = bestColonyTarget(*ship)) {
                if (applyCommand(s_, c_, id_, cmd::Colonize{shipId, *target})) colonyTargets_.insert(*target);
                return;
            }
        }

        if (st.speed <= 0) return;
        const Empire& e = empire();
        auto path = findPathTo(
            s_, ship->location, [&](Location l) { return !e.hasExplored(l.system) && !exploreTargets_.contains(l.system); }, &e);
        if (path && !path->empty()) {
            exploreTargets_.insert(path->back().system);
            (void)applyCommand(s_, c_, id_, cmd::MoveShip{shipId, path->back()});
            return;
        }

        // Nothing left to explore: warships gather at the homeworld.
        if (st.armed() && e.homeworld.valid()) {
            const Location home = s_.planet(e.homeworld).location();
            if (ship->location != home) (void)applyCommand(s_, c_, id_, cmd::MoveShip{shipId, home});
        }
    }

    std::optional<DesignId> designFor(std::string_view role) {
        std::optional<DesignId> found;
        for (DesignId d : empire().designs)
            if (s_.design(d).role == role && !s_.design(d).obsolete && s_.design(d).stats.problems.empty()) found = d;
        return found;
    }

    int countRole(std::string_view role) {
        int n = 0;
        for (const Ship& ship : s_.ships)
            if (ship.owner == id_ && s_.design(ship.design).role == role) ++n;
        for (const Planet& p : s_.planets)
            if (p.colony && p.colony->owner == id_)
                for (const auto& item : p.colony->queue)
                    if (item.kind == ConstructionKind::Ship && s_.design(item.design).role == role) ++n;
        return n;
    }

    bool anyUnexplored() {
        return std::any_of(empire().explored.begin(), empire().explored.end(), [](uint8_t x) { return x == 0; });
    }

    int colonyTargetCount(DesignId colonyDesign) {
        const DesignStats& st = s_.design(colonyDesign).stats;
        int n = 0;
        for (const Planet& p : s_.planets)
            if (!p.colony && empire().hasExplored(p.system) && st.canColonize(p.surface)) ++n;
        return n;
    }

    bool tryFacility(Planet& p) {
        static constexpr std::array<std::string_view, 8> kOrder{
            "mineral_miner", "research_lab", "organics_farm", "radioactives_extractor",
            "advanced_mineral_miner", "research_complex", "space_yard", "mineral_miner"};
        const Colony& col = *p.colony;
        if (usedFacilitySlots(col) >= facilitySlots(c_, p)) return false;
        const size_t start = col.facilities.size() % kOrder.size();
        for (size_t i = 0; i < kOrder.size(); ++i) {
            const auto fac = c_.findFacility(kOrder[(start + i) % kOrder.size()]);
            if (!fac || !isAvailable(c_, empire(), *fac)) continue;
            if (c_.facility(*fac).abilityTotal(AbilityType::SpaceYard) > 0 && hasSpaceYard(c_, col)) continue;
            if (applyCommand(s_, c_, id_, cmd::BuildFacility{p.id, *fac})) return true;
        }
        return false;
    }

    void planConstruction(Planet& p) {
        if (!hasSpaceYard(c_, *p.colony)) {
            tryFacility(p);
            return;
        }
        const auto scout = designFor("scout");
        const auto colony = designFor("colony");
        const auto warship = designFor("warship");
        if (scout && countRole("scout") < 2 && anyUnexplored()) {
            (void)applyCommand(s_, c_, id_, cmd::BuildShip{p.id, *scout});
        } else if (colony && countRole("colony") < 2 && colonyTargetCount(*colony) > countRole("colony")) {
            (void)applyCommand(s_, c_, id_, cmd::BuildShip{p.id, *colony});
        } else if (!tryFacility(p) && warship && countRole("warship") < 6) {
            (void)applyCommand(s_, c_, id_, cmd::BuildShip{p.id, *warship});
        }
    }

    GameState& s_;
    const Content& c_;
    EmpireId id_;
    std::set<PlanetId> colonyTargets_;
    std::set<SystemId> exploreTargets_;
};

} // namespace

void runAi(GameState& s, const Content& c, EmpireId empire) { Ai(s, c, empire).run(); }

} // namespace opense4::sim
